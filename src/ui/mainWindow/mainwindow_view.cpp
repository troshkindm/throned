#include "include/ui/mainwindow.h"
#include "NkrVersion.h"

#include <QApplication>
#include <QScopeGuard>
#include <QFrame>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>

#include <future>
#include <vector>

#include "include/api/RPC.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/stats/autoselector/AutoSelectorMonitor.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/stats/dialog_auto_selector.h"
#include <QStyledItemDelegate>

#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/utils/ProfilesTableFilterHeader.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "include/ui/widget/StartStopButton.hpp"

void MainWindow::applyTopBarMetrics() {
    // MainPreview deliberately lets each compact nav item fit its own label.
    const QList<QToolButton*> menuButtons = {
        ui->toolButton_program, ui->toolButton_preferences, ui->toolButton_testing,
        ui->toolButton_routing, ui->toolButton_tools,
    };
    for (auto *button : menuButtons) {
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->updateGeometry();
    }
    // An explicit minimum stops the layout raising the floor itself, and translated nav labels can outgrow the designed one.
    const QSize contentMin = minimumSizeHint();
    setMinimumSize(qMax(designMinimumSize.width(), contentMin.width()),
                   qMax(designMinimumSize.height(), contentMin.height()));
    FitWindowToScreen(this);
}

void MainWindow::UpdateDataView(bool force)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - lastUpdatedMs.load() < 100)
    {
        return;
    }
    auto html = dataViewHtmlGenerator_.buildHtml();
    runOnUiThread([=, this] {
        ui->data_view->setHtml(html);
        const bool hasTransientStatus = !html.trimmed().isEmpty();
        ui->data_view->setFixedHeight(hasTransientStatus ? 72 : 0);
        ui->data_view->setVisible(hasTransientStatus);
        const bool hasBatchSelection = ui->profilesTableView->selectionModel()
            && ui->profilesTableView->selectionModel()->selectedRows().size() > 1;
        if (auto *connectedStatus = findChild<QFrame *>(QStringLiteral("statusCard"))) {
            connectedStatus->setVisible(!hasTransientStatus && !hasBatchSelection);
        }
        if (auto *selectionStatus = findChild<QFrame *>(QStringLiteral("selectionCard"))) {
            selectionStatus->setVisible(!hasTransientStatus && hasBatchSelection);
        }
    }, true);
    lastUpdatedMs.store(QDateTime::currentMSecsSinceEpoch());
}

void MainWindow::setDownloadReport(const DownloadProgressReport& report, bool show)
{
    dataViewHtmlGenerator_.setDownloadReport(report, show);
}

void MainWindow::refresh_auto_selector_view()
{
    const auto view = Stats::autoSelectorMonitor->Snapshot();
    dataViewHtmlGenerator_.setAutoSelectorStatus(view.valid ? view.summary() : QString(),
                                                 view.valid ? view.detail() : QString());
    ui->actionAuto_Selector->setVisible(view.valid);
    UpdateDataView();
    if (m_autoSelectorDialog != nullptr) m_autoSelectorDialog->refresh();
}

void MainWindow::updateLogFilterFields() {
    const auto level = Configs::SingBox::NormalizeLogLevel(Configs::dataManager->settingsRepo->log_level);
    if (logLevelSelector != nullptr) {
        const QSignalBlocker blocker(logLevelSelector);
        if (const int index = logLevelSelector->findData(level); index >= 0) logLevelSelector->setCurrentIndex(index);
    }
    const int rank = Configs::SingBox::LogLevelRank(level);
    bool levelChanged;
    {
        QMutexLocker locker(&logMutex);
        levelChanged = minLogLevelRank >= 0 && rank != minLogLevelRank;
        minLogLevelRank = rank;
        includeKeywords.clear();
        excludeKeywords.clear();
        for (const auto& inKeyword : Configs::dataManager->settingsRepo->log_include_keyword) includeKeywords.append(inKeyword);
        for (const auto& exKeyword : Configs::dataManager->settingsRepo->log_exclude_keyword) excludeKeywords.append(exKeyword);
        includeCombined.setPattern(Configs::dataManager->settingsRepo->log_include_regex.join("|"));
        excludeCombined.setPattern(Configs::dataManager->settingsRepo->log_exclude_regex.join("|"));
        includeCombined.optimize();
        excludeCombined.optimize();
    }
    // The lines already on screen went through the old threshold, so leaving them
    // would read as the selector doing nothing.
    if (levelChanged) {
        clear_log_view();
        QString notice = tr("Log level: %1. Only lines at this level and above are shown.").arg(level);
        // Raising hides lines at once, but lowering cannot invent what the core never
        // emitted: it keeps writing at the level it started with until it restarts.
        if (Configs::dataManager->settingsRepo->started_id >= 0 && coreLogLevelRank_ >= 0 && rank < coreLogLevelRank_) {
            notice += QLatin1Char(' ')
                + tr("The running core writes at %1 - restart it to get more.")
                      .arg(Configs::SingBox::LogLevels.value(coreLogLevelRank_));
        }
        MW_show_log(notice);
    }
}

