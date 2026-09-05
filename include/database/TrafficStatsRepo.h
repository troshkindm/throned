#pragma once

#include "Database.h"
#include <QString>
#include <QList>
#include <atomic>
#include <mutex>
#include <string>

namespace Configs {
    // bucket_start is a unix epoch second aligned to its tier (minute = multiple of 60, hour = multiple of 3600).
    struct ConfigTrafficRow {
        long long bucket_start = 0;
        int profile_id = 0;
        long long up = 0;
        long long down = 0;
    };

    struct AppTrafficRow {
        long long bucket_start = 0;
        QString process_name;
        // The outbound tag the bytes took. Empty means the row predates this column, which
        // is not the same as unknown routing and is reported apart from it.
        QString outbound;
        long long up = 0;
        long long down = 0;
    };

    struct ConfigUsage {
        int profile_id = 0;
        long long up = 0;
        long long down = 0;
    };

    struct AppUsage {
        QString process_name;
        long long up = 0;
        long long down = 0;
        // Subsets of up/down, never additions to them.
        long long direct_up = 0;
        long long direct_down = 0;
        long long unknown_up = 0;
        long long unknown_down = 0;
    };

    struct TrafficSeriesPoint {
        long long bucket_start = 0;
        long long up = 0;
        long long down = 0;
        long long direct_up = 0;
        long long direct_down = 0;
    };

    // Kept so deleted/renamed configs and moved apps still resolve in the dashboard.
    struct ConfigMetaRow {
        int profile_id = 0;
        QString name;
        QString group_name;
        QString type;
        QString server_address;
        long long first_seen = 0;
        long long last_seen = 0;
    };

    struct AppMetaRow {
        QString process_name;
        QString last_path;
        long long first_seen = 0;
        long long last_seen = 0;
    };

    // Every public method is serialized by `mu`, so one shared instance is safe to call from the looper, rollup and UI threads.
    // A fatal SQLite error, or kMaxConsecutiveFailures in a row, trips the breaker: every call is then a no-op until the next start.
    class TrafficStatsRepo {
    public:
        explicit TrafficStatsRepo(Database& database);

        // Upsert-add: accumulates into the existing minute bucket.
        void UpsertConfigMinuteBatch(const QList<ConfigTrafficRow>& rows);
        void UpsertAppMinuteBatch(const QList<AppTrafficRow>& rows);

        void UpsertConfigMeta(const ConfigMetaRow& meta);
        void UpsertAppMeta(const QString& processName, const QString& lastPath, long long nowSecs);

        // Atomic per call, so a crash never double-counts.
        void RollupMinuteToHour(long long olderThanSecs);
        void PruneHour(long long olderThanSecs);

        // Reads sum across both tiers over [fromSecs, toSecs).
        QList<ConfigUsage> QueryConfigUsage(long long fromSecs, long long toSecs);
        QList<AppUsage> QueryAppUsage(long long fromSecs, long long toSecs);
        QList<ConfigMetaRow> GetAllConfigMeta();
        QList<AppMetaRow> GetAllAppMeta();

        // Empty buckets are omitted; utcOffsetSecs (east of UTC) shifts the UTC-aligned grouping so bucket_start is the local boundary epoch.
        // directProfileId names the pseudo-profile that stands for direct egress, so the
        // point can carry its bypassed share. 0 is never a real profile id, so the default
        // simply reports no bypass for callers that do not care.
        QList<TrafficSeriesPoint> QueryConfigSeries(long long fromSecs, long long toSecs, long long bucketSecs,
                                                    long long utcOffsetSecs, int directProfileId = 0);
        QList<TrafficSeriesPoint> QueryAppSeries(long long fromSecs, long long toSecs, long long bucketSecs, long long utcOffsetSecs);

        [[nodiscard]] bool Disabled() const { return disabled.load(); }

    private:
        static constexpr int kMaxConsecutiveFailures = 5;

        Database& db;
        std::mutex mu;
        std::atomic<bool> disabled{false};
        int consecutiveFailures = 0; // guarded by mu

        void createTables();
        void migrateAppOutbound();
        // A member, not a file-local helper: the unity build would collide the symbol with another TU's.
        static std::string bucketExpr(long long bucketSecs, long long utcOffsetSecs);
        static std::string bypassExpr();

        // Failure handling runs after mu is released, so a notice can never nest an event loop under the lock.
        template<typename F>
        void guarded(const char* op, bool transactional, F&& body) {
            if (disabled.load()) return;
            DbError err;
            bool failed = false;
            bool trip = false;
            {
                std::lock_guard<std::mutex> lk(mu);
                try {
                    if (transactional) db.execThrow("BEGIN IMMEDIATE");
                    body();
                    if (transactional) db.execThrow("COMMIT");
                    consecutiveFailures = 0;
                } catch (std::exception& e) {
                    if (transactional) {
                        try { db.execThrow("ROLLBACK"); } catch (...) {}
                    }
                    err = DescribeDbError(e);
                    failed = true;
                    trip = IsFatalDbError(err) || ++consecutiveFailures >= kMaxConsecutiveFailures;
                }
            }
            if (failed) onFailure(op, err, trip);
        }

        template<typename F>
        void write(const char* op, F&& body) { guarded(op, true, std::forward<F>(body)); }

        template<typename F>
        void read(const char* op, F&& body) { guarded(op, false, std::forward<F>(body)); }

        void onFailure(const char* op, const DbError& err, bool trip);
    };
}
