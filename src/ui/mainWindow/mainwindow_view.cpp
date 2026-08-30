#include "include/ui/mainwindow.h"
#include "NkrVersion.h"

#include <QApplication>
#include <QScopeGuard>
#include <QFrame>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QMenu>
#include <QVBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QActionGroup>
#include <QPainter>
#include <QToolButton>
#include <QVariantAnimation>

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
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/stats/dialog_auto_selector.h"
#include <QStyledItemDelegate>

#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/utils/ProfilesFilterProxyModel.h"
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
    if (logLevelActions != nullptr) {
        for (QAction *action : logLevelActions->actions()) {
            const QSignalBlocker blocker(action);
            action->setChecked(action->data().toString() == level);
        }
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

bool MainWindow::searchesEveryGroup() const {
    return Configs::dataManager->settingsRepo->profiles_search_all_groups
        && !globalFilterString.isEmpty();
}

void MainWindow::applyProfileFilters()
{
    if (!profilesFilterModel) return;
    profilesFilterModel->setFilters(typeFilterString, addressFilterString, nameFilterString, countryFilterString);
    profilesFilterModel->setSearch(globalFilterString);
    const bool wide = searchesEveryGroup();
    // A cross-group result set has no single meaningful order.  The drop handler
    // already refuses filtered moves, but disabling the gesture keeps the UI from
    // promising a reorder that can only be ignored.
    ui->profilesTableView->setDragEnabled(
        !Configs::dataManager->settingsRepo->profiles_favorites_view && !wide);
    // Clearing the last character has to put the group back, and typing the first
    // one has to widen: both change which rows the model holds, not just which of
    // them the proxy lets through.
    if (wide != m_searchedEveryGroup) {
        m_searchedEveryGroup = wide;
        refresh_proxy_list({}, true);
        return;
    }
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
    if (icon_status_new != icon_status) QApplication::setWindowIcon(GetTaskbarIcon(icon_status_new));

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
        speedChartWidget->addSample(proxyDl, proxyUp, directDl, directUp);
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
        // Comfortable columns are stretch + fixed, never user-dragged, so a width
        // saved by an older build must not freeze them.
        if (comfortable) {
            group->column_width.clear();
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
        refreshProfilesEmptyState();
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
    } else if (Configs::dataManager->settingsRepo->profiles_favorites_view) {
        profilesTableModel->refreshTable(
            Configs::dataManager->profilesRepo->GetFavoriteProfileIds(), mayNeedReset);
    } else if (searchesEveryGroup()) {
        // The proxy narrows by text; widening the model is what lets the search
        // reach past the group the tab is showing.
        profilesTableModel->refreshTable(
            Configs::dataManager->profilesRepo->GetAllProfileIds(), mayNeedReset);
    } else {
        profilesTableModel->refreshTable(currentGroup->profiles, mayNeedReset);
    }
}

void MainWindow::setFavoritesView(bool on) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    // A hidden button must never leave an invisible cross-group view behind,
    // including after restoring an older or interrupted settings write.
    if (on && !settings->profiles_favorites_button) on = false;
    if (settings->profiles_favorites_view == on && favoritesButton != nullptr
        && favoritesButton->isChecked() == on
        && ui->tabWidget->groupTabBar()->isSelectionVisible() == !on) {
        return;
    }
    settings->profiles_favorites_view = on;
    settings->Save();
    if (favoritesButton != nullptr) {
        const QSignalBlocker blocker(favoritesButton);
        favoritesButton->setChecked(on);
        favoritesButton->setToolTip(on ? tr("Back to the group") : tr("Show favourites"));
    }
    syncGroupTabSelection();
    // Rows here come from several groups, so dragging one would rewrite the order
    // of a group the user cannot even see.
    ui->profilesTableView->setDragEnabled(!on && !searchesEveryGroup());
    refreshFavoritesButtonIcon();
    refresh_proxy_list({}, true);
}

void MainWindow::syncGroupTabSelection() {
    auto *bar = ui->tabWidget->groupTabBar();
    if (bar == nullptr) return;
    bar->setSelectionVisible(
        !Configs::dataManager->settingsRepo->profiles_favorites_view);
}

