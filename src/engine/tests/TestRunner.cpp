#include "include/engine/tests/TestRunner.h"

#include "include/api/RPC.h"
#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/engine/EnginePolicy.h"
#include "include/engine/EngineTr.h"
#include "include/engine/services/AssetUpdates.h"
#include "include/engine/services/ProfileService.h"
#include "include/engine/tests/TestObserver.h"
#include "include/global/Configs.hpp"
#include "include/global/CountryHelper.hpp"
#include "include/stats/traffic/TrafficStatsManager.hpp"

#include <QCoreApplication>
#include <QSemaphore>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include <limits>
#include <utility>

namespace Engine {
namespace {
constexpr int kTestRunnerBatchSize = 100;
constexpr int kTestRunnerLatencyPollIntervalMs = 200;
constexpr int kTestRunnerSpeedPollIntervalMs = 100;
constexpr int kTestRunnerPoolThreads = 10;
constexpr int kTestRunnerTrafficFlushIntervalMs = 1000;

QList<int> testRunnerWithoutAutoSelectors(const QList<int>& profileIDs) {
    const auto selectors = Configs::dataManager->profilesRepo->GetProfileIdsByType("autoselector");
    if (selectors.isEmpty()) return profileIDs;
    const QSet<int> skip(selectors.begin(), selectors.end());
    QList<int> filtered;
    filtered.reserve(profileIDs.size());
    for (int id: profileIDs) {
        if (!skip.contains(id)) filtered << id;
    }
    return filtered;
}

bool testRunnerIsTestAborted(const QString& error) {
    return error.contains("test aborted") || error.contains("context canceled");
}

bool testRunnerIsVpnProfile(const std::shared_ptr<Configs::Profile>& ent) {
    return ent != nullptr && (ent->type == "openvpn" || ent->type == "openconnect");
}

// Mirror the core's URLTestTimeout, TunnelStartupTimeout, TunnelHandshakeTimeout and normalizeConcurrency.
constexpr int kTestRunnerCoreDefaultTimeoutMs = 3000;
constexpr int kTestRunnerTunnelStartupMs = 5000;
constexpr int kTestRunnerTunnelHandshakeMs = 10000;
constexpr int kTestRunnerCoreMaxConcurrency = 100;
constexpr int kTestRunnerRpcSlackMs = 30000;

// Test/IPTest answer only once the whole batch is done, so the deadline covers its worst case.
int testRunnerBatchRpcTimeoutMs(qsizetype tagCount, int requestsPerTag) {
    const auto& settings = Configs::dataManager->settingsRepo;
    int concurrency = settings->test_concurrent;
    if (concurrency <= 0 || concurrency >= 500) concurrency = kTestRunnerCoreMaxConcurrency;
    const qint64 timeoutMs = settings->url_test_timeout_ms > 0 ? settings->url_test_timeout_ms : kTestRunnerCoreDefaultTimeoutMs;
    const qint64 rounds = (qMax<qsizetype>(tagCount, 1) + concurrency - 1) / concurrency;
    const qint64 total = rounds * (kTestRunnerTunnelHandshakeMs + requestsPerTag * timeoutMs + kTestRunnerTunnelStartupMs) + kTestRunnerRpcSlackMs;
    return static_cast<int>(qMin<qint64>(total, std::numeric_limits<int>::max()));
}

int testRunnerResolveEntID(const QMap<QString, int>& tag2entID, const std::string& tag, int fallback) {
    if (tag2entID.isEmpty()) return fallback;
    return tag2entID.value(QString::fromStdString(tag), -1);
}

// Target is deduced, not named: access control applies to naming a private type.
template<typename Req, typename Target>
void testRunnerFillCommonTestReq(Req& req, const Target& target) {
    for (const auto& tag: target.outboundTags) req.outbound_tags.push_back(tag.toStdString());
    req.config = target.coreConfig.toStdString();
    req.use_default_outbound = target.useDefaultOutbound;
    req.xray_config = target.xrayConfig.toStdString();
    req.need_xray = !target.xrayConfig.isEmpty();
    req.xray_outbound_dns_strategy = target.xrayDnsStrategy.toStdString();
    for (const auto& xc: target.xrayFullConfigs) req.xray_full_configs.push_back(xc.toStdString());
}

// Stopping does not join: the poll may itself sit in a 30s RPC and must not stall the batch.
class TestRunnerResultPoller {
public:
    TestRunnerResultPoller(std::function<void()> tick, int intervalMs)
        : stop_(std::make_shared<std::atomic<bool>>(false)) {
        runOnNewThread([stop = stop_, tick = std::move(tick), intervalMs] {
            while (!stop->load()) {
                QThread::msleep(intervalMs);
                if (stop->load()) break;
                tick();
            }
        });
    }

    ~TestRunnerResultPoller() { stop_->store(true); }