void MainWindow::applyProfileFilters()
{
    if (!profilesFilterModel) return;
    profilesFilterModel->setFilters(typeFilterString, addressFilterString, nameFilterString, countryFilterString);
    profilesFilterModel->setSearch(globalFilterString);
    refresh_proxy_list_column_size();
}

void MainWindow::setStatusText(QLabel *label, const QString &text) {
    if (label == nullptr) return;
    label->setProperty("statusFullText", text);
    const int available = label->width() - 2;
    const QString elided = available > 8 ? label->fontMetrics().elidedText(text, Qt::ElideRight, available) : text;
    label->setText(elided);
    // Only clear a tooltip we set: label_running has its own in select mode.
    if (elided != text) {
        label->setProperty("statusOwnsToolTip", true);
        label->setToolTip(text);
    } else if (label->property("statusOwnsToolTip").toBool()) {
        label->setProperty("statusOwnsToolTip", false);
        label->setToolTip({});
    }
}

void MainWindow::refresh_status(const QString &traffic_update) {
    const auto* settings = Configs::dataManager->settingsRepo.get();

    auto refresh_speed_label = [=,this] {
        if (settings->disable_traffic_stats) {
            setStatusText(ui->label_speed, "");
            setStatusText(statusDirectSpeed, "");
        }
        else if (traffic_update_cache == "") {
            // Same shape as the populated state so the status bar does not
            // reflow, but with a placeholder instead of a dangling number.
            const QString idle = QStringLiteral("↑ —   ↓ —");
            setStatusText(ui->label_speed, idle);
            setStatusText(statusDirectSpeed, idle);
        } else {
            const QStringList halves = traffic_update_cache.split(QChar(0x001F));
            setStatusText(ui->label_speed, halves.value(0));
            setStatusText(statusDirectSpeed, halves.value(1));
        }
    };

    if (!traffic_update.isEmpty() && !settings->disable_traffic_stats) {
        traffic_update_cache = traffic_update;
        if (traffic_update == "STOP") {
            traffic_update_cache = "";
        } else {
            refresh_speed_label();
            return;
        }
    }

    refresh_speed_label();

    QString group_name;
    if (running != nullptr) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(running->gid);
        if (group != nullptr) group_name = group->name;
    }

    // An endpoint profile never resolves a country, so its tunnel state takes that slot.
    const QString runningDetail = m_vpnEndpointState.isEmpty()
                                      ? (running ? running->runningCountryInfo : QString())
                                      : m_vpnEndpointState;

    if (QDateTime::currentSecsSinceEpoch() - last_test_time > 2) {
        QString runningLabelText;
        if (running) {
            runningLabelText = QString("[%1] %2").arg(group_name, running->outbound->DisplayName());
        } else {
            runningLabelText = tr("Not Running");
        }
        setStatusText(ui->label_running, runningLabelText);
        if (statusConnectionCaption != nullptr) {
            setStatusText(statusConnectionCaption,
                          running && !runningDetail.isEmpty()
                              ? tr("Connection") + QStringLiteral(" · ") + runningDetail
                              : tr("Connection"));
        }
    }
    const auto display_socks = DisplayAddress(settings->inbound_address, settings->inbound_socks_port);
    const auto inbound_disabled = settings->disable_mixed_inbound;
    const auto inbound_txt = QString("Mixed: %1").arg(inbound_disabled ? "Disabled" : display_socks);
    setStatusText(ui->label_inbound, inbound_txt);
    //
    ui->checkBox_VPN->setChecked(settings->spmode_vpn);
    ui->checkBox_SystemProxy->setChecked(settings->spmode_system_proxy);
    if (select_mode) {
        setStatusText(ui->label_running, tr("Select") + " *");
        ui->label_running->setToolTip(tr("Select mode, double-click or press Enter to select a profile, press ESC to exit."));
    } else {
        ui->label_running->setToolTip({});
    }

    const auto route = Configs::dataManager->routesRepo->GetRouteProfile(settings->current_route_id);
    const QString activeRouteName = (route && route->name != "Default") ? route->name : "";

    auto make_title = [=,this](bool isTray) {
        QStringList tt;
        if (!isTray && Configs::IsAdmin()) tt << "[Admin]";
        if (select_mode) tt << "[" + tr("Select") + "]";
        if (!title_error.isEmpty()) tt << "[" + title_error + "]";
        if (settings->spmode_vpn && !settings->spmode_system_proxy) tt << "[Tun]";
        if (!settings->spmode_vpn && settings->spmode_system_proxy) tt << "[" + tr("System Proxy") + "]";
        if (settings->spmode_vpn && settings->spmode_system_proxy) tt << "[Tun+" + tr("System Proxy") + "]";
        tt << software_name;
        if (!isTray) tt << QString(NKR_VERSION);
        if (!activeRouteName.isEmpty()) {
            tt << "[" + activeRouteName + "]";
        }
        if (running != nullptr) {
            tt << running->outbound->DisplayTypeAndName() + "@" + group_name;
            if (!runningDetail.isEmpty()) {
                tt << runningDetail;
            }
        }
        return tt.join(isTray ? "\n" : " ");
    };

    auto icon_status_new = Icon::NONE;

    if (running != nullptr) {
        if (settings->spmode_vpn) {
            icon_status_new = Icon::VPN;
        } else if (settings->system_dns_set && settings->spmode_system_proxy) {
            icon_status_new = Icon::SYSTEM_PROXY_DNS;
        } else if (settings->system_dns_set) {
            icon_status_new = Icon::DNS;
        } else if (settings->spmode_system_proxy) {
            icon_status_new = Icon::SYSTEM_PROXY;
        } else {
            icon_status_new = Icon::RUNNING;
        }
    }

    setWindowTitle(make_title(false));
    if (icon_status_new != icon_status) QApplication::setWindowIcon(GetTrayIcon(icon_status_new));

    if (tray != nullptr) {
        tray->setToolTip(make_title(true));
        if (icon_status_new != icon_status) tray->setIcon(Icon::GetTrayIcon(icon_status_new));
    }

    icon_status = icon_status_new;

    refresh_startstop_button();
}