void MainWindow::refreshProfilesEmptyState() {
    if (profilesEmptyState == nullptr || profilesFilterModel == nullptr) return;
    profilesEmptyState->setGeometry(ui->profilesTableView->viewport()->rect());
    if (profilesFilterModel->rowCount() > 0) {
        profilesEmptyState->hide();
        return;
    }

    // A short panel used to squeeze every part instead of dropping any, which
    // flattened the icon into a smear and the button into a sliver. Shed the
    // decorative parts first and keep the sentence that says what to do.
    const int room = ui->profilesTableView->viewport()->height();
    const bool showIcon = room >= 200;
    const bool showSub = room >= 132;
    profilesEmptyIcon->setVisible(showIcon);
    profilesEmptySub->setVisible(showSub);
    if (profilesEmptyAction != nullptr) profilesEmptyAction->setVisible(room >= 96);
    if (auto *column = qobject_cast<QVBoxLayout *>(profilesEmptyState->layout())) {
        // Indices follow the order the layout was built in: the spacers only earn
        // their height while the widget they follow is on screen.
        const auto setSpacer = [column](int index, int height) {
            if (QSpacerItem *spacer = column->itemAt(index) ? column->itemAt(index)->spacerItem() : nullptr)
                spacer->changeSize(0, height, QSizePolicy::Minimum, QSizePolicy::Fixed);
        };
        setSpacer(2, showIcon ? 14 : 0);
        setSpacer(4, showSub ? 6 : 0);
        setSpacer(6, showSub ? 16 : 12);
        column->invalidate();
    }
    const auto colors = themeManager->Colors();
    const bool favorites = Configs::dataManager->settingsRepo->profiles_favorites_view;
    const bool searching = !globalFilterString.isEmpty();
    const int hidden = profilesTableModel != nullptr ? profilesTableModel->rowCount() : 0;
    if (profilesEmptyAction != nullptr) profilesEmptyAction->setVisible(!searching && !favorites);

    MaterialIcon::Glyph glyph = MaterialIcon::Glyph::Apps;
    QString title;
    QString sub;
    if (searching) {
        glyph = MaterialIcon::Glyph::Search;
        title = tr("Nothing matches “%1”").arg(globalFilterString);
        sub = hidden > 0 ? tr("All %n server(s) are still here — the search is hiding them.", "", hidden)
                         : tr("Clear the search to see the list again.");
    } else if (favorites) {
        glyph = MaterialIcon::Glyph::StarOutline;
        title = tr("No favourites yet");
        sub = tr("Right-click a server and add it to favourites to keep it here, from any group.");
    } else {
        title = tr("No servers in this group");
        sub = tr("Paste a subscription link to fetch them, or add one server by hand.");
    }
    profilesEmptyIcon->setPixmap(MaterialIcon::pixmap(glyph, colors.textSubtle, 40));
    if (profilesEmptyAction != nullptr)
        profilesEmptyAction->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Add, colors.text, 17));
    profilesEmptyTitle->setText(title);
    profilesEmptySub->setText(sub);
    // Adding a widget with an alignment flag makes the layout size it from
    // sizeHint() and skip heightForWidth(), so a wrapped sentence was cut after
    // its first line. The width is fixed, so the height it needs is knowable.
    profilesEmptySub->setMinimumHeight(profilesEmptySub->heightForWidth(profilesEmptySub->width()));
    profilesEmptyState->show();
    profilesEmptyState->raise();
}

void MainWindow::setFavoritesButtonVisible(bool on) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    settings->profiles_favorites_button = on;
    settings->Save();
    if (favoritesButton != nullptr) favoritesButton->setVisible(on);
    // Staying in a view whose only indicator just disappeared would read as the
    // group having lost most of its servers.
    if (!on && settings->profiles_favorites_view) setFavoritesView(false);
}