    TestRunnerResultPoller(const TestRunnerResultPoller&) = delete;
    TestRunnerResultPoller& operator=(const TestRunnerResultPoller&) = delete;

private:
    std::shared_ptr<std::atomic<bool>> stop_;
};

class TestRunnerNullObserver final : public TestObserver {
public:
    void testPanelSeeded(TestKind, int) override {}
    void testProgressed() override {}
    void testPanelsCleared() override {}
    void testViewChanged(bool) override {}
    void speedTestProgress(const QString&, const libcore::SpeedTestResult&) override {}
    void testedProfilesChanged(const QList<int>&) override {}
};

TestObserver* testRunnerNullObserver() {
    static TestRunnerNullObserver observer;
    return &observer;
}
} // namespace

TestRunner::TestRunner(EnginePolicy* policy, TestObserver* observer, RunningSnapshot runningSnapshot)
    : policy_(policy),
      observer_(observer != nullptr ? observer : testRunnerNullObserver()),
      runningSnapshot_(std::move(runningSnapshot)) {
    pool_.setMaxThreadCount(kTestRunnerPoolThreads);
}

bool TestRunner::isRunning() {
    if (!session_.tryLock()) return true;
    session_.unlock();
    return false;
}

void TestRunner::stop() {
    stopRequested_.store(true);
    bool ok;
    API::defaultClient->StopTests(&ok);

    if (!ok) {
        MW_show_log(MainWindowTr::tr("Failed to stop tests"));
    }
}

QString TestRunner::contextName(int entID) const {
    if (entID != -1) {
        if (auto e = Configs::dataManager->profilesRepo->GetProfile(entID)) return e->outbound->DisplayTypeAndName();
    }
    return MainWindowTr::tr("a tested profile");
}

int TestRunner::runningProfileId() const {
    const auto running = runningSnapshot_();
    return running ? running->id : -1;
}

void TestRunner::applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                                const QHash<QString, bool>* vpnConnected) {
    const auto error = QString::fromStdString(res.error.value());
    if (error.isEmpty()) {
        ent->SetLatency(res.latency_ms.value());
    } else if (testRunnerIsTestAborted(error)) {
        ent->SetLatency(0);
    } else if (vpnConnected != nullptr && testRunnerIsVpnProfile(ent) && vpnConnected->value(QString::fromStdString(res.outbound_tag.value()), false)) {
        ent->SetLatency(Configs::kLatencyConnectOnly);
    } else {
        ent->SetLatency(-1);
        MW_show_log(MainWindowTr::tr("[%1] test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
    }
    Configs::dataManager->profilesRepo->Save(ent);
}

void TestRunner::applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res) {
    const auto error = QString::fromStdString(res.error.value());
    if (error.isEmpty()) {
        ent->ip_out = QString::fromStdString(res.ip.value());
        ent->test_country = QString::fromStdString(res.country_code.value());
    } else {
        if (!testRunnerIsTestAborted(error)) {
            MW_show_log(MainWindowTr::tr("[%1] IP test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
        }
        ent->ip_out.clear();
        ent->test_country.clear();
    }
    Configs::dataManager->profilesRepo->Save(ent);
}

void TestRunner::applyUdpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::UDPTestRes& res) {
    // A failed probe is a result too: whoever ran the test wants to see the column
    // either way, so it is revealed here rather than only on success.
    Configs::dataManager->settingsRepo->show_udp_column = true;
    const auto error = QString::fromStdString(res.error.value());
    const int sent = res.sent.value();
    const int received = res.received.value();
    if (!error.isEmpty() || received == 0) {
        if (!error.isEmpty() && !testRunnerIsTestAborted(error)) {
            MW_show_log(MainWindowTr::tr("[%1] UDP test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
        }
        ent->udp_avg = -1;
        ent->udp_jitter = 0;
        ent->udp_loss = 100;
        ent->udp_error = error;
        return;
    }
    ent->udp_avg = res.avg_ms.value();
    ent->udp_jitter = res.jitter_ms.value();
    ent->udp_loss = sent > 0 ? (sent - received) * 100 / sent : 0;
    ent->udp_error.clear();
}

void TestRunner::runUdpProbe(const Target& target) {
    if (stopRequested_.load()) {
        MW_show_log(MainWindowTr::tr("Profile test aborted"));
        return;
    }

    libcore::UDPTestRequest req;
    testRunnerFillCommonTestReq(req, target);
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;
    req.target = Configs::dataManager->settingsRepo->udp_test_target.toStdString();

    bool rpcOK = false;
    QString coreError;
    libcore::UDPTestResp result;
    {
        TestRunnerResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID] {
            if (staleGen(gen)) return;
            bool ok = false;
            const auto resp = API::defaultClient->QueryUDPTest(&ok);
            // Checked again: a poll can sit in this RPC while its batch ends and the next
            // one zeroes the counter and reuses the positional tags.
            if (staleGen(gen)) return;
            if (!ok || resp.results.empty()) return;

            QList<int> updated;
            for (const auto& res: resp.results) {
                observer_->testProgressed();
                observer_->testViewChanged(false);
                const int entid = testRunnerResolveEntID(tag2entID, res.outbound_tag.value(), -1);
                if (entid == -1) continue;
                auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
                if (ent == nullptr) continue;
                applyUdpResult(ent, res);
                updated << entid;
            }
            if (updated.isEmpty()) return;
            observer_->testViewChanged(true);
            runOnUiThread([=, this] { observer_->testedProfilesChanged(updated); });
        },
                                      kTestRunnerLatencyPollIntervalMs);

        result = API::defaultClient->UDPTest(&rpcOK, req, &coreError);
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(target.entID), interactive_.load());
        return;
    }

    for (const auto& res: result.results) {
        const int entid = testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) continue;
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) continue;
        applyUdpResult(ent, res);
    }
}