void MainWindow::refresh_startstop_button() {
    auto *btn = ui->toolButton_startstop;
    if (btn == nullptr) return;

    const auto &settings = Configs::dataManager->settingsRepo;

    auto mode = StartStopButton::Mode::Off;
    if (running != nullptr) {
        if (settings->spmode_vpn) mode = StartStopButton::Mode::Tun;
        else if (settings->system_dns_set && settings->spmode_system_proxy) mode = StartStopButton::Mode::SystemProxyDns;
        else if (settings->system_dns_set) mode = StartStopButton::Mode::Dns;
        else if (settings->spmode_system_proxy) mode = StartStopButton::Mode::SystemProxy;
        else mode = StartStopButton::Mode::Core;
    }
    btn->setMode(mode);

    StartStopButton::State state;
    if (m_profileConnecting) state = StartStopButton::State::Connecting;
    else if (m_profileDisconnecting) state = StartStopButton::State::Disconnecting;
    else if (running != nullptr) state = StartStopButton::State::Running;
    else if (get_profile_to_start() >= 0) state = StartStopButton::State::Idle;
    else state = StartStopButton::State::Disabled;
    btn->setState(state);
}

void MainWindow::update_traffic_graph(int proxyDl, int proxyUp, int directDl, int directUp)
{
    if (speedChartWidget) {
        QMap<SpeedWidget::GraphType, long> pointData;
        pointData[SpeedWidget::OUTBOUND_PROXY_UP] = proxyUp;
        pointData[SpeedWidget::OUTBOUND_PROXY_DOWN] = proxyDl;
        pointData[SpeedWidget::OUTBOUND_DIRECT_UP] = directUp;
        pointData[SpeedWidget::OUTBOUND_DIRECT_DOWN] = directDl;

        speedChartWidget->AddPointData(pointData);
    }
}

