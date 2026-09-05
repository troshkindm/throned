#include "include/database/TrafficStatsRepo.h"
#include "include/global/Logger.hpp"

#include <QDir>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

// The real error path is linked in; only its two exits into the rest of the application
// are stubbed. A failure the repo swallows still shows up here as a recorded message.
static QStringList reportedFailures;

void Logging::Write(Level, const QString &message, const char *, int) { reportedFailures << message; }
void PostPassiveWarning(const QString &, const QString &text) { reportedFailures << text; }

class TestTrafficStats : public QObject {
    Q_OBJECT

    static constexpr long long kMinute = 60;
    static constexpr long long kBase = 1788000000LL / kMinute * kMinute;

    // The tables exactly as they shipped before the outbound column, so the migration is
    // exercised against the shape a real installation actually has on disk.
    static void createLegacyTables(Configs::Database &db) {
        db.execThrow(R"(CREATE TABLE app_traffic_minute (
            bucket_start INTEGER NOT NULL, process_name TEXT NOT NULL,
            up INTEGER NOT NULL DEFAULT 0, down INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (bucket_start, process_name)))");
        db.execThrow(R"(CREATE TABLE app_traffic_hour (
            bucket_start INTEGER NOT NULL, process_name TEXT NOT NULL,
            up INTEGER NOT NULL DEFAULT 0, down INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (bucket_start, process_name)))");
    }

    static Configs::AppUsage usageFor(const QList<Configs::AppUsage> &rows, const QString &name) {
        for (const auto &row : rows)
            if (row.process_name == name) return row;
        return {};
    }

private slots:
    void init() { reportedFailures.clear(); }

    void cleanup() {
        if (!reportedFailures.isEmpty()) QFAIL(qPrintable(reportedFailures.join(QStringLiteral("; "))));
    }

    void aLegacyDatabaseKeepsItsBytesAndReportsThemAsUnrecorded() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = QDir(dir.path()).filePath(QStringLiteral("stats.db")).toStdString();
        {
            Configs::Database db(path);
            createLegacyTables(db);
            db.execThrow("INSERT INTO app_traffic_minute (bucket_start, process_name, up, down) VALUES (?, ?, ?, ?)",
                         kBase, std::string("chrome.exe"), 100LL, 900LL);
            db.execThrow("INSERT INTO app_traffic_hour (bucket_start, process_name, up, down) VALUES (?, ?, ?, ?)",
                         kBase - 7200, std::string("chrome.exe"), 10LL, 90LL);
        }

        Configs::Database db(path);
        Configs::TrafficStatsRepo repo(db); // the constructor migrates
        const auto rows = repo.QueryAppUsage(kBase - 86400, kBase + 86400);
        QCOMPARE(rows.size(), 1);
        const auto chrome = usageFor(rows, QStringLiteral("chrome.exe"));
        // Not one byte may be lost or reclassified by widening the key.
        QCOMPARE(chrome.up, 110LL);
        QCOMPARE(chrome.down, 990LL);
        // And none of it may be claimed as direct, which was never recorded.
        QCOMPARE(chrome.direct_up, 0LL);
        QCOMPARE(chrome.direct_down, 0LL);
        QCOMPARE(chrome.unknown_up, 110LL);
        QCOMPARE(chrome.unknown_down, 990LL);
    }

    void oneProgramRoutedTwoWaysIsOneRowWithASplit() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Configs::Database db(QDir(dir.path()).filePath(QStringLiteral("stats.db")).toStdString());
        Configs::TrafficStatsRepo repo(db);
        repo.UpsertAppMinuteBatch({
            {kBase, QStringLiteral("chrome.exe"), QStringLiteral("proxy"), 100, 700},
            {kBase, QStringLiteral("chrome.exe"), QStringLiteral("direct"), 20, 180},
            // A second bypass tag, to prove the two are counted as one side of the split.
            {kBase, QStringLiteral("chrome.exe"), QStringLiteral("l3-direct"), 5, 15},
            {kBase, QStringLiteral("steam.exe"), QStringLiteral("direct"), 1, 2},
        });

        const auto rows = repo.QueryAppUsage(kBase - 60, kBase + 60);
        QCOMPARE(rows.size(), 2);
        const auto chrome = usageFor(rows, QStringLiteral("chrome.exe"));
        QCOMPARE(chrome.up, 125LL);
        QCOMPARE(chrome.down, 895LL);
        QCOMPARE(chrome.direct_up, 25LL);
        QCOMPARE(chrome.direct_down, 195LL);
        QCOMPARE(chrome.unknown_up, 0LL);
        QCOMPARE(chrome.unknown_down, 0LL);
        // Fully bypassed programs keep their whole total on the direct side.
        const auto steam = usageFor(rows, QStringLiteral("steam.exe"));
        QCOMPARE(steam.direct_up, steam.up);
        QCOMPARE(steam.direct_down, steam.down);

        const auto series = repo.QueryAppSeries(kBase - 60, kBase + 60, 3600, 0);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].up, 126LL);
        QCOMPARE(series[0].down, 897LL);
        QCOMPARE(series[0].direct_up, 26LL);
        QCOMPARE(series[0].direct_down, 197LL);
    }

    void theRollupKeepsTheOutboundApart() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Configs::Database db(QDir(dir.path()).filePath(QStringLiteral("stats.db")).toStdString());
        Configs::TrafficStatsRepo repo(db);
        repo.UpsertAppMinuteBatch({
            {kBase, QStringLiteral("chrome.exe"), QStringLiteral("proxy"), 100, 700},
            {kBase + 60, QStringLiteral("chrome.exe"), QStringLiteral("proxy"), 10, 70},
            {kBase, QStringLiteral("chrome.exe"), QStringLiteral("direct"), 20, 180},
        });
        // Downsampling minutes into hours must not merge the two tags into one bucket,
        // or every record older than the minute window loses its split.
        repo.RollupMinuteToHour(kBase + 3600);

        const auto rows = repo.QueryAppUsage(kBase - 86400, kBase + 86400);
        QCOMPARE(rows.size(), 1);
        const auto chrome = usageFor(rows, QStringLiteral("chrome.exe"));
        QCOMPARE(chrome.up, 130LL);
        QCOMPARE(chrome.down, 950LL);
        QCOMPARE(chrome.direct_up, 20LL);
        QCOMPARE(chrome.direct_down, 180LL);
    }
};

QTEST_MAIN(TestTrafficStats)
#include "test_traffic_stats.moc"