void TestRunner::runUrlProbe(const Target& target) {
    if (stopRequested_.load()) {
        MW_show_log(MainWindowTr::tr("Profile test aborted"));
        return;
    }

    libcore::TestReq req;
    testRunnerFillCommonTestReq(req, target);
    req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;

    // The test box dies with the RPC, so the verdict has to be asked for up front.
    for (auto it = target.tag2entID.cbegin(); it != target.tag2entID.cend(); ++it) {
        if (testRunnerIsVpnProfile(Configs::dataManager->profilesRepo->GetProfile(it.value()))) {
            req.vpn_endpoint_tags.push_back(it.key().toStdString());
        }
    }

    bool rpcOK = false;
    QString coreError;
    libcore::TestResp result;
    {
        TestRunnerResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID] {
            if (staleGen(gen)) return;
            bool ok = false;
            const auto resp = API::defaultClient->QueryURLTest(&ok);
            // Checked again: this poll can sit in the RPC while its batch ends and the tags are reused.
            if (staleGen(gen)) return;
            if (!ok || resp.results.empty()) return;

            QList<int> updated;
            for (const auto& res: resp.results) {
                observer_->testProgressed();
                observer_->testViewChanged(false);
                const int entid = testRunnerResolveEntID(tag2entID, res.outbound_tag.value(), -1);
                if (entid == -1) continue;
                auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
                if (ent == nullptr) continue;
                applyUrlResult(ent, res);
                updated << entid;
            }
            if (updated.isEmpty()) return;
            observer_->testViewChanged(true);
            runOnUiThread([=, this] { observer_->testedProfilesChanged(updated); });
        },
                                      kTestRunnerLatencyPollIntervalMs);

        result = API::defaultClient->Test(&rpcOK, req, &coreError, testRunnerBatchRpcTimeoutMs(target.outboundTags.size(), 2));
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(target.entID), interactive_.load());
        return;
    }

    QHash<QString, bool> vpnConnected;
    for (const auto& st: result.vpn_status) {
        vpnConnected.insert(QString::fromStdString(st.tag.value()), st.connected.value());
    }

    for (const auto& res: result.results) {
        const int entid = testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindowTr::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindowTr::tr("Profile manager data is corrupted, try again."));
            continue;
        }
        applyUrlResult(ent, res, &vpnConnected);
    }
}

void TestRunner::runIpProbe(const Target& target) {
    if (stopRequested_.load()) {
        MW_show_log(MainWindowTr::tr("Profile test aborted"));
        return;
    }

    libcore::IPTestRequest req;
    testRunnerFillCommonTestReq(req, target);
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;

    bool rpcOK = false;
    QString coreError;
    libcore::IPTestResp result;
    {
        TestRunnerResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID] {
            if (staleGen(gen)) return;
            bool ok = false;
            const auto resp = API::defaultClient->QueryIPTest(&ok);
            if (staleGen(gen)) return;
            if (!ok || resp.results.empty()) return;

            QList<int> updated;
            for (const auto& res: resp.results) {
                observer_->testProgressed();
                observer_->testViewChanged(false);
                const int entid = testRunnerResolveEntID(tag2entID, res.outbound_tag.value(), -1);
                if (entid == -1) continue;
                auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
                if (ent == nullptr) continue;
                applyIpResult(ent, res);
                updated << entid;
            }
            if (updated.isEmpty()) return;
            observer_->testViewChanged(true);
            runOnUiThread([=, this] { observer_->testedProfilesChanged(updated); });
        },
                                      kTestRunnerLatencyPollIntervalMs);

        result = API::defaultClient->IPTest(&rpcOK, req, &coreError, testRunnerBatchRpcTimeoutMs(target.outboundTags.size(), 1));
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(target.entID), interactive_.load());
        return;
    }

    for (const auto& res: result.results) {
        const int entid = testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindowTr::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindowTr::tr("Profile manager data is corrupted, try again."));
            continue;
        }
        applyIpResult(ent, res);
    }
}

bool TestRunner::runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive) {
    return runLatencyGroup(LatencyKind::Url, profileIDs, onFinished, false, interactive);
}

void TestRunner::queueUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive) {
    runOnNewThread([=, this] { runLatencyGroup(LatencyKind::Url, profileIDs, onFinished, true, interactive); });
}

bool TestRunner::runIpTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive) {
    return runLatencyGroup(LatencyKind::Ip, profileIDs, onFinished, false, interactive);
}