void MainWindow::applyProfileColumnVisibility() {
    if (profilesTableModel == nullptr) return;
    auto *view = ui->profilesTableView;
    const auto *settings = Configs::dataManager->settingsRepo.get();
    if (profilesTableModel->rowStyle() != ProfilesTableModel::RowStyle::Comfortable) {
        // Compact columns are not configurable here, and a hidden index survives a
        // style switch, so anything hidden earlier has to come back.
        for (int column = 0; column < ProfilesTableModel::ColumnCount; ++column)
            view->setColumnHidden(column, false);
        return;
    }
    view->setColumnHidden(ProfilesTableModel::ColcPing, !settings->profiles_show_ping);
    view->setColumnHidden(ProfilesTableModel::ColcSpeed, !settings->profiles_show_speed);
    view->setColumnHidden(ProfilesTableModel::ColcTraffic, !settings->profiles_show_traffic);
    refresh_proxy_list_column_size();
}

void MainWindow::addProfileColumnsMenu(QMenu &menu) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    QMenu *columns = menu.addMenu(tr("Columns"));
    const QList<QPair<QString, bool *>> entries{
        {tr("Ping · UDP"), &settings->profiles_show_ping},
        {tr("Speed"), &settings->profiles_show_speed},
        {tr("Traffic"), &settings->profiles_show_traffic},
    };
    for (const auto &[label, flag] : entries) {
        QAction *action = columns->addAction(label);
        action->setCheckable(true);
        action->setChecked(*flag);
        connect(action, &QAction::triggered, this, [this, flag](bool on) {
            *flag = on;
            Configs::dataManager->settingsRepo->Save();
            applyProfileColumnVisibility();
        });
    }
}

void MainWindow::addFavoritesButtonAction(QMenu &menu) {
    auto *action = menu.addAction(tr("Favourites button"));
    action->setCheckable(true);
    action->setChecked(Configs::dataManager->settingsRepo->profiles_favorites_button);
    connect(action, &QAction::triggered, this, [this](bool on) { setFavoritesButtonVisible(on); });
}

void MainWindow::refreshFavoritesButtonIcon() {
    if (favoritesButton == nullptr) return;
    const auto colors = themeManager->Colors();
    favoritesButton->setIcon(MaterialIcon::icon(
        MaterialIcon::Glyph::Star,
        favoritesButton->isChecked() ? colors.accent : colors.textMuted, 18));
}

