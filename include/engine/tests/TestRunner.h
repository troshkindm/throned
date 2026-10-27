#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

#ifndef Q_MOC_RUN
#include <core/gen/libcore.pb.h>
#endif

#include "include/database/entities/Profile.h"

namespace Engine {
class EnginePolicy;
class TestObserver;

// Not a QObject: queued signals would reorder the synchronous progress path.
class TestRunner {
public:
    // Any thread; the running profile, or null.
    using RunningSnapshot = std::function<std::shared_ptr<Configs::Profile>()>;

    // policy and observer outlive the runner; a null observer gets a no-op stand-in (headless).
    TestRunner(EnginePolicy* policy, TestObserver* observer, RunningSnapshot runningSnapshot);

    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    // The run* calls return whether a session started; onFinished fires either way, on a refusal before they return.
    bool runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {}, bool interactive = true);

    // Waits out a running session instead of refusing it; returns at once and is safe from any thread.
    void queueUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive = true);

    bool runIpTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {}, bool interactive = true);

    void runUdpTests(const QList<int>& profileIDs);

    bool runSpeedTests(const QList<int>& profileIDs, bool testCurrent = false,
                       const std::function<void()>& onFinished = {}, bool interactive = true);

    // One HTTP probe per site through each selected profile. The verdict is what the
    // site answered, not whether it liked us: a geo-block replies and a filter does not.
    struct SiteTarget {
        QString name;
        QString url;
    };
    struct SiteVerdict {
        int status = 0; // HTTP code, 0 when nothing answered
        int latencyMs = 0;
        QString error;
        [[nodiscard]] bool reached() const { return status > 0; }
        [[nodiscard]] bool served() const { return status >= 200 && status < 400; }
    };
    struct SiteReport {
        QStringList sites;                  // column order
        QMap<int, QList<SiteVerdict>> rows; // profile id -> one verdict per site
        QMap<int, QString> errors;          // setup/core failures, not HTTP failures
        QList<int> skipped;                 // auto-selectors are not individual profiles
        QString error;                      // the run could not start or was cancelled
    };

    static QList<SiteTarget> configuredSites();

    // onFinished runs on the main thread.
    void runSiteTests(const QList<int>& profileIDs, const std::function<void(SiteReport)>& onFinished);

    // Walks one profile's path hop by hop, so a failure names the stage that broke instead of collapsing into a
    // timeout. onReport runs on the main thread with a line per hop.
    void runDiagnostics(int profileID, const std::function<void(const QString& title, const QString& report)>& onReport);

    void stop();

    bool isRunning();

    bool isTestingCurrent() const { return testingCurrent_.load(); }

    // Whether stop() was called since the latest session started.
    bool stopRequested() const { return stopRequested_.load(); }

private:
    enum class LatencyKind { Url,
                             Ip,
                             Udp };

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

    bool runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                         const std::function<void()>& onFinished, bool waitForSession = false, bool interactive = true);

    void runUrlProbe(const Target& target);

    void runIpProbe(const Target& target);

    void runUdpProbe(const Target& target);

    void runSiteProbe(const Target& target, const QList<SiteTarget>& sites, QMutex& reportMutex, SiteReport& report);

    void runSpeedProbe(const Target& target);

    void applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                        const QHash<QString, bool>* vpnConnected = nullptr);

    void applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res);

    void applyUdpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::UDPTestRes& res);

    QString contextName(int entID) const;

    // -1 while nothing runs.
    int runningProfileId() const;

    bool staleGen(quint64 gen) const { return sessionGen_.load() != gen; }

    void pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag,
                       qint64 curUp, qint64 curDown);

    void flushTrafficCredits();

    EnginePolicy* policy_;
    TestObserver* observer_;
    RunningSnapshot runningSnapshot_;

    // Held for a whole sweep, so it must never double as a per-batch latch.
    QMutex session_;
    // A poll thread is not joined, so a late tick must not drain the next batch.
    std::atomic<quint64> sessionGen_ = 0;
    std::atomic<bool> stopRequested_ = false;
    std::atomic<bool> testingCurrent_ = false;
    // The running session's flag: an unattended session never prompts for geo assets.
    std::atomic<bool> interactive_ = true;

    // Tests bypass the clash tracker, so their bytes are counted only here, diffed per tag.
    QMutex creditMu_;
    QHash<QString, QPair<qint64, qint64>> credited_;
    QHash<int, std::shared_ptr<Configs::Profile>> pendingTraffic_;
    QElapsedTimer trafficFlushTimer_;
    QMutex trafficFlushMu_;

    // Last, so it is destroyed first, while the members its probes use still exist.
    QThreadPool pool_;
};
} // namespace Engine