void MainWindow::refresh_proxy_list_column_size() {
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group || !ui->profilesTableView->isVisible()) return;

    auto *hHeader = dynamic_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader());
    QTimer::singleShot(0, ui->profilesTableView, [=, this]() {
        // The resizeSection / scrollbar-policy changes below re-enter here via valueChanged.
        if (m_adjustingColumns) return;
        m_adjustingColumns = true;
        QScrollBar *vBar = ui->profilesTableView->verticalScrollBar();
        const bool vBarBlocked = vBar->blockSignals(true);
        hHeader->blockSignals(true);
        const int columnCount = hHeader->count();
        // Widths saved before the column set last changed no longer line up with
        // the header, so fall back to auto-sizing instead of indexing past the end.
        if (!group->column_width.isEmpty() && group->column_width.size() != columnCount) {
            group->column_width.clear();
        }
        const bool comfortable = profilesTableModel != nullptr
            && profilesTableModel->rowStyle() == ProfilesTableModel::RowStyle::Comfortable;
        if (group->column_width.isEmpty() && comfortable) {
            hHeader->setSectionResizeMode(ProfilesTableModel::ColcServer, QHeaderView::Stretch);
            for (int col : {ProfilesTableModel::ColcPing, ProfilesTableModel::ColcSpeed,
                            ProfilesTableModel::ColcTraffic}) {
                hHeader->setSectionResizeMode(col, QHeaderView::Fixed);
                hHeader->resizeSection(col, ProfileRowDelegate::metricColumnWidth(
                                                col, ui->profilesTableView->font()));
            }
            ui->profilesTableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            group->clearCalculatedColumnWidth();
            for (int i = 0; i < columnCount; i++) group->calculated_column_width << hHeader->sectionSize(i);
        } else if (group->column_width.isEmpty()) {
            hHeader->setSectionResizeMode(ProfilesTableModel::ColType, QHeaderView::ResizeToContents);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColAddress, QHeaderView::Stretch);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColName, QHeaderView::Stretch);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColTestResult, QHeaderView::ResizeToContents);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColTraffic, QHeaderView::ResizeToContents);
            // ResizeToContents only measures on-screen rows, so pin these or they jitter while scrolling.
            for (int col : {ProfilesTableModel::ColType,
                            ProfilesTableModel::ColTestResult, ProfilesTableModel::ColTraffic}) {
                if (group->calculated_column_width.size() > col &&
                    group->calculated_column_width[col] > hHeader->sectionSize(col)) {
                    hHeader->setSectionResizeMode(col, QHeaderView::Fixed);
                    hHeader->resizeSection(col, group->calculated_column_width[col]);
                }
            }
            ui->profilesTableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            group->clearCalculatedColumnWidth();
            for (int i = 0; i < columnCount; i++) {
                auto size = hHeader->sectionSize(i);
                hHeader->setSectionResizeMode(i, QHeaderView::Interactive);
                hHeader->resizeSection(i, size);
                group->calculated_column_width << size;
            }
        } else {
            group->clearCalculatedColumnWidth();
            for (int i = 0; i < columnCount; i++) {
                hHeader->setSectionResizeMode(i, QHeaderView::Interactive);
                hHeader->resizeSection(i, group->column_width.at(i));
            }
            ui->profilesTableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        hHeader->adjustPositions();
        hHeader->blockSignals(false);
        // The view learns about section widths through sectionResized, which was just
        // blocked to keep this routine from re-entering itself. Without a nudge the
        // header ends up at the new width while the rows are still painted at the old
        // one, which is what a window resize used to leave behind.
        ui->profilesTableView->viewport()->update();
        vBar->blockSignals(vBarBlocked);
        m_adjustingColumns = false;
    });
}

