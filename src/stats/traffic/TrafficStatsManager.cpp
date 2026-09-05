#include "include/stats/traffic/TrafficStatsManager.hpp"

#include "include/database/DatabaseManager.h"
#include "include/global/Utils.hpp"

#include <QDateTime>
#include <QThread>

namespace Stats {

    TrafficStatsManager* trafficStatsManager = new TrafficStatsManager;

    namespace {
        // Fixed: the retention setting governs the hour tier, not this window.
        constexpr long long kMinuteWindowSecs = 48LL * 3600LL;
        constexpr unsigned long kRollupIntervalMs = 10UL * 60UL * 1000UL;

        long long alignMinute(long long secs) { return (secs / 60) * 60; }
    }

    bool TrafficStatsManager::aggregationDisabled() {
        return Configs::dataManager && Configs::dataManager->settingsRepo &&
               Configs::dataManager->settingsRepo->disable_traffic_aggregation;
    }

    void TrafficStatsManager::Init() {
        if (started.exchange(true)) return;
        // Catch-up pass first: downsample whatever aged past the window while the app was closed.
        runOnNewThread([this] {
            runRollupOnce();
            while (true) {
                QThread::msleep(kRollupIntervalMs);
                runRollupOnce();
            }
        });
    }

    void TrafficStatsManager::drainLocked(long long bucket,
                                          QList<Configs::ConfigTrafficRow>& cfg,
                                          QList<Configs::AppTrafficRow>& app) {
        for (auto it = configAccum.constBegin(); it != configAccum.constEnd(); ++it) {
            if (it.value().up == 0 && it.value().down == 0) continue;
            cfg.append(Configs::ConfigTrafficRow{bucket, it.key(), it.value().up, it.value().down});
        }
        for (auto it = appAccum.constBegin(); it != appAccum.constEnd(); ++it) {
            if (it.value().up == 0 && it.value().down == 0) continue;
            app.append(Configs::AppTrafficRow{bucket, it.key().first, it.key().second,
                                              it.value().up, it.value().down});
        }
        configAccum.clear();
        appAccum.clear();
    }

    void TrafficStatsManager::writeRows(const QList<Configs::ConfigTrafficRow>& cfg,
                                        const QList<Configs::AppTrafficRow>& app) {
        if (!Configs::dataManager || !Configs::dataManager->trafficStatsRepo) return;
        if (!cfg.isEmpty()) Configs::dataManager->trafficStatsRepo->UpsertConfigMinuteBatch(cfg);
        if (!app.isEmpty()) Configs::dataManager->trafficStatsRepo->UpsertAppMinuteBatch(app);
    }

    void TrafficStatsManager::AddConfigDelta(int profileId, long long up, long long down) {
        if (aggregationDisabled()) return;
        if (up == 0 && down == 0) return;
        const long long nowBucket = alignMinute(QDateTime::currentSecsSinceEpoch());
        QList<Configs::ConfigTrafficRow> cfg;
        QList<Configs::AppTrafficRow> app;
        {
            QMutexLocker lk(&mu);
            if (currentBucket != 0 && nowBucket != currentBucket) {
                drainLocked(currentBucket, cfg, app); // flush the bucket that just ended
            }
            currentBucket = nowBucket;
            auto& d = configAccum[profileId];
            d.up += up;
            d.down += down;
        }
        if (!cfg.isEmpty() || !app.isEmpty()) writeRows(cfg, app);
    }

    void TrafficStatsManager::AddAppDelta(const QString& processName, const QString& path,
                                          const QString& outbound, long long up, long long down) {
        if (aggregationDisabled()) return;
        if (processName.isEmpty() || (up == 0 && down == 0)) return;
        const long long nowBucket = alignMinute(QDateTime::currentSecsSinceEpoch());
        QList<Configs::ConfigTrafficRow> cfg;
        QList<Configs::AppTrafficRow> app;
        bool writeMeta = false;
        {
            QMutexLocker lk(&mu);
            if (currentBucket != 0 && nowBucket != currentBucket) {
                drainLocked(currentBucket, cfg, app);
            }
            currentBucket = nowBucket;
            auto& d = appAccum[{processName, outbound}];
            d.up += up;
            d.down += down;
            if (!path.isEmpty()) {
                const auto it = appMetaSeen.constFind(processName);
                if (it == appMetaSeen.constEnd() || it.value() != path) {
                    appMetaSeen.insert(processName, path);
                    writeMeta = true;
                }
            }
        }
        if (!cfg.isEmpty() || !app.isEmpty()) writeRows(cfg, app);
        if (writeMeta && Configs::dataManager && Configs::dataManager->trafficStatsRepo) {
            Configs::dataManager->trafficStatsRepo->UpsertAppMeta(
                processName, path, QDateTime::currentSecsSinceEpoch());
        }
    }

    void TrafficStatsManager::Flush() {
        if (aggregationDisabled()) return;
        QList<Configs::ConfigTrafficRow> cfg;
        QList<Configs::AppTrafficRow> app;
        {
            QMutexLocker lk(&mu);
            if (currentBucket == 0) return;
            drainLocked(currentBucket, cfg, app);
        }
        writeRows(cfg, app);
    }

    void TrafficStatsManager::SnapshotConfigMeta(int profileId, const QString& name,
                                                 const QString& groupName, const QString& type,
                                                 const QString& serverAddress) {
        if (aggregationDisabled()) return;
        if (!Configs::dataManager || !Configs::dataManager->trafficStatsRepo) return;
        if (profileId < 0 && profileId != DIRECT_STAT_PROFILE_ID) return;
        const long long now = QDateTime::currentSecsSinceEpoch();
        Configs::ConfigMetaRow m;
        m.profile_id = profileId;
        m.name = name;
        m.group_name = groupName;
        m.type = type;
        m.server_address = serverAddress;
        m.first_seen = now;
        m.last_seen = now;
        Configs::dataManager->trafficStatsRepo->UpsertConfigMeta(m);
    }

    void TrafficStatsManager::EnsureDirectMeta() {
        SnapshotConfigMeta(DIRECT_STAT_PROFILE_ID, "Direct", "", "direct", "");
    }

    void TrafficStatsManager::runRollupOnce() {
        if (aggregationDisabled()) return;
        if (!Configs::dataManager || !Configs::dataManager->trafficStatsRepo) return;
        const long long now = QDateTime::currentSecsSinceEpoch();
        Configs::dataManager->trafficStatsRepo->RollupMinuteToHour(now - kMinuteWindowSecs);

        int days = Configs::dataManager->settingsRepo->traffic_stats_retention_days;
        if (days < 1) days = 1;
        Configs::dataManager->trafficStatsRepo->PruneHour(now - static_cast<long long>(days) * 86400LL);
    }
}