void TestRunner::runUdpTests(const QList<int>& profileIDs) {
    runLatencyGroup(LatencyKind::Udp, profileIDs, {});
}

bool TestRunner::runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                                 const std::function<void()>& onFinished, bool waitForSession, bool interactive) {
    const bool isUrl = kind == LatencyKind::Url;
    const bool isUdp = kind == LatencyKind::Udp;
    const auto panelKind = isUrl ? TestKind::Latency : isUdp ? TestKind::Udp
                                                             : TestKind::Ip;
    // Must fire on every exit path — a caller may be blocked on it.
    const auto finish = [onFinished] { if (onFinished) onFinished(); };

    const auto profileIDs = testRunnerWithoutAutoSelectors(requestedIDs);
    if (profileIDs.isEmpty()) {
        finish();
        return false;
    }
    if (waitForSession) {
        session_.lock();
    } else if (!session_.tryLock()) {
        const auto text = isUrl
                              ? MainWindowTr::tr("The last url test did not exit completely, please wait. If it persists, please restart the program.")
                              : MainWindowTr::tr("The last test did not exit completely, please wait. If it persists, please restart the program.");
        // Auto-selector ranking calls in from a worker thread, where no widget may be created.
        if (interactive && QThread::currentThread() == QCoreApplication::instance()->thread())
            policy_->testSessionBusy(text);
        else
            MW_show_log(text);
        finish();
        return false;
    }
    sessionGen_.fetch_add(1);
    // Reset here, not on the worker: a stop() that lands before the worker runs must still count.
    stopRequested_.store(false);
    interactive_.store(interactive);

    runOnNewThread([this, profileIDs, panelKind, isUrl, isUdp, finish]() {
        observer_->testPanelSeeded(panelKind, profileIDs.size());
        observer_->testViewChanged(true);

        auto runBatch = [this, isUrl, isUdp](const QList<std::shared_ptr<Configs::Profile>>& profileSlice, const QList<int>& ids) {
            // Per batch, not per probe: a batch's probes drain each other's results, and tags restart each batch.
            sessionGen_.fetch_add(1);
            auto buildObject = Configs::BuildTestConfig(profileSlice);
            if (!buildObject->error.isEmpty()) {
                MW_show_log(MainWindowTr::tr("Failed to build test config for batch: ") + buildObject->error);
                return;
            }

            const int testCount = buildObject->fullConfigs.size() + (buildObject->outboundTags.empty() ? 0 : 1);
            if (testCount == 0) return;

            QSemaphore batchDone;
            const auto probe = [this, isUrl, isUdp, &batchDone](const Target& target) {
                pool_.start([this, isUrl, isUdp, target, &batchDone] {
                    const QSemaphoreReleaser releaser(batchDone);
                    if (isUrl)
                        runUrlProbe(target);
                    else if (isUdp)
                        runUdpProbe(target);
                    else
                        runIpProbe(target);
                });
            };

            for (const auto& entID: buildObject->fullConfigs.keys()) {
                Target target;
                target.coreConfig = buildObject->fullConfigs[entID];
                target.useDefaultOutbound = true;
                target.entID = entID;
                probe(target);
            }
            if (!buildObject->outboundTags.empty()) {
                Target target;
                target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
                target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, false) : "";
                target.xrayFullConfigs = buildObject->xrayFullConfigs;
                target.outboundTags = buildObject->outboundTags;
                target.tag2entID = buildObject->tag2entID;
                target.xrayDnsStrategy = buildObject->xrayDnsStrategy;
                probe(target);
            }
            batchDone.acquire(testCount);

            MW_show_log(isUrl ? "URL test for batch done." : isUdp ? "UDP test for batch done."
                                                                   : "IP test for batch done.");
            runOnUiThread([=, this] {
                observer_->testedProfilesChanged(ids);
            });
        };

        std::shared_ptr<Configs::Group> currentGroup;
        QHash<int, QList<int>> tested;
        for (int i = 0; i < profileIDs.length(); i += kTestRunnerBatchSize) {
            if (stopRequested_.load()) break;
            const auto profileIDsSlice = profileIDs.mid(i, kTestRunnerBatchSize);
            auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
            if (isUrl && !currentGroup && !profiles.isEmpty()) {
                currentGroup = Configs::dataManager->groupsRepo->GetGroup(profiles[0]->gid);
            }
            for (const auto& profile: profiles) tested[profile->gid] << profile->id;
            runBatch(profiles, profileIDsSlice);
        }
        // Once the run is over, so rows do not jump while results stream in.
        Configs::RepositionLater(tested, Configs::SortInput::TestResult);

        observer_->testPanelsCleared();
        observer_->testViewChanged(true);
        session_.unlock();
        finish();

        if (currentGroup != nullptr && currentGroup->auto_clear_unavailable) {
            MW_show_log("URL test finished, clearing unavailable profiles...");
            runOnUiThread([=] {
                ProfileService::RemoveUnavailable(profileIDs);
            });
        }
        MW_show_log(isUrl   ? MainWindowTr::tr("URL test finished!")
                    : isUdp ? MainWindowTr::tr("UDP test finished!")
                            : MainWindowTr::tr("IP test finished!"));
    });
    return true;
}

