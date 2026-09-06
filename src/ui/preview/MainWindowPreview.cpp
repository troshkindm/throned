#include "include/ui/preview/UiPreview.h"
#include "include/ui/preview/MainWindowCapture.h"
#include "include/ui/preview/GeometryReport.h"

#include <QLabel>
#include <QApplication>
#include <QDateTime>
#include <QCursor>
#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QTreeWidget>

#include "include/api/RPC.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/stats/traffic/TrafficLooper.hpp"
#include "include/ui/group/dialog_edit_group.h"
#include "include/ui/mainwindow_interface.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/ui/stats/diagnostics_window.h"
#include "include/ui/stats/dialog_site_reachability.h"
#include "include/ui/widget/GroupTabBar.h"
#include "include/ui/widget/SubscriptionPopover.hpp"

namespace UiPreview {
void RunMainWindow(const QString &prefix) {
    auto *window = GetMainWindow();
    if (window == nullptr) {
        qApp->exit(2);
        return;
    }
    QSize previewSize(1180, 780);
    const QStringList arguments = QApplication::arguments();
    // Documentation screenshots should not age merely because the local
    // preview binary carries a development build stamp.
    if (arguments.contains(QStringLiteral("-ui-preview-docs")))
        if (auto *version = window->findChild<QLabel *>(QStringLiteral("titleVersion"))) version->hide();
    if (const int sizeAt = arguments.indexOf(QStringLiteral("-ui-preview-size"));
        sizeAt >= 0 && sizeAt + 1 < arguments.size()) {
        const QStringList parts = arguments.at(sizeAt + 1).toLower().split(QLatin1Char('x'));
        if (parts.size() == 2) {
            bool widthOk = false;
            bool heightOk = false;
            const int width = parts.at(0).toInt(&widthOk);
            const int height = parts.at(1).toInt(&heightOk);
            if (widthOk && heightOk && width >= 900 && height >= 620)
                previewSize = QSize(width, height);
        }
    }
    window->resize(previewSize);
    if (QScreen *screen = window->screen()) QCursor::setPos(screen->geometry().bottomLeft());

    // `-ui-preview` is forced onto a temporary database in main(), so it is
    // safe to seed the real widgets here. Keep every value visibly synthetic:
    // RFC 5737 addresses, reserved example domains, and invented programs.
    const bool emptyPreview = arguments.contains(QStringLiteral("-ui-preview-empty"));
    if (auto group = Configs::dataManager->groupsRepo->CurrentGroup()) {
        group->name = QStringLiteral("Demo subscription");
        group->url = QStringLiteral("https://subscription.example/profiles");
        // Relative so the readouts stay meaningful in every render instead of drifting.
        group->info = QStringLiteral("upload=8589934592; download=25769803776; total=107374182400; expire=%1")
                          .arg(QDateTime::currentSecsSinceEpoch() + 23 * 86400);
        group->sub_last_update = 1788037200;
        group->provider.announce = arguments.contains(QStringLiteral("-ui-preview-long-announce"))
                                       ? QStringLiteral(
                                             // Both wrap hazards at once: a long URL Qt can break, and a run it cannot.
                                             "Scheduled maintenance on every DE and FR node until 3 September 06:00 UTC, see "
                                             "https://status.example/incidents/2026-09-02-maintenance ref "
                                             "a7f3c19e4b82d05f6617ac93be24d8710fe5b3629a4c8d17")
                                       : QStringLiteral(
                                             "Maintenance on the DE nodes until 3 September, use the NL exits meanwhile.");
        group->provider.supportUrl = QStringLiteral("https://support.example/throned");
        group->provider.webPageUrl = QStringLiteral("https://subscription.example");
        group->provider.updateIntervalMinutes = 360;
        group->provider.intervalFromProvider = true;
        Configs::dataManager->groupsRepo->Save(group);

        const struct {
            const char *type;
            const char *address;
            int port;
            const char *name;
            const char *country;
            int latency;
            const char *downSpeed;
            const char *upSpeed;
            long long up;
            long long down;
        } profileSamples[] = {
            {"vless", "192.0.2.10", 443, "Demo West", "DE", 42, "148 Mbps", "36 Mbps", 7340032, 94371840},
            {"trojan", "198.51.100.24", 8443, "Demo North", "FI", 57, "96 Mbps", "28 Mbps", 4194304, 68157440},
            {"hysteria", "203.0.113.42", 443, "Demo East", "JP", 83, "74 Mbps", "19 Mbps", 2097152, 39845888},
            {"shadowsocks", "192.0.2.71", 2087, "Demo Backup", "NL", 109, "51 Mbps", "14 Mbps", 1048576, 18874368},
        };
        if (!emptyPreview) {
            int profileIndex = 0;
            for (const auto &sample: profileSamples) {
                auto profile = Configs::ProfilesRepo::NewProfile(QString::fromLatin1(sample.type));
                if (!profile || !profile->outbound) continue;
                profile->outbound->SetAddress(QString::fromLatin1(sample.address));
                profile->outbound->server_port = sample.port;
                profile->outbound->name = QString::fromLatin1(sample.name);
                profile->test_country = QString::fromLatin1(sample.country);
                profile->SetLatency(sample.latency);
                profile->dl_speed = QString::fromLatin1(sample.downSpeed);
                profile->ul_speed = QString::fromLatin1(sample.upSpeed);
                profile->ip_out = QString::fromLatin1(sample.address);
                profile->traffic_uplink = sample.up;
                profile->traffic_downlink = sample.down;
                // One starred row, so the favourites view and the row mark are visible.
                profile->favorite = QString::fromLatin1(sample.name) == QStringLiteral("Demo East");
                Configs::dataManager->profilesRepo->AddProfile(profile, group->id);
                if (profileIndex++ == 1)
                    Configs::dataManager->settingsRepo->started_id = profile->id;
            }
        }

        // Enough groups to overflow the strip at any supported window width.
        if (arguments.contains(QStringLiteral("-ui-preview-many-groups"))) {
            static const char *const groupNames[] = {
                "Frankfurt",
                "Amsterdam",
                "Helsinki",
                "Stockholm",
                "Warsaw",
                "Prague",
                "Vienna",
                "Zurich",
                "Milan",
                "Madrid",
                "Lisbon",
                "Dublin",
                "London",
                "Reykjavik",
                "Toronto",
                "Chicago",
                "Dallas",
                "Seattle",
                "Tokyo",
                "Osaka",
                "Singapore",
                "Sydney",
                "Auckland",
                "Cape Town",
            };
            for (const char *const name: groupNames) {
                auto extra = Configs::GroupsRepo::NewGroup();
                if (!extra) continue;
                extra->name = QString::fromLatin1(name);
                if (!Configs::dataManager->groupsRepo->AddGroup(extra)) continue;
            }
        }
        window->refresh_groups();
        window->refresh_proxy_list({}, true);
        if (!emptyPreview) {
            if (auto *profiles = window->findChild<QTableView *>(QStringLiteral("profilesTableView"))) {
                profiles->selectRow(arguments.contains(QStringLiteral("-ui-preview-running-unselected")) ? 0 : 1);
            }
        }
    }

    QList<Stats::ConnectionMetadata> connections;
    const struct {
        const char *dest;
        const char *domain;
        const char *process;
        const char *processPath;
        const char *outbound;
        const char *network;
        const char *protocol;
        long long up;
        long long down;
    } samples[] = {
        {"203.0.113.10:443", "assistant.example", "DemoChat.exe", "C:\\DemoApps\\DemoChat\\DemoChat.exe",
         "proxy", "tcp", "tls", 18422, 918233},
        {"198.51.100.24:443", "accounts.example.net", "ExampleBrowser.exe", "C:\\Program Files\\Example Browser\\ExampleBrowser.exe",
         "proxy", "tcp", "tls", 4211, 88231},
        {"192.0.2.53:53", "resolver.example", "SystemDemo.exe", "C:\\DemoApps\\SystemDemo.exe",
         "direct", "udp", "dns", 128, 344},
        {"198.51.100.7:443", "code.example.org", "EditorDemo.exe", "C:\\Program Files\\Example Editor\\EditorDemo.exe",
         "direct", "tcp", "tls", 9120, 240113},
        {"203.0.113.88:443", "sync.example.com", "SyncDemo.exe",
         "C:\\DemoApps\\SyncDemo\\SyncDemo.exe", "direct", "tcp", "tls", 2211, 51002},
        {"192.0.2.190:1900", "discovery.example", "OverlayDemo.exe",
         "C:\\DemoApps\\OverlayDemo\\OverlayDemo.exe",
         "direct", "udp", "", 640, 0},
    };
    int index = 0;
    for (const auto &sample: samples) {
        Stats::ConnectionMetadata conn;
        conn.id = QString::number(++index);
        conn.dest = QString::fromLatin1(sample.dest);
        conn.domain = QString::fromLatin1(sample.domain);
        conn.process = QString::fromLatin1(sample.process);
        conn.processPath = QString::fromLatin1(sample.processPath);
        conn.outbound = QString::fromLatin1(sample.outbound);
        conn.network = QString::fromLatin1(sample.network);
        conn.protocol = QString::fromLatin1(sample.protocol);
        conn.upload = sample.up;
        conn.download = sample.down;
        conn.uploadSpeed = sample.up / 8;
        conn.downloadSpeed = sample.down / 8;
        connections.append(conn);
    }
    window->UpdateConnectionList(connections);

    if (auto *log = window->findChild<QTextBrowser *>(QStringLiteral("masterLogBrowser"))) {
        log->setPlainText(QStringLiteral(
            "[INF] [2be4] [ui] sing-box: v1.13.20\n"
            "[INF] [2be4] [ui] Xray-core: 26.7.28\n"
            "[INF] [2be4] [ui] Core has successfully connected to Throned\n"
            "[INF] [48809] dns: exchanged A sync.example in 28 ms\n"
            "[INF] [48809] inbound/tun[tun-in]: connection from 192.0.2.190:1900\n"
            "[WRN] [48809] outbound/direct: connection to 198.51.100.7:443 timed out"));
    }

    auto proxy = std::make_shared<Stats::TrafficLooperEntry>();
    proxy->uplink_rate = 84213;
    proxy->downlink_rate = 1348221;
    auto direct = std::make_shared<Stats::TrafficLooperEntry>();
    direct->uplink_rate = 912;
    direct->downlink_rate = 4410;
    window->refresh_status(Stats::DisplaySpeed(proxy) + QChar(0x001F) + Stats::DisplaySpeed(direct));
    window->refresh_status();

    // refresh_proxy_list() completes its model reset on the UI queue. Wait
    // for that reset before treating rowCount as the search baseline.
    QTimer::singleShot(350, window, [window, prefix, arguments, emptyPreview] {
        if (arguments.contains(QStringLiteral("-ui-preview-quick-add"))) {
            CaptureQuickAdd(window, prefix, emptyPreview);
            return;
        }
        if (arguments.contains(QStringLiteral("-ui-preview-selection"))) {
            auto *table = window->findChild<QTableView *>(QStringLiteral("profilesTableView"));
            if (table == nullptr || table->model() == nullptr || table->model()->rowCount() == 0) {
                qWarning() << "The profile table is missing or empty in the selection preview";
                qApp->exit(2);
                return;
            }
            table->selectRow(0);
            window->grab().save(prefix + QStringLiteral("-selected.png"), "PNG");

            // Clicking past the last row is the ordinary way out of a selection, and
            // the group must not hand it back on the next rebuild.
            const QRect last = table->visualRect(table->model()->index(table->model()->rowCount() - 1, 0));
            const QPoint empty(last.center().x(), last.bottom() + 40);
            if (!table->viewport()->rect().contains(empty) || table->indexAt(empty).isValid()) {
                qWarning() << "No empty area under the rows to click in the preview";
                qApp->exit(2);
                return;
            }
            QMouseEvent press(QEvent::MouseButtonPress, empty, table->viewport()->mapToGlobal(empty),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(table->viewport(), &press);
            QMouseEvent release(QEvent::MouseButtonRelease, empty, table->viewport()->mapToGlobal(empty),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(table->viewport(), &release);
            if (table->selectionModel()->hasSelection()) {
                qWarning() << "Clicking the empty area left the selection in place";
                qApp->exit(2);
                return;
            }
            window->refresh_proxy_list({}, true);
            if (table->selectionModel()->hasSelection()) {
                qWarning() << "A refresh handed the dismissed selection back";
                qApp->exit(2);
                return;
            }
            window->grab().save(prefix + QStringLiteral("-cleared.png"), "PNG");
            qApp->exit(0);
            return;
        }
        if (arguments.contains(QStringLiteral("-ui-preview-settings"))) {
            // The redesign moves Designer controls into code-built cards and then deletes
            // the page they came from, so a control nobody placed is destroyed with it.
            // Surviving the constructor is therefore the assertion that it was placed.
            auto *dialog = new DialogBasicSettings(window);
            dialog->show();
            QTimer::singleShot(300, window, [dialog, prefix] {
                // Install/Uninstall are excluded on purpose: this dialog restyles reparented
                // Designer buttons by overwriting their object name, so they no longer
                // answer to it. Their row is proven by url_scheme_status beside them.
                for (const auto &name: {"url_scheme_auto_register", "url_scheme_status",
                                        "siteTargetsEdit", "siteTimeoutSpin"}) {
                    auto *control = dialog->findChild<QWidget *>(QString::fromLatin1(name));
                    if (control == nullptr || control->parentWidget() == nullptr) {
                        qWarning() << "Settings control is missing or unplaced:" << name;
                        qApp->exit(2);
                        return;
                    }
                }
                // Scrolled to the controls under test rather than to the top of the page.
                if (auto *edit = dialog->findChild<QWidget *>(QStringLiteral("siteTargetsEdit"))) {
                    for (auto *area: dialog->findChildren<QScrollArea *>()) {
                        if (area->isAncestorOf(edit)) area->ensureWidgetVisible(edit, 0, 40);
                    }
                }
                QTimer::singleShot(150, dialog, [dialog, prefix] {
                    dialog->grab().save(prefix + QStringLiteral("-settings.png"), "PNG");
                    SaveGeometryReport(dialog, prefix + QStringLiteral("-settings.png"));
                    dialog->close();
                    qApp->exit(0);
                });
            });
            return;
        }
        if (arguments.contains(QStringLiteral("-ui-preview-diagnostics"))) {
            window->grab().save(prefix + QStringLiteral("-diagnostics-entry.png"), "PNG");
            auto *dialog = new DiagnosticsWindow(window);
            if (arguments.contains(QStringLiteral("-ui-preview-diagnostics-light"))) {
                // Exercise live restyling with a deterministic light system palette,
                // even when the Windows desktop itself uses dark mode.
                themeManager()->system_palette = QPalette(QColor(QStringLiteral("#efefef")));
                qApp->setPalette(themeManager()->system_palette);
                themeManager()->ApplyTheme(QStringLiteral("System"), true);
            }
            dialog->setWindowTitle(QStringLiteral("Throned · Diagnostics preview"));
            // These are the widgets the preview drives; a missing one is a real
            // regression, so it exits 2 instead of dereferencing a null child.
            auto *previewAddress = dialog->findChild<QLineEdit *>(QStringLiteral("diagnosticAddress"));
            auto *previewConnections = dialog->findChild<QTreeWidget *>(QStringLiteral("diagnosticConnections"));

            if (previewAddress == nullptr || previewConnections == nullptr) {
                qWarning() << "Diagnostics preview is missing a widget it drives";
                qApp->exit(2);
                return;
            }
            // Same production widgets, isolated synthetic data. No RPC is connected.
            DiagnosticsWindow::LocalState state;
            state.coreRunning = true;
            state.coreVersion = QStringLiteral("sing-box 1.13.20");
            state.coreUptimeMs = 12 * 60 * 1000;
            state.profileName = QStringLiteral("Demo North");
            state.profileType = QStringLiteral("Trojan");
            state.profileLatencyMs = 57;
            state.tun = true;
            state.environmentReport = QStringLiteral("Throned 1.3.8\nOS: Windows 11 (x86_64)\nTun: on | System proxy: off");
            dialog->applyLocalState(state);
            libcore::HealthResponse health;
            health.outbound_tag = "demo-proxy";
            health.outbounds = {"demo-proxy", "demo-eu", "demo-backup", "direct"};
            health.external_ip = "198.51.100.24";
            health.external_country = "FI";
            health.clock_known = true;
            health.clock_skew_ms = 0;
            health.udp_checked = true;
            health.udp_ok = false;
            health.udp_error = "i/o timeout";
            health.dns_domain = "example.org";
            health.dns_compared = true;
            health.dns_agrees = false;
            health.dns_core = {"172.64.155.209"};
            health.dns_system = {"104.18.32.47"};
            dialog->applyHealth(health);
            dialog->show();
            QTimer::singleShot(200, window, [dialog, prefix, previewAddress, previewConnections] {
                dialog->grab().save(prefix + QStringLiteral("-diagnostics-overview.png"), "PNG");
                previewAddress->setText(QStringLiteral("https://example.org"));
                dialog->showAddress(QStringLiteral("https://example.org"));
                libcore::PreviewRouteResponse route;
                route.outbound_tag = "demo-proxy";
                route.matched_rule = "domain_suffix=example.org => route(demo-proxy)";
                route.action = "route";
                dialog->applyRoutePreview(route);
                libcore::DiagnoseSiteResponse result;
                result.outbound_tag = "demo-proxy";
                result.connect_ms = 86;
                result.tls_ms = 115;
                result.http_ms = 68;
                result.status = 403;
                result.tls_version = "TLS 1.3";
                result.tls_alpn = "http/1.1";
                result.tls_issuer = "Demo Root CA";
                result.tls_expires_unix = 1798761600;
                result.dns_ms = 7;
                result.dns_compared = true;
                result.dns_agrees = false;
                result.dns_core = {"172.64.155.209"};
                result.dns_system = {"104.18.32.47"};
                dialog->applySiteResult(result);
                dialog->grab().save(prefix + QStringLiteral("-diagnostics-site.png"), "PNG");
                dialog->showApplication();
                // Twelve sockets over three destinations: the point of the list is that
                // it collapses them and floats the flow that never answered to the top.
                libcore::QueryConnectionsResp snapshot;
                for (int i = 0; i < 12; ++i) {
                    const int kind = i % 3;
                    libcore::ConnectionMetaData c;
                    c.id = std::to_string(i);
                    c.process = "DemoChat.exe";
                    c.process_path = "C:/Demo/DemoChat.exe";
                    c.domain = kind == 2 ? "" : kind == 1 ? "cdn.example.org"
                                                          : "gateway.example.org";
                    c.dest = kind == 2 ? "203.0.113.24:50005" : kind == 1 ? "203.0.113.60:443"
                                                                          : "203.0.113.10:443";
                    c.network = kind == 2 ? "udp" : "tcp";
                    c.outbound = kind == 2 ? "direct" : "demo-proxy";
                    // A real geosite rule, not a one-liner: the detail card has to
                    // hold the routes people actually write.
                    c.matched_rule = kind == 2 ? "network=udp => route(direct)"
                                               : "domain=[*.demo.example gateway.example.org cdn.example.org] "
                                                 "domain_suffix=[example.org example.net example.com] "
                                                 "rule_set=[geosite-demo geosite-demo-chat geosite-demo-cdn geoip-demo geosite-demo-media "
                                                 "geosite-demo-updates geosite-demo-auth] => route(demo-proxy)";
                    c.upload = 8192;
                    c.download = kind == 2 ? 0 : 47104;
                    c.source = "127.0.0.1:5510";
                    snapshot.active.push_back(c);
                }
                // A LAN client sharing the connection: the core reports no owning
                // program for it, which is ordinary and must still be listed.
                for (int i = 0; i < 2; ++i) {
                    libcore::ConnectionMetaData c;
                    c.id = std::to_string(100 + i);
                    c.domain = "updates.example.net";
                    c.dest = "203.0.113.80:443";
                    c.network = "tcp";
                    c.outbound = "demo-proxy";
                    c.source = "192.168.1.42:5510";
                    c.upload = 2048;
                    c.download = 9216;
                    snapshot.active.push_back(c);
                }
                dialog->applyConnections(snapshot);
                if (previewConnections->topLevelItemCount() < 3) {
                    qWarning() << "Diagnostics preview did not list the synthetic connections";
                    qApp->exit(2);
                    return;
                }
                // Row 0 is whatever the window ranked most suspicious, which is the case worth showing.
                previewConnections->setCurrentItem(previewConnections->topLevelItem(0));

                QTimer::singleShot(200, dialog, [dialog, prefix, previewConnections] {
                    dialog->grab().save(prefix + QStringLiteral("-diagnostics-app.png"), "PNG");
                    // The row carrying the geosite rule, which is what the detail card
                    // has to condense rather than print in full. The replaced fact
                    // labels leave the card only once deleteLater runs, so the capture
                    // drains those first instead of photographing both sets at once.
                    previewConnections->setCurrentItem(previewConnections->topLevelItem(1));
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    if (auto *detail = dialog->findChild<QScrollArea *>(QStringLiteral("diagnosticDetailScroll"))) {
                        detail->widget()->adjustSize();
                        detail->verticalScrollBar()->setValue(detail->verticalScrollBar()->maximum());
                    }
                    dialog->grab().save(prefix + QStringLiteral("-diagnostics-app-rule.png"), "PNG");
                    auto *reportRail = dialog->findChildren<QPushButton *>(QStringLiteral("diagnosticRail")).value(4);
                    if (reportRail == nullptr) {
                        qWarning() << "Diagnostics preview lost the report section";
                        qApp->exit(2);
                        return;
                    }
                    auto *statsRail = dialog->findChildren<QPushButton *>(QStringLiteral("diagnosticRail")).value(3);
                    if (statsRail == nullptr) {
                        qWarning() << "Diagnostics preview lost the statistics section";
                        qApp->exit(2);
                        return;
                    }
                    // Synthetic week of traffic: the shape of the screen, not the probe.
                    DiagnosticsWindow::Usage usage;
                    usage.bucketSecs = 86400;
                    usage.daysStored = 7;
                    usage.databaseBytes = 4404019;
                    // Paths as long as the ones a store-installed program really has:
                    // the row has to survive them without pushing the bar and the
                    // share off its own card.
                    // Four shapes the split has to render: mostly proxied, split down
                    // the middle, entirely proxied, and entirely around the tunnel.
                    usage.apps = {
                        {QStringLiteral("chrome.exe"), QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"), 720LL << 20, 18400LL << 20, 90LL << 20, 3100LL << 20},
                        {QStringLiteral("steam.exe"), QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/Steamworks Shared/steam.exe"), 190LL << 20, 11200LL << 20, 150LL << 20, 9800LL << 20},
                        {QStringLiteral("Telegram.exe"), QStringLiteral("C:/Users/demo/AppData/Roaming/Telegram Desktop/tdata/user_data/Telegram.exe"), 640LL << 20, 7900LL << 20, 0, 0},
                        {QStringLiteral("DemoChat.exe"), QStringLiteral("C:/Program Files/WindowsApps/DemoChat_1.46388.4.0_x64__pzs8sxrjxfjjc/app/DemoChat.exe"), 210LL << 20, 4100LL << 20, 210LL << 20, 4100LL << 20},
                    };
                    usage.servers = {
                        {QStringLiteral("Demo North"), QStringLiteral("Demo subscription"), 1200LL << 20, 33000LL << 20, 0, 0},
                        {QStringLiteral("Direct"), {}, 560LL << 20, 8600LL << 20, 560LL << 20, 8600LL << 20},
                    };
                    const auto today = QDateTime::currentSecsSinceEpoch() / 86400 * 86400;
                    const qint64 shape[] = {5600, 7400, 3800, 8800, 4600, 6400, 3000};
                    for (int day = 0; day < 7; ++day)
                        usage.series.append({today - (6 - day) * 86400, shape[day] << 17, shape[day] << 20,
                                             (shape[day] << 17) / 5, (shape[day] << 20) / 4,
                                             QDateTime::fromSecsSinceEpoch(today - (6 - day) * 86400).toString(QStringLiteral("dd.MM"))});
                    // Deliberately a different shape from the app series: the servers
                    // tab used to draw the app aggregation under its own totals, and
                    // only two distinguishable curves show that it no longer does.
                    const qint64 serverShape[] = {8200, 3100, 6900, 4200, 7600, 2800, 5400};
                    for (int day = 0; day < 7; ++day)
                        usage.serverSeries.append({today - (6 - day) * 86400, serverShape[day] << 17, serverShape[day] << 20,
                                                   (serverShape[day] << 17) / 3, (serverShape[day] << 20) / 3,
                                                   QDateTime::fromSecsSinceEpoch(today - (6 - day) * 86400).toString(QStringLiteral("dd.MM"))});
                    dialog->applyUsage(usage);
                    statsRail->click();
                    dialog->applyUsage(usage);
                    QTimer::singleShot(120, dialog, [dialog, prefix, reportRail] {
                        dialog->grab().save(prefix + QStringLiteral("-diagnostics-stats.png"), "PNG");
                        // The same page with the tunnel bypass filtered out, which is
                        // the state the split bar exists to explain.
                        if (auto *proxyOnly = dialog->findChild<QPushButton *>(QStringLiteral("diagnosticUsageProxyOnly"))) {
                            // Every posted event, not just the deletions: rebuilt rows
                            // are only shown once their ChildAdded and layout requests
                            // are delivered, and a grab before that photographs a hole.
                            // Two passes, and both are needed: the generic queue never
                            // carries DeferredDelete, so the replaced rows only leave
                            // on the typed pass while the new ones are only shown on
                            // the generic one. Skipping either photographs both sets.
                            const auto settle = [] {
                                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                                QCoreApplication::sendPostedEvents();
                            };
                            proxyOnly->setChecked(true);
                            settle();
                            dialog->grab().save(prefix + QStringLiteral("-diagnostics-stats-proxyonly.png"), "PNG");
                            proxyOnly->setChecked(false);
                            settle();
                            // The servers tab, whose chart and totals now describe the
                            // same aggregation.
                            if (auto *serversTab = dialog->findChildren<QPushButton *>(
                                                             QStringLiteral("diagnosticUsageTab"))
                                                       .value(1)) {
                                serversTab->click();
                                settle();
                                dialog->grab().save(prefix + QStringLiteral("-diagnostics-stats-servers.png"), "PNG");
                                dialog->findChildren<QPushButton *>(QStringLiteral("diagnosticUsageTab")).value(0)->click();
                                settle();
                            }
                        }
                        // The smallest window the dialog allows, once per section. This
                        // is where a page that cannot scroll stops dropping what does
                        // not fit and starts stacking it instead, and only a render of
                        // every section shows which one has stopped coping.
                        dialog->resize(dialog->minimumSize());
                        QTimer::singleShot(120, dialog, [dialog, prefix, reportRail] {
                            for (int section = 0; section < 5; ++section) {
                                dialog->findChildren<QPushButton *>(QStringLiteral("diagnosticRail")).value(section)->click();
                                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                                dialog->grab().save(prefix + QStringLiteral("-diagnostics-short-%1.png").arg(section), "PNG");
                            }
                            dialog->resize(940, 800);
                            reportRail->click();
                            QTimer::singleShot(120, dialog, [dialog, prefix] {
                                dialog->grab().save(prefix + QStringLiteral("-diagnostics-report.png"), "PNG");
                                dialog->close();
                                qApp->exit(0);
                            });
                        });
                    });
                });
            });
            return;
        }
        if (arguments.contains(QStringLiteral("-ui-preview-sites"))) {
            // No core runs in preview, so the verdicts are synthetic: this proves the
            // grid, the colours and the three states, not the probe.
            auto *dialog = new SiteReachabilityDialog(window);
            const auto ids = Configs::dataManager->groupsRepo->CurrentGroup()->Profiles();
            const QStringList sites{"Google", "YouTube", "ChatGPT", "Telegram", "GitHub", "Netflix"};
            QList<QPair<int, QString>> profiles;
            for (const int id: ids) {
                const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
                profiles.append({id, profile == nullptr ? QStringLiteral("Unknown") : profile->name});
            }
            dialog->beginRun(profiles, sites);
            TestRunner::SiteReport report;
            report.sites = sites;
            const QList<QList<int>> sample{
                {204, 200, 200, 200, 200, 200},
                {204, 200, 403, 200, 200, 403},
                {204, 200, 0, 200, 200, 0},
                {0, 0, 0, 0, 0, 0},
            };
            for (int row = 0; row < ids.size(); row++) {
                QList<TestRunner::SiteVerdict> verdicts;
                for (const int status: sample.at(row % sample.size())) {
                    verdicts << TestRunner::SiteVerdict{
                        status, status > 0 ? 40 + (row * 17 + verdicts.size() * 9) % 180 : 0,
                        status > 0 ? QString() : QStringLiteral("context deadline exceeded")};
                }
                report.rows.insert(ids.at(row), verdicts);
            }
            if (arguments.contains(QStringLiteral("-ui-preview-sites-error"))) {
                report.rows.clear();
                for (const int id: ids) report.errors.insert(id, QStringLiteral("unknown method: SiteTest"));
                if (!ids.isEmpty()) {
                    report.errors.remove(ids.last());
                    report.skipped << ids.last();
                }
            }
            dialog->applyReport(report);
            dialog->show();
            QTimer::singleShot(250, window, [dialog, prefix] {
                dialog->grab().save(prefix + QStringLiteral("-sites.png"), "PNG");
                dialog->close();
                qApp->exit(0);
            });
            return;
        }
        if (arguments.contains(QStringLiteral("-ui-preview-hover"))) {
            auto *groups = window->findChild<QTabWidget *>(QStringLiteral("groupsCard"));
            auto *groupBar = groups == nullptr ? nullptr : qobject_cast<GroupTabBar *>(groups->tabBar());
            if (groupBar == nullptr) {
                qWarning() << "Group tab bar is missing from the hover preview";
                qApp->exit(2);
                return;
            }
            // A provider is allowed to omit subscription-userinfo. The tab then
            // has no usage line, but its refresh cycle and provider links must
            // still be reachable from the hover card.
            if (auto group = Configs::dataManager->groupsRepo->CurrentGroup()) {
                group->info.clear();
                Configs::dataManager->groupsRepo->Save(group);
                window->refreshSubscriptionReadouts();
            }
            // Hover has to open the card without taking focus, and a click on the
            // same tab has to promote it into a real popup.
            const QPoint hoverPoint = groupBar->tabRect(groupBar->currentIndex()).center();
            QMouseEvent hover(QEvent::MouseMove, hoverPoint, groupBar->mapToGlobal(hoverPoint),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(groupBar, &hover);
            QTimer::singleShot(700, window, [window, groupBar, prefix] {
                auto *card = window->findChild<SubscriptionPopover *>();
                if (card == nullptr || !card->isVisible()) {
                    qWarning() << "Hovering a subscription tab did not open the card";
                    qApp->exit(2);
                    return;
                }
                if (!card->isHoverCard() || card->isActiveWindow()) {
                    qWarning() << "The hover card took focus" << card->isHoverCard() << card->isActiveWindow();
                    qApp->exit(2);
                    return;
                }
                SavePopupComposite(window, card, prefix + QStringLiteral("-hover-card.png"));
                emit groupBar->meterClicked(groupBar->currentIndex());
                if (card->isHoverCard()) {
                    qWarning() << "Clicking did not promote the hover card into a popup";
                    qApp->exit(2);
                    return;
                }
                card->close();
                // The card is owned and reused by MainWindow. Closing it used to delete
                // the object while leaving MainWindow's raw pointer dangling, so the
                // second hover crashed or called into freed memory.
                QTimer::singleShot(80, window, [window, groupBar, card] {
                    emit groupBar->meterHovered(groupBar->currentIndex());
                    QTimer::singleShot(700, window, [window, card] {
                        auto *reopened = window->findChild<SubscriptionPopover *>();
                        if (reopened != card || !reopened->isVisible() || !reopened->isHoverCard()) {
                            qWarning() << "The subscription card was not safely reused after closing";
                            qApp->exit(2);
                            return;
                        }
                        reopened->close();
                        qApp->exit(0);
                    });
                });
            });
            return;
        }
        if (arguments.contains(QStringLiteral("-ui-preview-subscription"))) {
            auto *groups = window->findChild<QTabWidget *>(QStringLiteral("groupsCard"));
            auto *groupBar = groups == nullptr ? nullptr : qobject_cast<GroupTabBar *>(groups->tabBar());
            if (groupBar == nullptr) {
                qWarning() << "Group tab bar is missing from the subscription preview";
                qApp->exit(2);
                return;
            }
            window->grab().save(prefix + QStringLiteral("-announce.png"), "PNG");
            emit groupBar->meterClicked(groupBar->currentIndex());
            QTimer::singleShot(250, window, [window, prefix] {
                auto *popover = window->findChild<SubscriptionPopover *>();
                if (popover == nullptr || !popover->isVisible()) {
                    qWarning() << "The subscription popover did not open";
                    qApp->exit(2);
                    return;
                }
                popover->grab().save(prefix + QStringLiteral("-subscription.png"), "PNG");
                SavePopupComposite(window, popover, prefix + QStringLiteral("-subscription-in-place.png"));
                // The mute toggle is icon-only, so its off state is worth a render of its own.
                auto *mute = popover->findChild<QToolButton *>(QStringLiteral("subPopoverMute"));
                if (mute == nullptr) {
                    qWarning() << "The subscription popover lost its notification toggle";
                    qApp->exit(2);
                    return;
                }
                mute->click();
                popover->grab().save(prefix + QStringLiteral("-subscription-muted.png"), "PNG");
                mute->click();
                popover->close();

                // Dismissing it is also the only way to see the table header with
                // nothing above it, which is where its top border comes and goes.
                auto *dismiss = window->findChild<QToolButton *>(QStringLiteral("subAnnounceClose"));
                if (dismiss == nullptr) {
                    qWarning() << "The announcement strip lost its dismiss button";
                    qApp->exit(2);
                    return;
                }
                dismiss->click();
                QTimer::singleShot(120, window, [window, prefix] {
                    window->grab().save(prefix + QStringLiteral("-announce-dismissed.png"), "PNG");
                });

                // The per-group refresh cycle lives in the group editor, so the
                // same run proves those controls are wired and renders them.
                auto *editor = new DialogEditGroup(
                    Configs::dataManager->groupsRepo->CurrentGroup(), window);
                editor->show();
                QTimer::singleShot(200, window, [editor, window, prefix] {
                    if (editor->findChild<QSpinBox *>(QStringLiteral("update_interval_hours")) == nullptr) {
                        qWarning() << "The group editor is missing its update-interval control";
                        qApp->exit(2);
                        return;
                    }
                    editor->grab().save(prefix + QStringLiteral("-group-editor.png"), "PNG");
                    editor->close();

                    // The Program menu had no coverage at all, and it now carries the
                    // start-with submenu, whose place in the order is easy to get wrong.
                    auto *program = window->findChild<QMenu *>(QStringLiteral("menu_program"));
                    if (program == nullptr) {
                        qWarning() << "The Program menu is missing from the preview";
                        qApp->exit(2);
                        return;
                    }
                    program->popup(window->mapToGlobal(QPoint(20, 60)));
                    QTimer::singleShot(200, window, [program, window, prefix] {
                        // The submenu is parented to the window, not to the menu it hangs off.
                        auto *sub = window->findChild<QMenu *>(QStringLiteral("startPickMenu"));
                        if (sub == nullptr) {
                            qWarning() << "The start-with submenu is missing from the Program menu";
                            qApp->exit(2);
                            return;
                        }
                        sub->setEnabled(true);
                        sub->popup(program->mapToGlobal(QPoint(program->width() - 8, 120)));
                        QTimer::singleShot(200, program, [program, sub, prefix] {
                            program->grab().save(prefix + QStringLiteral("-program-menu.png"), "PNG");
                            sub->grab().save(prefix + QStringLiteral("-start-with.png"), "PNG");
                            sub->close();
                            program->close();
                            qApp->exit(0);
                        });
                    });
                });
            });
            return;
        }
        if (arguments.contains(QStringLiteral("-ui-preview-favorites"))) {
            auto *favorites = window->findChild<QToolButton *>(QStringLiteral("favoritesTabButton"));
            auto *groups = window->findChild<QTabWidget *>(QStringLiteral("groupsCard"));
            if (favorites == nullptr || groups == nullptr) {
                qWarning() << "Favourites preview controls are missing";
                qApp->exit(2);
                return;
            }
            favorites->click();
            QTimer::singleShot(120, window, [window, favorites, groups, prefix] {
                auto *groupBar = qobject_cast<GroupTabBar *>(groups->tabBar());
                const bool opened = favorites->isChecked() && groupBar != nullptr && !groupBar->isSelectionVisible();
                if (!opened) {
                    qWarning() << "Favourites kept a group tab visually selected"
                               << favorites->isChecked()
                               << (groupBar != nullptr ? groupBar->isSelectionVisible() : true);
                    qApp->exit(2);
                    return;
                }
                window->grab().save(prefix + QStringLiteral("-favorites.png"), "PNG");
                favorites->click();
                QTimer::singleShot(80, window, [favorites, groupBar] {
                    const bool restored = !favorites->isChecked() && groupBar->isSelectionVisible();
                    if (!restored)
                        qWarning() << "Leaving favourites did not restore the group tab"
                                   << favorites->isChecked() << groupBar->isSelectionVisible();
                    qApp->exit(restored ? 0 : 2);
                });
            });
            return;
        }
        // The list is the point of the button, so the catalogue shows it open.
        if (arguments.contains(QStringLiteral("-ui-preview-group-menu"))) {
            auto *overflow = window->findChild<QToolButton *>(QStringLiteral("groupOverflowButton"));
            if (overflow == nullptr || !overflow->isVisible()) {
                qWarning() << "The group overflow button is missing or hidden; the strip did not overflow";
                qApp->exit(2);
                return;
            }
            overflow->click();
            QTimer::singleShot(350, window, [window, prefix] {
                QWidget *menu = nullptr;
                for (QWidget *top: QApplication::topLevelWidgets())
                    if (top->isVisible() && top->inherits("GroupOverflowMenu")) menu = top;
                if (menu == nullptr) {
                    qWarning() << "The group overflow list did not open";
                    qApp->exit(2);
                    return;
                }
                QPixmap composed = window->grab();
                QPainter painter(&composed);
                painter.drawPixmap(window->mapFromGlobal(menu->mapToGlobal(QPoint(0, 0))), menu->grab());
                painter.end();
                composed.save(prefix + QStringLiteral("-group-menu.png"), "PNG");
                qApp->exit(0);
            });
            return;
        }
        VerifyStatsPanelAndCapture(window, prefix);
    });
}

} // namespace UiPreview