void MainWindow::refresh_proxy_list(const QList<int>& ids, bool mayNeedReset, RefreshAnchor anchor) {
    // A finished UDP test flips the setting; this is where the column catches up.
    refreshUdpColumnVisibility();
    if (!Configs::dataManager->settingsRepo->refreshing_group) saveProfileFocusState();
    refresh_proxy_list_impl(ids, mayNeedReset);
    if (mayNeedReset) restoreProfileFocusState(anchor);
}

void MainWindow::refresh_proxy_list_impl(const QList<int>& ids, bool mayNeedReset) {
    const auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
    if (currentGroup == nullptr)
    {
        MW_show_log("Could not find current group!");
        return;
    }
    refresh_proxy_list_impl_refresh_data(ids, mayNeedReset);
    refresh_proxy_list_column_size();
}

void MainWindow::refresh_proxy_list_impl_refresh_data(const QList<int>& ids, bool mayNeedReset) {
    const auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
    if (currentGroup == nullptr) return;
    if (!ids.isEmpty()) {
        for (auto id:ids) profilesTableModel->refreshProfileId(id);
    } else {
        profilesTableModel->refreshTable(currentGroup->profiles, mayNeedReset);
    }
}

std::shared_ptr<Configs::Profile> MainWindow::vpn_exit_endpoint(const std::shared_ptr<Configs::Profile> &ent) {
    auto hop = ent;
    // The "proxy" tag lands on the exit hop, and the stored list runs in to out.
    if (hop != nullptr && hop->type == "chain") {
        const auto *chain = hop->Chain();
        if (chain == nullptr || chain->list.isEmpty()) return nullptr;
        hop = Configs::dataManager->profilesRepo->GetProfile(chain->list.back());
    }
    if (hop == nullptr) return nullptr;
    if (hop->type != "openvpn" && hop->type != "openconnect") return nullptr;
    return hop;
}

QString MainWindow::vpn_state_text(const QString &state, const QString &error) {
    if (state == "connected") return MainWindow::tr("Connect OK");
    if (state == "connecting") return MainWindow::tr("Connecting");
    if (state == "auth-pending") return MainWindow::tr("Waiting for authentication");
    if (state == "error") {
        return error.isEmpty() ? MainWindow::tr("Tunnel error")
                               : MainWindow::tr("Tunnel error") + ": " + error;
    }
    return state;
}

QString MainWindow::liveVpnStateText(bool *connected) {
    if (connected != nullptr) *connected = false;
    const int startedID = Configs::dataManager->settingsRepo->started_id;
    if (startedID < 0) return {};
    if (vpn_exit_endpoint(Configs::dataManager->profilesRepo->GetProfile(startedID)) == nullptr) return {};

    bool ok = false;
    const auto status = API::defaultClient->QueryVPNStatus(&ok, {"proxy"});
    if (!ok || status.results.empty()) return {};
    const auto &res = status.results.front();
    if (connected != nullptr) *connected = res.connected.value();
    return vpn_state_text(QString::fromStdString(res.state.value()),
                          QString::fromStdString(res.error.value()));
}

QString MainWindow::liveVpnConnectOkText() {
    bool connected = false;
    const auto text = liveVpnStateText(&connected);
    return connected ? text : QString();
}

void MainWindow::url_test_current() {
    last_test_time = QDateTime::currentSecsSinceEpoch();
    setStatusText(ui->label_running, tr("Testing"));

    runOnNewThread([=,this] {
        libcore::TestReq req;
        req.test_current = true;
        req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();

        bool rpcOK;
        auto result = API::defaultClient->Test(&rpcOK, req);
        if (!rpcOK || result.results.empty()) return;

        auto latency = result.results[0].latency_ms.value();
        last_test_time = QDateTime::currentSecsSinceEpoch();
        // Blocking RPC, so it has to resolve here rather than on the UI thread.
        const auto vpnText = latency <= 0 ? liveVpnStateText() : QString();

        runOnUiThread([=,this] {
            if (!result.results[0].error.value().empty()) {
                MW_show_log(QString("UrlTest error: %1").arg(QString::fromStdString(result.results[0].error.value())));
            }
            if (latency <= 0) {
                setStatusText(ui->label_running,
                              tr("Test Result") + ": " + (vpnText.isEmpty() ? tr("Unavailable") : vpnText));
            } else if (latency > 0) {
                setStatusText(ui->label_running, tr("Test Result") + ": " + QString("%1 ms").arg(latency));
            }
        });
    });
}