bool TestRunner::runSpeedTests(const QList<int>& requestedIDs, bool testCurrent,
                               const std::function<void()>& onFinished, bool interactive) {
    const auto finish = [onFinished] { if (onFinished) onFinished(); };
    // A live-connection test stays valid for a selector: it measures whichever member carries traffic.
    const auto profileIDs = testCurrent ? requestedIDs : testRunnerWithoutAutoSelectors(requestedIDs);
    if (profileIDs.isEmpty() && !testCurrent) {
        finish();
        return false;
    }
    if (!session_.tryLock()) {
        const auto text = MainWindowTr::tr("The last test did not finish completely, please wait. If it persists, please restart the program.");
        if (interactive)
            policy_->testSessionBusy(text);
        else
            MW_show_log(text);
        finish();
        return false;
    }
    sessionGen_.fetch_add(1);
    stopRequested_.store(false);
    interactive_.store(interactive);

    testingCurrent_.store(testCurrent);

    runOnNewThread([this, profileIDs, testCurrent, finish]() {
        // Fresh per-tag byte baselines for this speed-test session.
        flushTrafficCredits();
        {
            QMutexLocker lk(&creditMu_);
            credited_.clear();
            pendingTraffic_.clear();
            trafficFlushTimer_.start();
        }
        QHash<int, QList<int>> tested;
        if (!testCurrent) {
            observer_->testPanelSeeded(TestKind::Speed, profileIDs.size());
            observer_->testViewChanged(true);
            auto runBatch = [this](const QList<std::shared_ptr<Configs::Profile>>& profileSlice) {
                auto buildObject = Configs::BuildTestConfig(profileSlice);
                if (!buildObject->error.isEmpty()) {
                    MW_show_log(MainWindowTr::tr("Failed to build batch test config: ") + buildObject->error);
                    return;
                }

                for (auto it = buildObject->fullConfigs.cbegin(); it != buildObject->fullConfigs.cend(); ++it) {
                    Target target;
                    target.coreConfig = it.value();
                    target.useDefaultOutbound = true;
                    target.entID = it.key();
                    runSpeedProbe(target);
                }

                if (!buildObject->outboundTags.empty()) {
                    Target target;
                    target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
                    target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, true) : "";
                    target.xrayFullConfigs = buildObject->xrayFullConfigs;
                    target.outboundTags = buildObject->outboundTags;
                    target.tag2entID = buildObject->tag2entID;
                    target.xrayDnsStrategy = buildObject->xrayDnsStrategy;
                    runSpeedProbe(target);
                }
            };
            const int stepSize = Configs::dataManager->settingsRepo->speed_test_mode == Configs::TestConfig::COUNTRY ? kTestRunnerBatchSize : 1;
            for (int i = 0; i < profileIDs.length(); i += stepSize) {
                if (stopRequested_.load()) break;
                const auto profileIDsSlice = profileIDs.mid(i, stepSize);
                auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
                for (const auto& profile: profiles) tested[profile->gid] << profile->id;
                runBatch(profiles);
            }
        } else {
            observer_->testPanelSeeded(TestKind::Speed, 1);
            Target target;
            target.testCurrent = true;
            runSpeedProbe(target);
            testingCurrent_.store(false);
            if (const auto running = runningSnapshot_()) tested[running->gid] << running->id;
        }
        // A short or cancelled test may finish before the periodic flush.
        flushTrafficCredits();
        // A speed test also credits the traffic it moved.
        Configs::RepositionLater(tested, Configs::SortInput::TestResult);
        Configs::RepositionLater(tested, Configs::SortInput::Traffic);
        observer_->testPanelsCleared();
        observer_->testViewChanged(true);
        session_.unlock();
        finish();
        runOnUiThread([=, this] {
            observer_->testedProfilesChanged(profileIDs);
            MW_show_log(MainWindowTr::tr("Speedtest finished!"));
        });
    });
    return true;
}

void TestRunner::creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag, qint64 curUp, qint64 curDown) {
    if (profile == nullptr || tag.isEmpty()) return;
    if (Configs::dataManager->settingsRepo->disable_traffic_stats) return;
    bool shouldFlush = false;
    {
        QMutexLocker lk(&creditMu_);
        auto& base = credited_[tag];
        const qint64 dUp = curUp >= base.first ? curUp - base.first : curUp;
        const qint64 dDown = curDown >= base.second ? curDown - base.second : curDown;
        base = qMakePair(curUp, curDown);
        if (dUp <= 0 && dDown <= 0) return;

        Stats::trafficStatsManager->AddConfigDelta(profile->id, dUp, dDown);
        // The tag under test is the outbound these bytes took, so the speed test lands on
        // the proxied side of the split instead of poisoning the range as "not recorded".
        Stats::trafficStatsManager->AddAppDelta(Stats::SPEEDTEST_APP_NAME, "", tag, dUp, dDown);

        profile->traffic_uplink += dUp;
        profile->traffic_downlink += dDown;
        pendingTraffic_[profile->id] = profile;
        shouldFlush = !trafficFlushTimer_.isValid() || trafficFlushTimer_.elapsed() >= kTestRunnerTrafficFlushIntervalMs;
    }
    if (shouldFlush) flushTrafficCredits();
}

