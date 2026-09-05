#pragma once

#include <QString>
#include <QHash>
#include <QMutex>
#include <atomic>

#include "include/database/TrafficStatsRepo.h"

namespace Stats {
    // Direct (non-proxied) egress; negative so it cannot collide with a real profile id.
    constexpr int DIRECT_STAT_PROFILE_ID = -101;

    // Speed-test bytes bypass the clash tracker, so they are credited explicitly under this synthetic app.
    inline const QString SPEEDTEST_APP_NAME = QStringLiteral("Speedtest");

    class TrafficStatsManager {
    public:
        // Idempotent; requires the database manager to already exist.
        void Init();

        void AddConfigDelta(int profileId, long long up, long long down);

        // outbound is the tag the bytes actually took; empty only for a caller that cannot
        // know it, which reads back as "not recorded" rather than as direct.
        void AddAppDelta(const QString& processName, const QString& path, const QString& outbound,
                         long long up, long long down);

        void Flush();

        void SnapshotConfigMeta(int profileId, const QString& name, const QString& groupName,
                                const QString& type, const QString& serverAddress);
        void EnsureDirectMeta();

    private:
        struct Delta { long long up = 0; long long down = 0; };

        QMutex mu; // guards the accumulators + currentBucket
        QHash<int, Delta> configAccum;
        // Keyed by process and outbound together: one program routed two ways is two rows.
        QHash<QPair<QString, QString>, Delta> appAccum;
        QHash<QString, QString> appMetaSeen; // process name -> last path written to app_meta
        long long currentBucket = 0; // minute-aligned epoch secs; 0 = nothing yet

        std::atomic<bool> started{false};

        // Caller holds mu.
        void drainLocked(long long bucket, QList<Configs::ConfigTrafficRow>& cfg,
                         QList<Configs::AppTrafficRow>& app);
        // Call off-lock.
        void writeRows(const QList<Configs::ConfigTrafficRow>& cfg,
                       const QList<Configs::AppTrafficRow>& app);

        void runRollupOnce();

        [[nodiscard]] static bool aggregationDisabled();
    };

    extern TrafficStatsManager* trafficStatsManager;
}