namespace {
    // Multiple targets run concurrently; one probe per tick keeps the monitor
    // cheap while the rolling graph supplies the longer-term signal.
    constexpr int kPingProbeCount = 1;
    constexpr int kPingTimeoutMs = 2000;
    constexpr int kPingHistoryCap = 300; // ten minutes at the two-second tick
    // A spike has to clear both bars: the multiplier alone fires constantly on a
    // fast link where 8 ms to 25 ms is noise, the flat margin alone never fires
    // on a slow one.
    constexpr int kPingSpikeFactor = 3;
    constexpr int kPingSpikeMarginMs = 150;

    int medianOf(QList<int> values) {
        if (values.isEmpty()) return -1;
        std::sort(values.begin(), values.end());
        return values.at(values.size() / 2);
    }
}

void MainWindow::recordPingSample(const QStringList &targets, const QList<int> &proxyMs, const int directMs) {
    if (targets.isEmpty() || proxyMs.size() != targets.size()) return;
    pingHistory_.append({QDateTime::currentSecsSinceEpoch(), targets, proxyMs, directMs});
    while (pingHistory_.size() > kPingHistoryCap) pingHistory_.removeFirst();

    if (pingChartWidget) {
        // Keep loss distinct from latency. The chart renders negative samples as
        // ceiling markers, but excludes them from the dynamic scale.
        QList<double> values;
        for (const auto value : proxyMs) values << value;
        values << directMs;
        pingChartWidget->pushValues(values);
    }
    updatePingLegend(targets, proxyMs, directMs);

    const int primaryMs = proxyMs.first();

    // Baseline from the settled part of the window, so the spike itself does not
    // drag the very number it is being compared against.
    QList<int> baselineSamples;
    for (int i = qMax(0, pingHistory_.size() - 31); i < pingHistory_.size() - 1; ++i) {
        const auto &sample = pingHistory_[i];
        if (!sample.proxyMs.isEmpty() && sample.proxyMs.first() >= 0)
            baselineSamples << sample.proxyMs.first();
    }
    const int baseline = medianOf(baselineSamples);
    if (baseline < 0 || baselineSamples.size() < 5) return;

    const bool bad = primaryMs < 0 || primaryMs > qMax(baseline * kPingSpikeFactor, baseline + kPingSpikeMarginMs);
    if (bad && !pingSpikeActive_) {
        pingSpikeActive_ = true;
        const QString proxyText = primaryMs < 0 ? tr("no reply") : QString("%1 ms").arg(primaryMs);
        // The verdict matters more than the number: it is what saves the user from
        // guessing whether the client, the proxy or their own line is at fault.
        QString verdict;
        if (directMs < 0 || directMs > baseline * kPingSpikeFactor) {
            verdict = tr("the direct path is just as bad, so this is your connection rather than the proxy");
        } else {
            verdict = tr("the direct path is fine (%1 ms), so this is the proxy or the route to it").arg(directMs);
        }
        MW_show_log(tr("UDP latency to %1 spiked: %2 against a %3 ms baseline - %4")
                        .arg(targets.first(), proxyText).arg(baseline).arg(verdict));
    } else if (!bad && pingSpikeActive_) {
        pingSpikeActive_ = false;
        MW_show_log(tr("UDP latency to %1 back to normal (%2 ms).").arg(targets.first()).arg(primaryMs));
    }
}