void TestRunner::flushTrafficCredits() {
    // Keep snapshots and writes ordered if the final result races a poll tick.
    QMutexLocker flushLock(&trafficFlushMu_);
    QList<std::shared_ptr<Configs::Profile>> profiles;
    {
        QMutexLocker creditLock(&creditMu_);
        profiles.reserve(pendingTraffic_.size());
        for (const auto& profile: std::as_const(pendingTraffic_)) profiles.append(profile);
        pendingTraffic_.clear();
        trafficFlushTimer_.restart();
    }
    if (!profiles.isEmpty()) Configs::dataManager->profilesRepo->SaveTrafficBatch(profiles);
}

void TestRunner::pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen) {
    bool ok = false;
    const auto res = API::defaultClient->QueryCurrentSpeedTests(&ok);
    if (staleGen(gen)) return;
    if (!ok || !res.is_running.value()) {
        return;
    }
    const libcore::SpeedTestResult result = res.result.value();
    const auto tag = QString::fromStdString(result.outbound_tag.value());
    // value(tag, -1), not operator[]: a const QMap yields 0 for a missing key.
    auto profile = testCurrent ? runningSnapshot_()
                               : Configs::dataManager->profilesRepo->GetProfile(tag2entID.value(tag, -1));
    if (profile == nullptr) {
        return;
    }
    creditTraffic(profile, tag, result.ul_bytes.value(), result.dl_bytes.value());
    runOnUiThread([this, profile, result] {
        observer_->speedTestProgress(profile->outbound->name, result);
        observer_->testViewChanged(false);

        if (result.error.value().empty() && !result.cancelled.value()) {
            if (!result.dl_speed.value().empty()) profile->dl_speed = QString::fromStdString(result.dl_speed.value());
            if (!result.ul_speed.value().empty()) profile->ul_speed = QString::fromStdString(result.ul_speed.value());
            if (profile->latency <= 0 && result.latency.value() > 0) profile->SetLatency(result.latency.value());
            if (!result.server_country.value().empty()) profile->test_country = CountryNameToCode(QString::fromStdString(result.server_country.value()));
            observer_->testedProfilesChanged({profile->id});
        }
    });
}

void TestRunner::pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen) {
    bool ok = false;
    const auto res = API::defaultClient->QueryCountryTestResults(&ok);
    if (staleGen(gen)) return;
    if (!ok || res.results.empty()) {
        return;
    }
    for (const auto& result: res.results) {
        observer_->testProgressed();
        observer_->testViewChanged(false);
        const auto tag = QString::fromStdString(result.outbound_tag.value());
        auto profile = testCurrent ? runningSnapshot_()
                                   : Configs::dataManager->profilesRepo->GetProfile(tag2entID.value(tag, -1));
        if (profile == nullptr) {
            continue;
        }
        runOnUiThread([this, profile, result] {
            if (result.error.value().empty() && !result.cancelled.value()) {
                if (profile->latency <= 0 && result.latency.value() > 0) profile->SetLatency(result.latency.value());
                if (!result.server_country.value().empty()) profile->test_country = CountryNameToCode(QString::fromStdString(result.server_country.value()));
                observer_->testedProfilesChanged({profile->id});
            }
        });
    }
    observer_->testViewChanged(true);
}