void MainWindow::toggleFavorite(const QList<int> &ids) {
    if (ids.isEmpty()) return;
    // One click sets them all the same way; mixed selections turn on.
    bool allFavorite = true;
    for (const int id : ids) {
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
        if (profile != nullptr && !profile->favorite) { allFavorite = false; break; }
    }
    for (const int id : ids) {
        auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
        if (profile == nullptr) continue;
        profile->favorite = !allFavorite;
        Configs::dataManager->profilesRepo->Save(profile);
    }
    // Un-starring inside the favourites view removes the row, so rebuild the list.
    if (Configs::dataManager->settingsRepo->profiles_favorites_view) refresh_proxy_list({}, true);
    else refresh_proxy_list(ids, false);
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

void MainWindow::revealRunningProfile() {
    const int id = Configs::dataManager->settingsRepo->started_id;
    if (id < 0 || profilesTableModel == nullptr || profilesFilterModel == nullptr) return;
    const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
    if (profile == nullptr) return;

    // It may be running out of a group the table is not showing.
    if (profile->gid != Configs::dataManager->settingsRepo->current_group) {
        const int tab = groupId2TabIndex(profile->gid);
        if (tab < 0) return;
        ui->tabWidget->setCurrentIndex(tab);
    }
    // ...and a search may be hiding it, in which case scrolling to nothing reads
    // as the click having missed.
    if (profilesFilterModel->toProxyRow(profilesTableModel->indexOfProfile(id)) < 0) {
        if (auto *search = findChild<QLineEdit *>(QStringLiteral("serverSearch"))) search->clear();
        globalFilterString.clear();
        applyProfileFilters();
    }

    const int sourceRow = profilesTableModel->indexOfProfile(id);
    if (sourceRow < 0) return;
    const int proxyRow = profilesFilterModel->toProxyRow(sourceRow);
    if (proxyRow < 0) return;

    auto *view = ui->profilesTableView;
    const QModelIndex target = profilesFilterModel->index(proxyRow, ProfilesTableModel::ColcServer);
    view->setCurrentIndex(target);
    view->selectRow(proxyRow);
    view->scrollTo(target, QAbstractItemView::PositionAtCenter);
    view->setFocus();
    flashProfileRow(proxyRow);
}

void MainWindow::flashProfileRow(int proxyRow) {
    if (profileRowDelegate == nullptr) return;
    if (rowFlashAnimation != nullptr) {
        rowFlashAnimation->stop();
        rowFlashAnimation->deleteLater();
        rowFlashAnimation = nullptr;
    }
    auto *animation = new QVariantAnimation(this);
    rowFlashAnimation = animation;
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    animation->setDuration(900);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this, [this, proxyRow](const QVariant &value) {
        Q_UNUSED(proxyRow); Q_UNUSED(value);
        ui->profilesTableView->viewport()->update();
    });
    connect(animation, &QVariantAnimation::finished, this, [this, animation] {
        if (rowFlashAnimation != animation) return;
        rowFlashAnimation = nullptr;

        ui->profilesTableView->viewport()->update();
        animation->deleteLater();
    });
    animation->start();
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

void MainWindow::updateStatsPanelChevron(qreal openProgress) {
    statsPanelOpenProgress = qBound(0.0, openProgress, 1.0);
    const auto colors = themeManager->Colors();
    const QPixmap glyph = MaterialIcon::pixmap(MaterialIcon::Glyph::ChevronUp,
                                               colors.textMuted, 16);
    // The canvas has to carry the same ratio as the glyph, or rotating it here
    // resamples an already-scaled bitmap a second time.
    const qreal ratio = glyph.devicePixelRatio();
    const QSizeF glyphSize = glyph.deviceIndependentSize();
    QPixmap canvas(QSize(20, 20) * ratio);
    canvas.setDevicePixelRatio(ratio);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(10.0, 10.0);
    painter.rotate(180.0 * statsPanelOpenProgress);
    painter.drawPixmap(QPointF(-glyphSize.width() / 2.0, -glyphSize.height() / 2.0), glyph);
    painter.end();
    const QIcon icon(canvas);
    if (statsStripToggle != nullptr) statsStripToggle->setIcon(icon);
    if (statsPanelToggle != nullptr) statsPanelToggle->setIcon(icon);
}

void MainWindow::setStatsPanelOpen(bool open, bool save) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    QWidget *panel = statsPanelHost != nullptr ? statsPanelHost : ui->stats_widget;
    const QList<int> current = ui->splitter->sizes();
    if (!open && current.size() == 2 && panel->isVisible() && current[1] > 60)
        settings->stats_panel_height = current[1];

    if (statsPanelAnimation != nullptr) {
        statsPanelAnimation->stop();
        statsPanelAnimation->deleteLater();
        statsPanelAnimation = nullptr;
    }

    settings->stats_panel_open = open;
    if (statsPanelToggle != nullptr)
        statsPanelToggle->setToolTip(open ? tr("Hide the panel") : tr("Show logs and connections"));
    refreshStatsPanelTools();
    if (save) settings->Save();

    const auto colors = themeManager->Colors();
    if (auto *tools = ui->stats_widget->cornerWidget(Qt::TopRightCorner))
        for (auto *menuButton : tools->findChildren<QToolButton *>(QStringLiteral("panelIconButton")))
            if (menuButton->menu() != nullptr)
                menuButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::More,
                                                       colors.textMuted, 18));

    if (statsStrip == nullptr || panel == nullptr) {
        updateStatsPanelChevron(open ? 1.0 : 0.0);
        return;
    }

    const auto applyOpenSplit = [this] {
        const QList<int> sizes = ui->splitter->sizes();
        if (sizes.size() != 2) return;
        const int total = sizes[0] + sizes[1];
        if (total <= 0) return;
        const int panelHeight = qBound(140, Configs::dataManager->settingsRepo->stats_panel_height,
                                       qMax(140, total - 200));
        ui->splitter->setSizes({total - panelHeight, panelHeight});
    };

    // Restore, theme changes and screenshot mode deliberately stay immediate.
    // User toggles get the short Smart-Animate-like handoff between the two
    // direct layout children; this avoids animating application startup.
    const bool animate = save && isVisible() && ui->splitter->height() > 0;
    if (!animate) {
        panel->setMinimumHeight(0);
        panel->setMaximumHeight(QWIDGETSIZE_MAX);
        statsStrip->setFixedHeight(statsStripHeight);
        if (open) {
            ui->stats_widget->show();
            panel->show();
            statsStrip->hide();
            QTimer::singleShot(0, this, applyOpenSplit);
        } else {
            panel->hide();
            statsStrip->show();
        }
        updateStatsPanelChevron(open ? 1.0 : 0.0);
        return;
    }

    const QList<int> startSizes = ui->splitter->sizes();
    const int startTotal = startSizes.size() == 2
        ? startSizes[0] + startSizes[1] : ui->splitter->height();
    const int targetPanel = qBound(140, settings->stats_panel_height,
                                   qMax(140, startTotal - 200));
    int panelStart = panel->isVisible() && startSizes.size() == 2 ? startSizes[1] : 0;
    int stripStart = statsStrip->isVisible() ? statsStrip->height() : 0;
    const int panelEnd = open ? targetPanel : 0;
    const int stripEnd = open ? 0 : statsStripHeight;
    const qreal progressStart = statsPanelOpenProgress;
    const qreal progressEnd = open ? 1.0 : 0.0;

    panel->setMinimumHeight(0);
    panel->setMaximumHeight(qMax(0, panelStart));
    ui->stats_widget->show();
    panel->show();
    statsStrip->setMinimumHeight(0);
    statsStrip->setMaximumHeight(qMax(0, stripStart));
    statsStrip->show();

    auto *animation = new QVariantAnimation(this);
    statsPanelAnimation = animation;
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(190);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this,
            [this, panel, panelStart, panelEnd, stripStart, stripEnd,
             progressStart, progressEnd](const QVariant &value) {
        const qreal t = value.toReal();
        const int panelHeight = qRound(panelStart + (panelEnd - panelStart) * t);
        const int stripHeight = qRound(stripStart + (stripEnd - stripStart) * t);
        panel->setMaximumHeight(qMax(0, panelHeight));
        statsStrip->setMaximumHeight(qMax(0, stripHeight));
        const int total = ui->splitter->height();
        if (total > 0)
            ui->splitter->setSizes({qMax(0, total - panelHeight), qMax(0, panelHeight)});
        updateStatsPanelChevron(progressStart + (progressEnd - progressStart) * t);
    });
    connect(animation, &QVariantAnimation::finished, this,
            [this, animation, panel, open, applyOpenSplit] {
        if (statsPanelAnimation != animation) return;
        statsPanelAnimation = nullptr;
        panel->setMinimumHeight(0);
        panel->setMaximumHeight(QWIDGETSIZE_MAX);
        statsStrip->setFixedHeight(statsStripHeight);
        if (open) {
            panel->show();
            statsStrip->hide();
            applyOpenSplit();
        } else {
            panel->hide();
            statsStrip->show();
        }
        updateStatsPanelChevron(open ? 1.0 : 0.0);
        animation->deleteLater();
    });
    animation->start();
}

void MainWindow::refreshStatsPanelTools() {
    const bool open = Configs::dataManager->settingsRepo->stats_panel_open;
    const QString currentPage = ui->stats_widget->currentWidget() != nullptr
        ? ui->stats_widget->currentWidget()->objectName() : QString();
    for (QWidget *tool : statsPanelTools) {
        if (tool == nullptr) continue;
        const QString page = tool->property("statsPage").toString();
        tool->setVisible(open && (page.isEmpty() || page == currentPage));
    }
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
    applyProfileColumnVisibility();
    // Widths saved for the other column set would land on the wrong columns.
    if (auto group = Configs::dataManager->groupsRepo->CurrentGroup(); group != nullptr) {
        group->column_width.clear();
        group->clearCalculatedColumnWidth();
    }
    refresh_proxy_list_column_size();
}
