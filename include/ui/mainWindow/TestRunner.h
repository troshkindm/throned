#pragma once

#include <QHash>
#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

#ifndef Q_MOC_RUN
#include <core/server/gen/libcore.pb.h>
#endif

#include "include/database/entities/Profile.h"

class MainWindow;

// Not a QObject: queued signals would reorder the synchronous progress path.
class TestRunner {
public:
    explicit TestRunner(MainWindow* mw) : mw_(mw) {}

    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    // `onFinished` fires on every exit path, so a caller may block on it.
    void runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {});

    void runIpTests(const QList<int>& profileIDs);

    // TCP latency says nothing about UDP: carriers throttle it separately, which
    // is what kills QUIC-based protocols while every URL test still reports green.
    void runUdpTests(const QList<int>& profileIDs);

    void runSpeedTests(const QList<int>& profileIDs, bool testCurrent = false);

    // One HTTP probe per site through each selected profile. The verdict is what the
    // site answered, not whether it liked us: a geo-block replies and a filter does not.
    struct SiteTarget { QString name; QString url; };
    struct SiteVerdict {
        int status = 0;     // HTTP code, 0 when nothing answered
        int latencyMs = 0;
        QString error;
        [[nodiscard]] bool reached() const { return status > 0; }
        [[nodiscard]] bool served() const { return status >= 200 && status < 400; }
    };
    struct SiteReport {
        QStringList sites;                    // column order
        QMap<int, QList<SiteVerdict>> rows;   // profile id -> one verdict per site
        QMap<int, QString> errors;            // setup/core failures, not HTTP failures
        QList<int> skipped;                   // auto-selectors are not individual profiles
        QString error;                       // the run could not start or was cancelled
    };

    static QList<SiteTarget> configuredSites();

    void runSiteTests(const QList<int>& profileIDs, const std::function<void(SiteReport)>& onFinished);

    // Walks one profile's path hop by hop and hands back a line per hop, so a
    // failure names the stage that broke instead of collapsing into a timeout.
    void runDiagnostics(int profileID);

    void stop();

    bool isRunning();

    bool isTestingCurrent() const { return testingCurrent_.load(); }

private:
    enum class LatencyKind { Url, Ip, Udp };

    struct Target {
        QString coreConfig;
        QString xrayConfig;
        QStringList xrayFullConfigs;
        QStringList outboundTags;
        QMap<QString, int> tag2entID;
        QString xrayDnsStrategy;
        int entID = -1;
        // Not derivable from an empty outboundTags: a test-current run leaves both empty but wants "proxy".
        bool useDefaultOutbound = false;
        bool testCurrent = false;
    };

    void runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                         const std::function<void()>& onFinished);

    void runUrlProbe(const Target& target);

    void runIpProbe(const Target& target);

    void runUdpProbe(const Target& target);

    void runSpeedProbe(const Target& target);

    void runSiteProbe(const Target& target, const QList<SiteTarget>& sites,
                      class QMutex& reportMutex, SiteReport& report);

    // `vpnConnected` is empty on the progress poll; only the final pass has verdicts.
    void applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                        const QHash<QString, bool>* vpnConnected = nullptr);

    void applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res);

    void applyUdpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::UDPTestRes& res);

    QString contextName(int entID) const;

    // A poll's batch can end while its query is in flight, so `gen` is re-checked
    // after every query and the result dropped if the batch it belongs to is gone.
    bool staleGen(quint64 gen) const { return sessionGen_.load() != gen; }

    void pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag,
                       qint64 curUp, qint64 curDown);

    void flushTrafficCredits();

    MainWindow* mw_;

    // Held for a whole sweep, so it must never double as a per-batch latch.
    QMutex session_;
    // A poll thread is not joined, so a late tick must not drain the next batch.
    std::atomic<quint64> sessionGen_ = 0;
    std::atomic<bool> stopRequested_ = false;
    std::atomic<bool> testingCurrent_ = false;

    // Tests bypass the clash tracker, so their bytes are counted only here, diffed per tag.
    QMutex creditMu_;
    QHash<QString, QPair<qint64, qint64>> credited_;
    QHash<int, std::shared_ptr<Configs::Profile>> pendingTraffic_;
    QElapsedTimer trafficFlushTimer_;
    QMutex trafficFlushMu_;
};