QString MainWindow::pingHistoryReport() const {
    if (pingHistory_.isEmpty()) return {};
    QStringList out;
    const auto targets = pingHistory_.last().targets;
    out << QString("UDP monitor: %1 ticks, targets: %2").arg(pingHistory_.size()).arg(targets.join(", "));
    for (int targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
        QList<int> samples;
        int lost = 0;
        for (const auto &sample : pingHistory_) {
            const int value = sample.proxyMs.value(targetIndex, -1);
            if (value >= 0) samples << value; else ++lost;
        }
        out << QString("  %1: %2 lost, median %3 ms")
                   .arg(targets.at(targetIndex)).arg(lost).arg(medianOf(samples));
    }
    out << QString("  direct baseline: %1").arg(targets.value(0));
    // Only the tail is worth pasting; the interesting part is always the recent past.
    for (int i = qMax(0, pingHistory_.size() - 40); i < pingHistory_.size(); ++i) {
        const auto &sample = pingHistory_[i];
        const auto render = [](const int value) {
            return value < 0 ? QStringLiteral("lost") : QString::number(value);
        };
        QStringList values;
        for (int targetIndex = 0; targetIndex < sample.targets.size(); ++targetIndex)
            values << QStringLiteral("%1=%2").arg(sample.targets.at(targetIndex),
                                                  render(sample.proxyMs.value(targetIndex, -1)));
        values << QStringLiteral("direct=%1").arg(render(sample.directMs));
        out << QString("  %1  %2")
                   .arg(QDateTime::fromSecsSinceEpoch(sample.at).toString("yyyy-MM-dd HH:mm:ss"),
                        values.join(QStringLiteral("  ")));
    }
    return out.join('\n');
}

// Measures the proxy path and the direct path on the same tick, so a spike can be
// attributed instead of merely observed. Skipped while nothing runs, and never
// overlapped: a slow probe must not queue more of itself behind the timer.
void MainWindow::pollPingMonitor() {
    if (running == nullptr || pingChartWidget == nullptr) return;
    bool idle = false;
    if (!pingProbeInFlight_.compare_exchange_strong(idle, true)) return;

    const auto targets = pingMonitorTargets();
    runOnNewThread([this, targets] {
        // A throw here would otherwise leave the guard raised and stall the monitor for good.
        const auto release = qScopeGuard([this] { pingProbeInFlight_.store(false); });

        const auto probe = [](const QString &target, const bool viaDirect) {
            libcore::UDPTestRequest req;
            req.test_current = true;
            req.via_direct = viaDirect;
            req.probe_count = kPingProbeCount;
            req.test_timeout_ms = kPingTimeoutMs;
            req.target = target.toStdString();

            bool rpcOK = false;
            const auto response = API::defaultClient->UDPTest(&rpcOK, req);
            if (!rpcOK || response.results.empty()) return -1;
            const auto &result = response.results.front();
            if (!result.error.value().empty() || result.received.value() == 0) return -1;
            return static_cast<int>(result.avg_ms.value());
        };

        std::vector<std::future<int>> proxyFutures;
        proxyFutures.reserve(targets.size());
        for (const auto &target : targets)
            proxyFutures.emplace_back(std::async(std::launch::async, probe, target, false));
        auto directFuture = std::async(std::launch::async, probe, targets.first(), true);

        QList<int> proxyMs;
        for (auto &future : proxyFutures) proxyMs << future.get();
        const int directMs = directFuture.get();

        runOnUiThread([this, targets, proxyMs, directMs] {
            // A menu change clears and reconfigures the graph while probes are in
            // flight. Do not mix the old series layout into the new selection.
            if (targets == pingMonitorTargets()) recordPingSample(targets, proxyMs, directMs);
        });
    });
}

void MainWindow::refreshUdpColumnVisibility() {
    if (profilesTableModel == nullptr) return;
    profilesTableModel->setUdpColumnVisible(Configs::dataManager->settingsRepo->show_udp_column);
}

