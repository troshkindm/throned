#include "include/ui/stats/diagnostics_window.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowChrome.h"
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMenu>
#include <QLocale>
#include <QPlainTextEdit>
#include <QTest>

ThemeManager *themeManager() { static ThemeManager manager; return &manager; }
ThronedThemeColors ThemeManager::Colors(const QString &) const { return {}; }
const ThronedSkin *ThemeManager::Skin(const QString &) const { return nullptr; }
void ThemeManager::RegisterStyle(QWidget *, const QString &) const {}
namespace ThronedChrome {
ThronedTitleBar *install(QWidget *window, const QString &context) { return new ThronedTitleBar(context, window); }
}
// Pulled in by the traffic chart's tooltip; stubbed rather than linking all of Utils.
QString ReadableSize(const qint64 &size) { return QLocale().formattedDataSize(size); }

class TestDiagnostics : public QObject {
    Q_OBJECT
    static libcore::ConnectionMetaData connection(const char *id, const char *network = "tcp") {
        libcore::ConnectionMetaData c;
        c.id = id; c.network = network; c.process = "demo.exe"; c.process_path = "C:/demo.exe";
        c.domain = "example.org"; c.dest = "203.0.113.10:443"; c.outbound = "chosen-proxy";
        c.matched_rule = "process_name=demo.exe => route(chosen-proxy)";
        c.upload = 42; c.download = 0;
        return c;
    }
private slots:
    void invalidInputNeverRequestsCore() {
        DiagnosticsWindow window;
        int calls = 0;
        connect(&window, &DiagnosticsWindow::routeRequested, &window, [&] { ++calls; });
        connect(&window, &DiagnosticsWindow::siteRequested, &window, [&] { ++calls; });
        auto *input = window.findChild<QLineEdit *>("diagnosticAddress");
        for (const auto &url : {"", "file:///etc/hosts", "https://user:secret@example.org", "https://example.org/#fragment"}) {
            input->setText(url);
            window.findChild<QPushButton *>("diagnosticCheck")->click();
        }
        QCOMPARE(calls, 0);
    }
    void coreFailureEndsBusyState() {
        DiagnosticsWindow window;
        auto *input = window.findChild<QLineEdit *>("diagnosticAddress");
        auto *check = window.findChild<QPushButton *>("diagnosticCheck");
        input->setText("example.org"); check->click();
        QVERIFY(!check->isEnabled());
        window.applySiteResult({}, "unknown method: DiagnoseSite");
        QVERIFY(check->isEnabled());
        QCOMPARE(window.findChild<QLabel *>("diagnosticVerdictTitle")->text(), QString("Could not run the check"));
    }
    void addressCheckPinsTheProcessAndProbesTheRuleOutbound() {
        DiagnosticsWindow window;
        int routeCalls = 0, siteCalls = 0;
        libcore::PreviewRouteRequest routeRequest;
        libcore::DiagnoseSiteRequest siteRequest;
        connect(&window, &DiagnosticsWindow::routeRequested, &window, [&](const auto &r) { routeRequest = r; ++routeCalls; });
        connect(&window, &DiagnosticsWindow::siteRequested, &window, [&](const auto &r) { siteRequest = r; ++siteCalls; });
        window.showApplication("C:/demo.exe");
        libcore::QueryConnectionsResp snapshot;
        snapshot.active.push_back(connection("1"));
        window.applyConnections(snapshot);
        window.findChild<QPushButton *>("diagnosticConnectionCheck")->click();
        QCOMPARE(routeCalls, 0);
        QCOMPARE(window.findChild<QLineEdit *>("diagnosticAddress")->text(), QString("https://example.org"));
        window.findChild<QPushButton *>("diagnosticCheck")->click();
        QCOMPARE(routeCalls, 1);
        QCOMPARE(siteCalls, 0); // the probe waits for the route to name an outbound
        QCOMPARE(routeRequest.process_path.value(), std::string("C:/demo.exe"));
        libcore::PreviewRouteResponse route;
        route.outbound_tag = "rule-proxy";
        route.matched_rule = "process_name=demo.exe => route(rule-proxy)";
        route.action = "route";
        window.applyRoutePreview(route);
        QCOMPARE(siteCalls, 1);
        QCOMPARE(siteRequest.outbound_tag.value(), std::string("rule-proxy"));
    }
    void comparisonRunsEachAddressExactlyOnce() {
        DiagnosticsWindow window;
        int routeCalls = 0, siteCalls = 0;
        connect(&window, &DiagnosticsWindow::routeRequested, &window, [&](const auto &) { ++routeCalls; });
        connect(&window, &DiagnosticsWindow::siteRequested, &window, [&](const auto &) { ++siteCalls; });
        window.findChild<QLineEdit *>("diagnosticAddress")->setText("first.example");
        window.findChild<QLineEdit *>("diagnosticCompare")->setText("second.example");
        window.findChild<QPushButton *>("diagnosticCompareToggle")->setChecked(true);
        window.findChild<QPushButton *>("diagnosticCheck")->click();

        libcore::PreviewRouteResponse route;
        route.outbound_tag = "proxy";
        route.action = "route";
        libcore::DiagnoseSiteResponse site;
        site.outbound_tag = "proxy";
        site.connect_ms = 1;
        site.tls_ms = 1;
        site.http_ms = 1;
        site.status = 200;

        QCOMPARE(routeCalls, 1);
        window.applyRoutePreview(route);
        QCOMPARE(siteCalls, 1);
        window.applySiteResult(site);
        QTRY_COMPARE(routeCalls, 2);
        window.applyRoutePreview(route);
        QCOMPARE(siteCalls, 2);
        window.applySiteResult(site);
        QCoreApplication::processEvents();
        QCOMPARE(routeCalls, 2);

        window.findChildren<QPushButton *>("diagnosticRail").value(4)->click();
        const auto report = window.findChild<QPlainTextEdit *>("diagnosticReportPreview")->toPlainText();
        QVERIFY(report.contains("first.example"));
        QVERIFY(report.contains("second.example"));
    }
    void aRejectingRuleNeverReachesTheNetwork() {
        DiagnosticsWindow window;
        int siteCalls = 0;
        connect(&window, &DiagnosticsWindow::siteRequested, &window, [&] { ++siteCalls; });
        window.findChild<QLineEdit *>("diagnosticAddress")->setText("ads.example");
        window.findChild<QPushButton *>("diagnosticCheck")->click();
        libcore::PreviewRouteResponse route;
        route.action = "reject";
        route.matched_rule = "geosite=ads => reject()";
        window.applyRoutePreview(route);
        QCOMPARE(siteCalls, 0);
        QVERIFY(window.findChild<QPushButton *>("diagnosticCheck")->isEnabled());
        QCOMPARE(window.findChild<QLabel *>("diagnosticVerdictTitle")->text(),
                 QString("A routing rule blocks this address"));
    }
    void aLateRoutePreviewAfterTheCheckEndedIsIgnored() {
        DiagnosticsWindow window;
        int siteCalls = 0;
        connect(&window, &DiagnosticsWindow::siteRequested, &window, [&] { ++siteCalls; });
        window.findChild<QLineEdit *>("diagnosticAddress")->setText("example.org");
        window.findChild<QPushButton *>("diagnosticCheck")->click();
        window.applySiteResult({}, "core stopped");
        libcore::PreviewRouteResponse route;
        route.outbound_tag = "proxy";
        route.action = "route";
        window.applyRoutePreview(route);
        QCOMPARE(siteCalls, 0);
    }
    void udpCannotLaunchHTTPCheck() {
        DiagnosticsWindow window;
        libcore::QueryConnectionsResp snapshot;
        snapshot.active.push_back(connection("1", "udp"));
        window.applyConnections(snapshot);
        QVERIFY(!window.findChild<QPushButton *>("diagnosticConnectionCheck")->isEnabled());
        const auto rule = window.findChild<QLabel *>("diagnosticRuleDetail")->text();
        QVERIFY(rule.startsWith(QString::fromStdString(snapshot.active[0].matched_rule.value())));
        QVERIFY(rule.contains("Chosen by the program that opened the connection."));
    }
    void connectionsToOneDestinationCollapseIntoOneRow() {
        DiagnosticsWindow window;
        auto *tree = window.findChild<QTreeWidget *>("diagnosticConnections");
        libcore::QueryConnectionsResp snapshot;
        for (const char *id : {"1", "2", "3"}) snapshot.active.push_back(connection(id));
        auto other = connection("4");
        other.domain = "elsewhere.example";
        other.upload = 10; other.download = 20;
        snapshot.active.push_back(other);
        window.applyConnections(snapshot);
        QCOMPARE(tree->topLevelItemCount(), 2);
        // Nothing came back on the three-connection group, so it sorts above the healthy one.
        QVERIFY(tree->topLevelItem(0)->text(0).contains("example.org"));
        QVERIFY(tree->topLevelItem(0)->text(0).contains(QString::fromUtf8("×3")));
        QVERIFY(tree->topLevelItem(1)->text(0).contains("elsewhere.example"));
    }
    void aPollReusesRowsInsteadOfRebuildingThem() {
        DiagnosticsWindow window;
        auto *tree = window.findChild<QTreeWidget *>("diagnosticConnections");
        libcore::QueryConnectionsResp snapshot;
        snapshot.active.push_back(connection("1"));
        window.applyConnections(snapshot);
        auto *first = tree->topLevelItem(0);
        auto grown = connection("2");
        grown.download = 4096;
        snapshot.active.push_back(grown);
        window.applyConnections(snapshot);
        // Same widget instance: a clear()/rebuild would drop the selection and the scroll.
        QCOMPARE(tree->topLevelItem(0), first);
        QVERIFY(tree->topLevelItem(0)->text(1).contains(QLocale().formattedDataSize(4096)));
    }
    void splitDNSIsReportedAsAProblemNotAsAFailure() {
        DiagnosticsWindow window;
        libcore::HealthResponse health;
        health.dns_domain = "example.org";
        health.dns_compared = true;
        health.dns_agrees = false;
        health.dns_core = {"172.64.155.209"};
        health.dns_system = {"104.18.32.47"};
        health.clock_known = true;
        health.clock_skew_ms = 600000;
        window.applyHealth(health);
        const auto rows = window.findChildren<QWidget *>("diagnosticHealthRow");
        QCOMPARE(rows.size(), 7);
        // The DNS row offers a way out; the clock row states the consequence and does not.
        QVERIFY(!rows[4]->findChild<QPushButton *>("diagnosticHealthAction")->isHidden());
        const auto labels = rows[6]->findChildren<QLabel *>();
        bool mentionsTLS = false;
        for (const auto *text : labels) mentionsTLS = mentionsTLS || text->text().contains("TLS will fail");
        QVERIFY(mentionsTLS);
    }
    void fakeDNSDifferenceIsExpected() {
        DiagnosticsWindow window;
        DiagnosticsWindow::LocalState state;
        state.fakeDns = true;
        window.applyLocalState(state);
        libcore::HealthResponse health;
        health.dns_domain = "example.org";
        health.dns_compared = true;
        health.dns_agrees = false;
        health.dns_core = {"104.18.32.47"};
        health.dns_system = {"198.18.0.1"};
        window.applyHealth(health);
        const auto rows = window.findChildren<QWidget *>("diagnosticHealthRow");
        QVERIFY(rows[4]->findChild<QPushButton *>("diagnosticHealthAction")->isHidden());
        bool explainsFakeIP = false;
        for (const auto *label : rows[4]->findChildren<QLabel *>())
            explainsFakeIP = explainsFakeIP || label->text().contains("FakeIP");
        QVERIFY(explainsFakeIP);
    }
    void comparisonMatrixUsesDNS_TLS_AndHTTPColumns() {
        DiagnosticsWindow window;
        libcore::HealthResponse health;
        health.outbounds = {"proxy", "direct"};
        window.applyHealth(health);
        window.findChild<QLineEdit *>("diagnosticAddress")->setText("example.org");
        window.findChild<QPushButton *>("diagnosticCheck")->click();
        libcore::PreviewRouteResponse route;
        route.outbound_tag = "proxy";
        route.action = "route";
        window.applyRoutePreview(route);
        libcore::DiagnoseSiteResponse site;
        site.outbound_tag = "proxy";
        site.connect_ms = 1;
        site.tls_ms = 27;
        site.http_ms = 3;
        site.status = 403;
        site.dns_compared = true;
        site.dns_agrees = true;
        window.applySiteResult(site);
        for (auto *action : window.findChildren<QPushButton *>("diagnosticVerdictAction"))
            if (action->property("target").toString() == QStringLiteral("matrix")) action->click();
        auto *matrix = window.findChild<QTreeWidget *>("diagnosticMatrix");
        QVERIFY(matrix->topLevelItemCount() >= 2);
        QCOMPARE(matrix->topLevelItem(0)->text(1), QString::fromUtf8("✓"));
        QCOMPARE(matrix->topLevelItem(0)->text(2), QString("27 ms"));
        QCOMPARE(matrix->topLevelItem(0)->text(3), QString("403"));
    }
    void aCutHandshakeOffersTheBypassRatherThanBlamingTheCertificate() {
        DiagnosticsWindow window;
        window.findChild<QLineEdit *>("diagnosticAddress")->setText("chatgpt.com");
        window.findChild<QPushButton *>("diagnosticCheck")->click();
        libcore::PreviewRouteResponse route;
        route.outbound_tag = "direct";
        route.action = "route";
        window.applyRoutePreview(route);
        libcore::DiagnoseSiteResponse result;
        result.outbound_tag = "direct";
        result.connect_ms = 22;
        result.tls_ms = 12;
        result.error_stage = "tls";
        result.error = "connection reset by peer";
        result.tls_cut = true;
        window.applySiteResult(result);
        QCOMPARE(window.findChild<QLabel *>("diagnosticVerdictTitle")->text(),
                 QString("The connection is cut right after the site name"));
        const auto actions = window.findChildren<QPushButton *>("diagnosticVerdictAction");
        QVERIFY(!actions.isEmpty());
        QCOMPARE(actions[0]->property("target").toString(), QString("dpi"));
    }
    void theReportMasksTheProfileAndTheVisibleAddress() {
        DiagnosticsWindow window;
        DiagnosticsWindow::LocalState state;
        state.coreRunning = true;
        state.profileName = "Secret Node";
        state.environmentReport = "Throned 1.3.8";
        window.applyLocalState(state);
        libcore::HealthResponse health;
        health.external_ip = "198.51.100.24";
        window.applyHealth(health);
        auto *preview = window.findChild<QPlainTextEdit *>("diagnosticReportPreview");
        window.findChildren<QPushButton *>("diagnosticRail").value(3)->click();
        const auto masked = preview->toPlainText();
        QVERIFY(!masked.contains("Secret Node"));
        QVERIFY(!masked.contains("198.51.100.24"));
        for (auto *box : window.findChildren<QCheckBox *>())
            if (box->text().contains("Hide personal")) box->setChecked(false);
        QVERIFY(window.findChild<QPlainTextEdit *>("diagnosticReportPreview")->toPlainText().contains("Secret Node"));
    }
    void connectionsWithoutAProcessStaySeparateAndExplainThemselves() {
        DiagnosticsWindow window;
        auto *tree = window.findChild<QTreeWidget *>("diagnosticConnections");
        libcore::QueryConnectionsResp snapshot;
        // Two LAN devices reaching the same host: no process either way, so only the
        // client address tells them apart. They must not collapse into one row.
        for (const auto *pair : {"1|192.168.1.42:5510", "2|192.168.1.99:4410"}) {
            auto c = connection(QString(pair).section('|', 0, 0).toUtf8().constData());
            c.process = ""; c.process_path = "";
            c.source = QString(pair).section('|', 1).toStdString();
            snapshot.active.push_back(c);
        }
        window.applyConnections(snapshot);
        QCOMPARE(tree->topLevelItemCount(), 2);
        QVERIFY(tree->topLevelItem(0)->text(0).contains("192.168.1."));
        // Nothing in the snapshot has a process, so the window says why rather than
        // leaving the user to conclude the capture is broken.
        bool explained = false;
        for (const auto *text : window.findChildren<QLabel *>())
            explained = explained || text->text().contains("no rules that match on a program");
        QVERIFY(explained);
        // And they can be isolated from the applications that do report a name.
        auto *filter = window.findChild<QComboBox *>("diagnosticApps");
        QVERIFY(filter->findData(QStringLiteral("?no-process")) > 0);
    }
    void processWarningFollowsTheSelectedApplicationFilter() {
        DiagnosticsWindow window;
        libcore::QueryConnectionsResp snapshot;
        snapshot.active.push_back(connection("with-process"));
        auto unknown = connection("without-process");
        unknown.process = "";
        unknown.process_path = "";
        unknown.source = "192.168.1.42:5510";
        snapshot.active.push_back(unknown);
        window.applyConnections(snapshot);

        auto *filter = window.findChild<QComboBox *>("diagnosticApps");
        filter->setCurrentIndex(filter->findData(QStringLiteral("C:/demo.exe")));
        const auto status = window.findChild<QLabel *>("diagnosticCaptureStatus")->text();
        QVERIFY(!status.contains("No application is reported for any connection"));
        QVERIFY(!status.contains("connection(s) report no application"));
    }
    void aHelperProcessOffersAWholeApplicationRule() {
        DiagnosticsWindow window;
        // The parent chain is resolved from the live process table, so the test uses
        // this test's own process: its executable is a real child of a real parent.
        libcore::QueryConnectionsResp snapshot;
        auto c = connection("1");
        c.process_id = QCoreApplication::applicationPid();
        c.process_path = QCoreApplication::applicationFilePath().toStdString();
        c.process = QFileInfo(QCoreApplication::applicationFilePath()).fileName().toStdString();
        snapshot.active.push_back(c);
        window.applyConnections(snapshot);
        auto *rule = window.findChild<QPushButton *>("diagnosticAddRule");
        QVERIFY(rule->isEnabled());
        // The per-process entries are always offered; the family entry only when a
        // different parent was actually found, which depends on how the test was run.
        bool hasProcessEntry = false;
        for (const auto *action : rule->menu()->actions())
            hasProcessEntry = hasProcessEntry || action->text().contains("This process");
        QVERIFY(hasProcessEntry);
    }
    void wholeApplicationDoesNotCaptureSiblingAppsFromTheDesktopShell() {
        DiagnosticsWindow window;
        window.ancestryTakenAt = QDateTime::currentMSecsSinceEpoch();
        window.ancestry.insert(100, {4, QStringLiteral("explorer.exe")});
        window.ancestry.insert(200, {100, QStringLiteral("chrome.exe")});
        window.ancestry.insert(201, {200, QStringLiteral("chrome-helper.exe")});
        window.ancestry.insert(300, {100, QStringLiteral("Telegram.exe")});

        libcore::QueryConnectionsResp snapshot;
        auto add = [&](const char *id, quint32 pid, const char *name) {
            auto c = connection(id);
            c.process_id = pid;
            c.process = name;
            c.process_path = (QStringLiteral("C:/apps/") + QString::fromUtf8(name)).toStdString();
            c.domain = (QString::fromUtf8(name) + QStringLiteral(".example")).toStdString();
            snapshot.active.push_back(c);
        };
        add("chrome", 200, "chrome.exe");
        add("helper", 201, "chrome-helper.exe");
        add("telegram", 300, "Telegram.exe");
        window.applyConnections(snapshot);

        const DiagnosticsWindow::ConnectionGroup *chrome = nullptr;
        for (const auto &group : window.groups)
            if (group.process == QStringLiteral("chrome.exe")) chrome = &group;
        QVERIFY(chrome != nullptr);
        window.rebuildRuleMenu(*chrome);

        QStringList emitted;
        connect(&window, &DiagnosticsWindow::rulesRequested, &window,
                [&](const QStringList &entries, int) { emitted = entries; });
        for (auto *action : window.addRule->menu()->actions())
            if (action->menu() != nullptr && action->text().contains("The whole application")) {
                QVERIFY(action->text().contains(QStringLiteral("chrome.exe")));
                action->menu()->actions().first()->trigger();
            }

        QCOMPARE(emitted, QStringList({QStringLiteral("processName:chrome.exe"),
                                       QStringLiteral("processName:chrome-helper.exe")}));
        QVERIFY(!emitted.contains(QStringLiteral("processName:Telegram.exe")));
        QVERIFY(!emitted.contains(QStringLiteral("processName:explorer.exe")));
    }
    void ruleEntriesMatchTheConnectionsTableFormat() {
        DiagnosticsWindow window;
        QStringList emitted;
        connect(&window, &DiagnosticsWindow::ruleRequested, &window,
                [&](const QString &entry, int) { emitted << entry; });
        libcore::QueryConnectionsResp snapshot;
        snapshot.active.push_back(connection("1"));
        window.applyConnections(snapshot);
        for (auto *action : window.findChild<QPushButton *>("diagnosticAddRule")->menu()->actions())
            if (action->menu() != nullptr && action->text().contains("Domain and subdomains"))
                action->menu()->actions().first()->trigger();
        QCOMPARE(emitted, QStringList{QStringLiteral("suffix:example.org")});
    }
    void observationRetainsClosedAndIgnoresLatePollAfterStop() {
        DiagnosticsWindow window;
        auto *observe = window.findChild<QPushButton *>("diagnosticObserve");
        auto *tree = window.findChild<QTreeWidget *>("diagnosticConnections");
        observe->click();
        libcore::QueryConnectionsResp snapshot;
        snapshot.active.push_back(connection("1"));
        window.applyConnections(snapshot);
        snapshot.active.clear();
        auto c = connection("1"); c.closed_at = QDateTime::currentMSecsSinceEpoch();
        snapshot.closed.push_back(c);
        window.applyConnections(snapshot);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QVERIFY(tree->topLevelItem(0)->text(0).contains("Closed"));
        observe->click();
        snapshot.active.push_back(connection("2"));
        window.applyConnections(snapshot);
        QCOMPARE(tree->topLevelItemCount(), 1);
    }
};
QTEST_MAIN(TestDiagnostics)
#include "test_diagnostics.moc"