void TestRunner::runSpeedProbe(const Target& target) {
    if (stopRequested_.load()) {
        MW_show_log(MainWindowTr::tr("Profile speed test aborted"));
        return;
    }

    sessionGen_.fetch_add(1);

    const auto speedtestConf = Configs::dataManager->settingsRepo->speed_test_mode;
    libcore::SpeedTestRequest req;
    testRunnerFillCommonTestReq(req, target);
    req.test_download = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::DL;
    req.test_upload = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::UL;
    req.simple_download = speedtestConf == Configs::TestConfig::SIMPLEDL;
    req.simple_download_addr = Configs::dataManager->settingsRepo->simple_dl_url.toStdString();
    req.test_current = target.testCurrent;
    req.timeout_ms = Configs::dataManager->settingsRepo->speed_test_timeout_ms;
    req.only_country = speedtestConf == Configs::TestConfig::COUNTRY;
    req.country_concurrency = Configs::dataManager->settingsRepo->test_concurrent;

    if (speedtestConf != Configs::TestConfig::COUNTRY) {
        observer_->testProgressed();
        observer_->testViewChanged(false);
    }

    const int contextID = target.testCurrent ? runningProfileId() : target.entID;

    bool rpcOK = false;
    QString coreError;
    libcore::SpeedTestResponse result;
    {
        TestRunnerResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID, testCurrent = target.testCurrent, speedtestConf] {
            if (staleGen(gen)) return;
            if (speedtestConf == Configs::TestConfig::COUNTRY)
                pollCountryTest(tag2entID, testCurrent, gen);
            else
                pollSpeedTest(tag2entID, testCurrent, gen);
        },
                                      kTestRunnerSpeedPollIntervalMs);

        result = API::defaultClient->SpeedTest(&rpcOK, req, &coreError);
    }

    flushTrafficCredits();

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(contextID), interactive_.load());
        return;
    }

    for (const auto& res: result.results) {
        const int entid = target.testCurrent
                              ? runningProfileId()
                              : testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindowTr::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }

        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindowTr::tr("Profile manager data is corrupted, try again."));
            continue;
        }

        creditTraffic(ent, QString::fromStdString(res.outbound_tag.value()),
                      res.ul_bytes.value(), res.dl_bytes.value());

        if (res.cancelled.value()) continue;

        const auto error = QString::fromStdString(res.error.value());
        if (error.isEmpty()) {
            ent->dl_speed = QString::fromStdString(res.dl_speed.value());
            ent->ul_speed = QString::fromStdString(res.ul_speed.value());
            if (ent->latency <= 0 && res.latency.value() > 0) ent->SetLatency(res.latency.value());
            if (!res.server_country.value().empty()) ent->test_country = CountryNameToCode(QString::fromStdString(res.server_country.value()));
        } else {
            ent->dl_speed = "N/A";
            ent->ul_speed = "N/A";
            ent->SetLatency(-1);
            ent->test_country = "";
            MW_show_log(MainWindowTr::tr("[%1] speed test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
        }
        Configs::dataManager->profilesRepo->Save(ent);
    }
}

QList<TestRunner::SiteTarget> TestRunner::configuredSites() {
    QList<SiteTarget> sites;
    for (const auto& line: Configs::dataManager->settingsRepo->site_test_targets) {
        const auto entry = line.trimmed();
        if (entry.isEmpty() || entry.startsWith(QLatin1Char('#'))) continue;
        const int bar = entry.indexOf(QLatin1Char('|'));
        // Without a name the host stands in for one, so a bare URL still lists sensibly.
        const QString url = bar < 0 ? entry : entry.mid(bar + 1).trimmed();
        if (url.isEmpty()) continue;
        QString name = bar < 0 ? QUrl(url).host() : entry.left(bar).trimmed();
        if (name.isEmpty()) name = url;
        sites.append({name, url});
    }
    return sites;
}

void TestRunner::runSiteTests(const QList<int>& requestedIDs, const std::function<void(SiteReport)>& onFinished) {
    const auto sites = configuredSites();
    const auto profileIDs = testRunnerWithoutAutoSelectors(requestedIDs);
    SiteReport initialReport;
    for (const auto& site: sites) initialReport.sites << site.name;
    for (const int id: requestedIDs) {
        if (!profileIDs.contains(id)) initialReport.skipped << id;
    }
    if (sites.isEmpty() || profileIDs.isEmpty()) {
        if (sites.isEmpty()) initialReport.error = MainWindowTr::tr("No sites are configured to check. Add some in Basic Settings.");
        if (onFinished) onFinished(initialReport);
        return;
    }
    if (!session_.tryLock()) {
        policy_->testSessionBusy(MainWindowTr::tr("The last test did not exit completely, please wait. If it persists, please restart the program."));
        initialReport.error = MainWindowTr::tr("Another test is still running. Wait for it to finish.");
        if (onFinished) onFinished(initialReport);
        return;
    }
    sessionGen_.fetch_add(1);
    stopRequested_.store(false);
    interactive_.store(true);

    runOnNewThread([this, profileIDs, sites, onFinished, initialReport] {
        SiteReport report = initialReport;

        for (int i = 0; i < profileIDs.length(); i += kTestRunnerBatchSize) {
            if (stopRequested_.load()) break;
            const auto slice = profileIDs.mid(i, kTestRunnerBatchSize);
            const auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(slice);
            auto buildObject = Configs::BuildTestConfig(profiles);
            if (!buildObject->error.isEmpty()) {
                MW_show_log(MainWindowTr::tr("Failed to build test config for batch: ") + buildObject->error);
                for (const int id: slice) report.errors.insert(id, buildObject->error);
                continue;
            }

            // Same split the other tests use: a full config is its own box, everything
            // else shares one and is told apart by outbound tag.
            QList<Target> targets;
            for (const auto& entID: buildObject->fullConfigs.keys()) {
                Target target;
                target.coreConfig = buildObject->fullConfigs[entID];
                target.useDefaultOutbound = true;
                target.entID = entID;
                targets << target;
            }
            if (!buildObject->outboundTags.empty()) {
                Target target;
                target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
                target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, false) : "";
                target.xrayFullConfigs = buildObject->xrayFullConfigs;
                target.outboundTags = buildObject->outboundTags;
                target.tag2entID = buildObject->tag2entID;
                target.xrayDnsStrategy = buildObject->xrayDnsStrategy;
                targets << target;
            }

            QSemaphore batchDone;
            QMutex reportMutex;
            for (const auto& target: targets) {
                pool_.start([this, target, sites, &batchDone, &reportMutex, &report] {
                    const QSemaphoreReleaser releaser(batchDone);
                    runSiteProbe(target, sites, reportMutex, report);
                });
            }
            batchDone.acquire(targets.size());
        }

        if (stopRequested_.load()) report.error = MainWindowTr::tr("Test cancelled.");
        session_.unlock();
        runOnUiThread([onFinished, report] { if (onFinished) onFinished(report); });
    });
}