void MainWindow::setStatsPanelOpen(bool open, bool save) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    // The corner widget is taller than the tabs, and both only report their real
    // height once laid out - before that the strip would be measured too short.
    int strip = ui->stats_widget->tabBar()->sizeHint().height();
    if (auto *corner = ui->stats_widget->cornerWidget(Qt::TopRightCorner))
        strip = qMax(strip, corner->sizeHint().height());
    strip += 6;
    const QList<int> sizes = ui->splitter->sizes();

    if (open) {
        ui->stats_widget->setMaximumHeight(QWIDGETSIZE_MAX);
        const int total = sizes.size() == 2 ? sizes[0] + sizes[1] : ui->splitter->height();
        const int panel = qBound(140, settings->stats_panel_height, qMax(140, total - 200));
        ui->splitter->setSizes({total - panel, panel});
    } else {
        // Remember the height it had, so reopening lands where it was left.
        if (sizes.size() == 2 && sizes[1] > strip + 20) settings->stats_panel_height = sizes[1];
        ui->stats_widget->setMaximumHeight(strip);
        // Without handing the freed height back explicitly the splitter leaves it
        // as a gap between the table and the strip.
        const int total = sizes.size() == 2 ? sizes[0] + sizes[1] : ui->splitter->height();
        ui->splitter->setSizes({total - strip, strip});
    }
    // Squashing the tab widget still leaves a sliver of the page under the tabs.
    if (auto *page = ui->stats_widget->currentWidget()) page->setVisible(open);
    // The tools act on a panel that is not on screen while it is closed.
    for (QWidget *tool : statsPanelTools) {
        if (tool != nullptr) tool->setVisible(open);
    }
    if (statsPanelToggle != nullptr) {
        statsPanelToggle->setIcon(MaterialIcon::icon(
            open ? MaterialIcon::Glyph::ChevronDown : MaterialIcon::Glyph::ChevronUp,
            themeManager->Colors().textMuted, 18));
        statsPanelToggle->setToolTip(open ? tr("Hide the panel") : tr("Show logs and connections"));
    }
    settings->stats_panel_open = open;
    if (save) settings->Save();

    // Sizes taken before the first layout pass are meaningless, so redo the split
    // once the window has one. Harmless afterwards: it re-applies what it just set.
    QTimer::singleShot(0, this, [this, open] {
        const QList<int> laidOut = ui->splitter->sizes();
        if (laidOut.size() != 2) return;
        const int total = laidOut[0] + laidOut[1];
        if (total <= 0) return;
        if (open) {
            const int panel = qBound(140, Configs::dataManager->settingsRepo->stats_panel_height,
                                     qMax(140, total - 200));
            ui->splitter->setSizes({total - panel, panel});
        } else {
            // Re-measured now that the tab bar has a real height: sizeHint left a
            // sliver of the page showing below the strip.
            // Exactly the tab bar: any slack shows a sliver of the page under it.
            const int laidOutStrip = ui->stats_widget->tabBar()->height() + 2;
            ui->stats_widget->setMaximumHeight(laidOutStrip);
            ui->splitter->setSizes({total - laidOutStrip, laidOutStrip});
        }
    });
}

void MainWindow::refreshProfileRowStyle() {
    if (profilesTableModel == nullptr) return;
    const bool comfortable = Configs::dataManager->settingsRepo->profile_rows_comfortable;
    if (profileRowDelegate == nullptr) profileRowDelegate = new ProfileRowDelegate(this);
    if (compactRowDelegate == nullptr) compactRowDelegate = new QStyledItemDelegate(this);

    ui->profilesTableView->setItemDelegate(comfortable ? static_cast<QStyledItemDelegate *>(profileRowDelegate)
                                                       : compactRowDelegate);
    profilesTableModel->setRowStyle(comfortable ? ProfilesTableModel::RowStyle::Comfortable
                                                : ProfilesTableModel::RowStyle::Compact);
    ui->profilesTableView->verticalHeader()->setDefaultSectionSize(comfortable ? ProfileRowDelegate::RowHeight : 34);
    if (auto *filterHeader = dynamic_cast<ProfilesTableFilterHeader *>(ui->profilesTableView->horizontalHeader()))
        filterHeader->setRowStyle(comfortable);
    // Widths saved for the other column set would land on the wrong columns.
    if (auto group = Configs::dataManager->groupsRepo->CurrentGroup(); group != nullptr) {
        group->column_width.clear();
        group->clearCalculatedColumnWidth();
    }
    refresh_proxy_list_column_size();
}