void TestRunner::runSiteProbe(const Target& target, const QList<SiteTarget>& sites, QMutex& reportMutex, SiteReport& report) {
    if (stopRequested_.load()) return;

    libcore::SiteTestRequest req;
    testRunnerFillCommonTestReq(req, target);
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->site_test_timeout_ms;
    for (const auto& site: sites) {
        libcore::SiteTarget entry;
        entry.name = site.name.toStdString();
        entry.url = site.url.toStdString();
        req.targets.push_back(entry);
    }

    bool rpcOK = false;
    QString coreError;
    const auto result = API::defaultClient->SiteTest(&rpcOK, req, &coreError);
    if (!rpcOK) {
        AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(target.entID), interactive_.load());
        if (coreError.isEmpty()) coreError = MainWindowTr::tr("The core did not return site test results.");
        MW_show_log(MainWindowTr::tr("Site test failed: %1").arg(coreError));
        const QMutexLocker lock(&reportMutex);
        if (target.tag2entID.isEmpty())
            report.errors.insert(target.entID, coreError);
        else
            for (const int id: target.tag2entID) report.errors.insert(id, coreError);
        return;
    }

    for (const auto& res: result.results) {
        const int entid = testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) continue;
        if (!res.error.value().empty()) {
            const QMutexLocker lock(&reportMutex);
            report.errors.insert(entid, QString::fromStdString(res.error.value()));
            continue;
        }
        if (static_cast<int>(res.probes.size()) != sites.size()) {
            const QMutexLocker lock(&reportMutex);
            report.errors.insert(entid, MainWindowTr::tr("The core returned an incomplete site test result."));
            continue;
        }
        QList<SiteVerdict> row;
        row.reserve(sites.size());
        // Positional, not by name: the core fills one slot per requested site, and two
        // sites the user happened to name the same would collide in a lookup by name.
        for (int i = 0; i < sites.size(); i++) {
            const auto& probe = res.probes[i];
            row << SiteVerdict{probe.status.value(), probe.latency_ms.value(), QString::fromStdString(probe.error.value())};
        }

        const QMutexLocker lock(&reportMutex);
        report.rows.insert(entid, row);
    }
}

void TestRunner::runDiagnostics(int profileID, const std::function<void(const QString& title, const QString& report)>& onReport) {
    auto ent = Configs::dataManager->profilesRepo->GetProfile(profileID);
    if (ent == nullptr) return;
    const auto title = ent->outbound->DisplayTypeAndName();

    runOnNewThread([profileID, title, onReport] {
        auto ent = Configs::dataManager->profilesRepo->GetProfile(profileID);
        if (ent == nullptr) return;
        auto buildObject = Configs::BuildTestConfig({ent});
        if (!buildObject->error.isEmpty()) {
            MW_show_log(MainWindowTr::tr("Failed to build diagnostic config: ") + buildObject->error);
            return;
        }

        Target target;
        if (!buildObject->fullConfigs.isEmpty()) {
            target.coreConfig = buildObject->fullConfigs.first();
            target.useDefaultOutbound = true;
        } else {
            target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
            target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, false) : "";
            target.xrayFullConfigs = buildObject->xrayFullConfigs;
            target.outboundTags = buildObject->outboundTags;
        }
        target.entID = profileID;

        libcore::TestReq req;
        testRunnerFillCommonTestReq(req, target);
        req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();
        req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;

        bool rpcOK = false;
        QString coreError;
        const auto result = API::defaultClient->Diagnose(&rpcOK, req, &coreError);
        if (!rpcOK) {
            MW_show_log(MainWindowTr::tr("[%1] diagnostics failed: %2").arg(title, coreError));
            return;
        }

        QStringList lines;
        for (const auto& step: result.results) {
            const auto label = QString::fromStdString(step.outbound_tag.value());
            const auto error = QString::fromStdString(step.error.value());
            lines << QString("%1  %2  %3 ms%4")
                         .arg(error.isEmpty() ? "OK  " : "FAIL", -4)
                         .arg(label, -46)
                         .arg(step.latency_ms.value(), 5)
                         .arg(error.isEmpty() ? QString() : "\n      " + error);
        }
        if (lines.isEmpty()) lines << MainWindowTr::tr("The core returned no steps.");

        const auto report = lines.join("\n");
        MW_show_log(MainWindowTr::tr("[%1] diagnostics:").arg(title) + "\n" + report);
        if (onReport) runOnUiThread([onReport, title, report] { onReport(title, report); });
    });
}
} // namespace Engine
