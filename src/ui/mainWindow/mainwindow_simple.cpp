#include "include/ui/mainwindow.h"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/mainWindow/TestRunner.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "include/configs/sub/SubInfo.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/widget/HoverMarqueeLabel.h"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/RoutingQuickMenu.hpp"
#include "include/ui/widget/SimpleModeControls.h"
#include "include/ui/widget/SimpleModeNotice.h"
#include "include/ui/widget/SimpleRoutesPage.h"
#include "include/ui/widget/SimpleSheet.h"
#include "include/ui/widget/StartStopButton.hpp"
#include "include/ui/widget/SubscriptionPopover.hpp"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowChrome.h"
#include "include/ui/widget/UpdateStatusWidget.h"

namespace {
struct Allowance {
    QString left;
    QString summary;
    qreal fraction = -1;
};

Allowance allowanceOf(const std::shared_ptr<Configs::Group> &group) {
    Allowance allowance;
    if (!group || group->url.trimmed().isEmpty()) return allowance;
    const auto info = Configs::ParseSubInfo(group->info);
    if (!info.valid) return allowance;
    QStringList parts;
    if (info.total > 0) {
        allowance.left = MainWindow::tr("%1 left").arg(ReadableSize(qMax<qint64>(0, info.total - info.used())));
        allowance.fraction = 1.0 - info.usedFraction();
        parts << allowance.left;
    } else {
        parts << MainWindow::tr("Unlimited traffic");
    }
    if (info.expire > 0) {
        parts << (info.expire <= QDateTime::currentSecsSinceEpoch()
                      ? MainWindow::tr("Expired")
                      : MainWindow::tr("Until %1").arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(info.expire).date(), QLocale::ShortFormat)));
    }
    allowance.summary = parts.join(QStringLiteral(" · "));
    return allowance;
}

QString latencyText(const std::shared_ptr<Configs::Profile> &profile) {
    if (profile->latency == Configs::kLatencyConnectOnly) return MainWindow::tr("Connect OK");
    if (profile->latency < 0) return MainWindow::tr("Unavailable");
    if (profile->latency > 0) return MainWindow::tr("%1 ms").arg(profile->latency);
    return QStringLiteral("—");
}

QString elapsed(qint64 seconds) {
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

SimpleMode currentSimpleMode() {
    const auto *settings = Configs::dataManager->settingsRepo.get();
    if (settings->spmode_vpn) return SimpleMode::WholeComputer;
    return settings->spmode_system_proxy ? SimpleMode::BrowserOnly : SimpleMode::Off;
}
} // namespace

void MainWindow::setupSimpleMode() {
    auto *root = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
    fullModeContent = new QWidget(centralWidget());
    auto *full = new QVBoxLayout(fullModeContent);
    full->setContentsMargins(0, 0, 0, 0);
    full->setSpacing(0);
    while (root->count() > 1) {
        auto *item = root->takeAt(1);
        auto *widget = item->widget();
        full->addWidget(widget, widget->objectName() == QStringLiteral("body") ? 1 : 0);
        delete item;
    }
    root->addWidget(fullModeContent, 1);
    simpleModeContent = new QWidget(centralWidget());
    simpleModeContent->setObjectName(QStringLiteral("simpleMode"));
    simpleModeContent->setFocusPolicy(Qt::ClickFocus);
    auto *outer = new QVBoxLayout(simpleModeContent);
    outer->setContentsMargins(20, 12, 20, 16);
    outer->setSpacing(10);
    simplePages = new QStackedWidget(simpleModeContent);
    outer->addWidget(simplePages, 1);
    auto *mainPage = new QWidget(simplePages);
    mainPage->setObjectName(QStringLiteral("simpleMainPage"));
    auto *layout = new QVBoxLayout(mainPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    simplePages->addWidget(mainPage);

    simpleModeAction = new QAction(tr("Simple mode"), this);
    simpleModeAction->setObjectName(QStringLiteral("simpleModeAction"));
    simpleModeAction->setCheckable(true);
    ui->menu_program->insertAction(ui->menu_program->actions().value(0), simpleModeAction);
    connect(simpleModeAction, &QAction::toggled, this, [this](bool simple) { setSimpleMode(simple); });
    // One caption button toggles both ways, where window-level view controls already live.
    if (auto *titleBar = dynamic_cast<ThronedTitleBar *>(findChild<QFrame *>(QStringLiteral("titleBar")))) {
        simpleModeToggle = titleBar->insertCaptionButton(ThronedCaptionButton::Glyph::Compact, QStringLiteral("titleSimpleMode"));
        ThronedChrome::setInteractive(this, simpleModeToggle);
        connect(simpleModeToggle, &QToolButton::clicked, simpleModeAction, &QAction::toggle);
    }
    simpleModeNotice = new SimpleModeNotice(updateStatusWidget, *Configs::dataManager->settingsRepo, [this] { setSimpleMode(true); });

    auto *hero = new QWidget(mainPage);
    hero->setObjectName(QStringLiteral("simpleHero"));
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(4);
    heroLayout->addStretch(1);
    auto *power = new SimpleConnectButton(hero);
    power->setObjectName(QStringLiteral("simpleConnect"));
    power->setFixedSize(148, 148);
    heroLayout->addWidget(power, 0, Qt::AlignHCenter);
    heroLayout->addSpacing(8);
    auto *state = new QLabel(hero);
    state->setObjectName(QStringLiteral("simpleState"));
    state->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(state);
    auto *substate = new QLabel(hero);
    substate->setObjectName(QStringLiteral("simpleSubstate"));
    substate->setAlignment(Qt::AlignCenter);
    substate->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    substate->installEventFilter(this);
    statusElidedLabels.append(substate);
    heroLayout->addWidget(substate);
    heroLayout->addStretch(1);
    layout->addWidget(hero, 1);
    connect(power, &QToolButton::clicked, this, [this] {
        if (Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview"))) return;
        if (running != nullptr)
            ui->menu_stop->trigger();
        else if (simpleProfileId >= 0)
            profile_start(simpleProfileId);
    });

    // The provider's notice explains outages, so it cannot be left behind in the full interface.
    auto *announce = new QFrame(mainPage);
    announce->setObjectName(QStringLiteral("subAnnounceStrip"));
    announce->setProperty("simpleAnnounce", true);
    auto *announceLayout = new QHBoxLayout(announce);
    announceLayout->setContentsMargins(10, 6, 6, 6);
    announceLayout->setSpacing(8);
    auto *announceIconLabel = new QLabel(announce);
    announceIconLabel->setFixedSize(16, 16);
    announceLayout->addWidget(announceIconLabel);
    auto *announceLabel = new HoverMarqueeLabel(announce);
    announceLabel->setObjectName(QStringLiteral("subAnnounceText"));
    announceLabel->setTextFormat(Qt::PlainText);
    announceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    announceLabel->installEventFilter(this);
    statusElidedLabels.append(announceLabel);
    announceLayout->addWidget(announceLabel, 1);
    auto *announceDismiss = new QToolButton(announce);
    announceDismiss->setObjectName(QStringLiteral("subAnnounceClose"));
    announceDismiss->setCursor(Qt::PointingHandCursor);
    announceDismiss->setFixedSize(18, 18);
    announceDismiss->setIconSize(QSize(12, 12));
    announceDismiss->setToolTip(tr("Dismiss until the provider changes it"));
    connect(announceDismiss, &QToolButton::clicked, this, &MainWindow::dismissAnnounce);
    announceLayout->addWidget(announceDismiss);
    const auto tintAnnounce = [announceIconLabel, announceDismiss] {
        const auto colors = themeManager()->Colors();
        announceIconLabel->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Campaign, colors.accent, 16));
        announceDismiss->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Close, colors.textSubtle, 12));
    };
    tintAnnounce();
    connect(themeManager(), &ThemeManager::themeChanged, this, tintAnnounce);
    announce->hide();
    simpleAnnounce = announce;
    layout->addWidget(announce);

    auto *server = new SimpleServerCard(mainPage);
    server->setObjectName(QStringLiteral("simpleServer"));
    connect(server, &QAbstractButton::clicked, this, &MainWindow::openSimpleServerSheet);
    layout->addWidget(server);

    auto *options = new QFrame(mainPage);
    options->setObjectName(QStringLiteral("simpleOptions"));
    auto *optionsLayout = new QVBoxLayout(options);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(0);
    auto *modeRow = new SimpleListRow(options);
    modeRow->setObjectName(QStringLiteral("simpleModeRow"));
    modeRow->setLabel(tr("Through the VPN"));
    modeRow->setFirst(true);
    connect(modeRow, &QAbstractButton::clicked, this, &MainWindow::openSimpleModeSheet);
    optionsLayout->addWidget(modeRow);
    auto *routesRow = new SimpleListRow(options);
    routesRow->setObjectName(QStringLiteral("simpleRoutesRow"));
    routesRow->setLabel(tr("Routing"));
    connect(routesRow, &QAbstractButton::clicked, this, &MainWindow::openSimpleRoutes);
    optionsLayout->addWidget(routesRow);
    layout->addWidget(options);

    auto *traffic = new SimpleTrafficLine(mainPage);
    traffic->setObjectName(QStringLiteral("simpleTraffic"));
    layout->addWidget(traffic);

    setupSimpleRoutes();

    simpleServerSheet = new SimpleServerSheet(simpleModeContent);
    connect(simpleServerSheet, &SimpleServerSheet::groupShown, this, &MainWindow::refreshSimpleServerSheet);
    connect(simpleServerSheet, &SimpleServerSheet::serverChosen, this, [this](int id) {
        selectSimpleProfile(id);
        simpleServerSheet->dismiss();
    });
    connect(simpleServerSheet, &SimpleServerSheet::testRequested, this, [this] {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(simpleServerSheet->shownGroup());
        if (!group || simplePingRunning || Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview"))) return;
        QList<int> ids;
        for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles()))
            if (profile != nullptr && profile->type != QStringLiteral("autoselector")) ids.append(profile->id);
        if (ids.isEmpty()) return;
        simplePingRunning = true;
        simpleServerSheet->setTesting(true, true);
        const QPointer<MainWindow> guard(this);
        testRunner->runUrlTests(ids, [guard] {
            if (!guard) return;
            QMetaObject::invokeMethod(guard.data(), [guard] {
                if (!guard) return;
                guard->simplePingRunning = false;
                guard->refreshSimpleServerSheet(guard->simpleServerSheet->shownGroup());
                guard->refreshSimpleStatus(); }, Qt::QueuedConnection);
        });
    });
    connect(simpleServerSheet, &SimpleServerSheet::addFromClipboard, this, [this] {
        simpleServerSheet->dismiss();
        on_menu_add_from_clipboard_triggered();
    });
    connect(simpleServerSheet, &SimpleServerSheet::addFromLink, this, [this] {
        simpleServerSheet->dismiss();
        bool accepted = false;
        const QString text = QInputDialog::getText(this, tr("Add profiles"), tr("Profile or subscription link"),
                                                   QLineEdit::Normal, {}, &accepted)
                                 .trimmed();
        if (accepted && !text.isEmpty()) import_or_handle_deeplink(text);
    });
    connect(simpleServerSheet, &SimpleServerSheet::allowanceClicked, this, [this](const QPoint &pos) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(simpleServerSheet->shownGroup());
        if (group && !group->url.isEmpty()) subscriptionCard()->popupFor(group, pos);
    });
    simpleModeSheet = new SimpleModeSheet(simpleModeContent);
    connect(simpleModeSheet, &SimpleModeSheet::modeChosen, this,
            [this](SimpleMode mode, bool confirmed) { chooseSimpleMode(static_cast<int>(mode), confirmed); });

    root->addWidget(simpleModeContent, 1);
    simpleModeContent->hide();
    themeManager()->RegisterStyle(simpleModeContent, QStringLiteral(R"(
QWidget#simpleMode { background: transparent; }
QWidget#simpleMode QLabel { background: transparent; }
QLabel#simpleState { font-size: 19px; font-weight: 600; color: #F1F3F5; }
QLabel#simpleSubstate { color: #A4ABB4; }
QFrame#simpleOptions { background: #222529; border: 1px solid #2F3136; border-radius: 14px; }
)"));

    auto *clock = new QTimer(simpleModeContent);
    clock->setObjectName(QStringLiteral("simpleClock"));
    clock->setInterval(1000);
    connect(clock, &QTimer::timeout, this, &MainWindow::refreshSimpleStatus);
    auto *refreshTimer = new QTimer(this);
    refreshTimer->setSingleShot(true);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::refreshSimpleProfiles);
    const auto refresh = [refreshTimer] { refreshTimer->start(0); };
    connect(profilesTableModel, &QAbstractItemModel::modelReset, this, refresh);
    connect(profilesTableModel, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(profilesTableModel, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(profilesTableModel, &QAbstractItemModel::dataChanged, this, refresh);
    connect(ui->checkBox_VPN, &QCheckBox::toggled, this, &MainWindow::refreshSimpleStatus);
    connect(ui->checkBox_SystemProxy, &QCheckBox::toggled, this, &MainWindow::refreshSimpleStatus);
    refreshSimpleProfiles();
    applyTopBarMetrics();
    auto *settings = Configs::dataManager->settingsRepo.get();
    if (settings->argv.contains(QStringLiteral("-ui-preview")))
        settings->simple_mode = false;
    else if (settings->simple_mode)
        setSimpleMode(true, false);
    refreshSimpleModeToggle();
}

void MainWindow::refreshSimpleModeToggle() {
    if (simpleModeToggle != nullptr) {
        simpleModeToggle->setGlyph(simpleModeActive ? ThronedCaptionButton::Glyph::Expand : ThronedCaptionButton::Glyph::Compact);
        const QString text = simpleModeActive ? tr("Full interface") : tr("Simple mode");
        simpleModeToggle->setToolTip(text);
        simpleModeToggle->setAccessibleName(text);
    }
    simpleModeNotice->refresh();
}

void MainWindow::setSimpleMode(bool simple, bool save) {
    if (simpleModeContent == nullptr || simpleModeActive == simple) return;
    if (subscriptionPopover != nullptr) subscriptionPopover->hide();
    simpleServerSheet->dismiss();
    simpleModeSheet->dismiss();
    simplePages->setCurrentIndex(0);
    auto *settings = Configs::dataManager->settingsRepo.get();
    const int selectedProfile = get_profile_to_start();
    const QString geometry = saveGeometry().toBase64();
    if (simpleModeActive)
        settings->simple_window_geometry = geometry;
    else
        settings->mainWindowGeometry = geometry;
    simpleModeActive = simple;
    settings->simple_mode = simple;
    const QSignalBlocker blocker(simpleModeAction);
    simpleModeAction->setChecked(simple);
    fullModeContent->setVisible(!simple);
    simpleModeContent->setVisible(simple);
    auto *fullLayout = qobject_cast<QVBoxLayout *>(fullModeContent->layout());
    if (simple) {
        fullLayout->removeWidget(updateStatusWidget);
        simpleModeContent->layout()->addWidget(updateStatusWidget);
    } else {
        simpleModeContent->layout()->removeWidget(updateStatusWidget);
        fullLayout->insertWidget(fullLayout->count() - 1, updateStatusWidget);
    }
    const bool footerVisible = updateStatusWidget->state() != UpdateStatusWidget::State::Hidden;
    updateStatusWidget->setCompact(simple);
    updateStatusWidget->setVisible(footerVisible);
    setMinimumSize(simple ? QSize(360, 560) : designMinimumSize);
    centralWidget()->layout()->invalidate();
    if (isMaximized()) showNormal();
    const QString target = simple ? settings->simple_window_geometry : settings->mainWindowGeometry;
    if (target.isEmpty() || !restoreGeometry(QByteArray::fromBase64(target.toUtf8())))
        resize(simple ? QSize(380, 680) : QSize(1180, 780));
    if (simple) {
        simpleProfileId = selectedProfile;
        refreshSimpleProfiles();
        updateSimpleDensity();
        applyWindowMinimum();
    } else {
        applyTopBarMetrics();
        refresh_proxy_list_column_size();
    }
    FitWindowToScreen(this);
    if (save) settings->Save();
    refreshSimpleModeToggle();
}

void MainWindow::updateSimpleDensity() {
    if (simpleModeContent == nullptr) return;
    // Below this height the button yields room so the lower controls never scroll.
    const int side = height() < 620 ? 116 : 148;
    if (auto *power = simpleModeContent->findChild<StartStopButton *>(QStringLiteral("simpleConnect")); power && power->width() != side)
        power->setFixedSize(side, side);
}

void MainWindow::selectSimpleProfile(int profileId) {
    const auto profile = Configs::dataManager->profilesRepo->GetProfile(profileId);
    if (profile == nullptr) return;
    if (profile->gid != Configs::dataManager->settingsRepo->current_group) {
        ui->tabWidget->setCurrentIndex(groupId2TabIndex(profile->gid));
        refreshSimpleProfiles();
    }
    simpleProfileId = profileId;
    const int proxyRow = profilesFilterModel->toProxyRow(profilesTableModel->indexOfProfile(profileId));
    if (proxyRow >= 0) selectProfileRows({proxyRow});
    if (running != nullptr && running->id != profileId &&
        !Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview")))
        profile_start(profileId);
    refreshSimpleStatus();
}

void MainWindow::refreshSimpleProfiles() {
    if (simpleModeContent == nullptr || !simpleModeActive) return;
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    const QList<int> ids = group ? group->Profiles() : QList<int>();
    if (!ids.contains(simpleProfileId)) {
        const int fallback = running != nullptr && ids.contains(running->id) ? running->id : ids.value(0, -1);
        simpleProfileId = fallback;
    }
    if (simpleServerSheet->isOpen()) refreshSimpleServerSheet(simpleServerSheet->shownGroup());
    refreshAnnounceStrip();
    refreshSimpleStatus();
}

void MainWindow::refreshSimpleSubscription() {
    if (simpleModeContent == nullptr || !simpleModeActive) return;
    if (simpleServerSheet->isOpen()) refreshSimpleServerSheet(simpleServerSheet->shownGroup());
    refreshSimpleStatus();
}

void MainWindow::openSimpleServerSheet() {
    refreshSimpleServerSheet(Configs::dataManager->settingsRepo->current_group);
    simpleServerSheet->open();
}

void MainWindow::refreshSimpleServerSheet(int groupId) {
    QList<QPair<int, QString>> groups;
    for (int index = 0; index < ui->tabWidget->count(); ++index)
        groups.append({tabIndex2GroupId(index), ui->tabWidget->tabText(index)});
    simpleServerSheet->setGroups(groups, groupId);
    const auto group = Configs::dataManager->groupsRepo->GetGroup(groupId);
    QList<SimpleServerEntry> servers;
    bool testable = false;
    if (group) {
        for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
            if (profile == nullptr || profile->outbound == nullptr) continue;
            testable = testable || profile->type != QStringLiteral("autoselector");
            servers.append({profile->id, profile->outbound->DisplayName(), profile->outbound->DisplayAddress(),
                            latencyText(profile), profile->latency, profile->id == simpleProfileId});
        }
    }
    simpleServerSheet->setServers(servers);
    const Allowance allowance = allowanceOf(group);
    simpleServerSheet->setAllowance(allowance.summary, allowance.fraction);
    simpleServerSheet->setTesting(simplePingRunning, testable);
}

void MainWindow::openSimpleModeSheet() {
    const auto *settings = Configs::dataManager->settingsRepo.get();
    QString note;
#if defined(Q_OS_WIN) && !defined(NKR_ELEVATION_HINT)
    if (!settings->disable_privilege_req && !Configs::IsAdmin())
        note = tr("Throned restarts with administrator rights for this, and Windows asks for permission. Choose Yes.");
#endif
    simpleModeSheet->setState(currentSimpleMode(), !settings->disable_mixed_inbound, note);
    simpleModeSheet->open();
}

void MainWindow::chooseSimpleMode(int mode, bool elevationConfirmed) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    if (static_cast<SimpleMode>(mode) == SimpleMode::WholeComputer) {
        set_spmode_vpn(true, true, elevationConfirmed);
        // Declined, or restarting elevated: the proxy stays until TUN is really on.
        if (!settings->spmode_vpn) {
            refreshSimpleStatus();
            return;
        }
        if (settings->spmode_system_proxy) set_spmode_system_proxy(false);
    } else {
        if (settings->spmode_vpn) set_spmode_vpn(false);
        if (!settings->spmode_system_proxy) set_spmode_system_proxy(true);
    }
    simpleModeSheet->dismiss();
    refresh_status();
    refreshSimpleStatus();
}

void MainWindow::refreshSimpleStatus() {
    auto *clock = simpleModeContent != nullptr ? simpleModeContent->findChild<QTimer *>(QStringLiteral("simpleClock")) : nullptr;
    if (clock == nullptr) return;
    auto *power = simpleModeContent->findChild<StartStopButton *>(QStringLiteral("simpleConnect"));
    const auto selected = simpleProfileId < 0 ? nullptr : Configs::dataManager->profilesRepo->GetProfile(simpleProfileId);
    auto state = ui->toolButton_startstop->state();
    if (state == StartStopButton::State::Idle || state == StartStopButton::State::Disabled)
        state = selected != nullptr ? StartStopButton::State::Idle : StartStopButton::State::Disabled;
    power->setState(state);
    power->setAccessibleName(state == StartStopButton::State::Running ? tr("Disconnect") : tr("Connect"));

    if (state == StartStopButton::State::Running) {
        if (simpleConnectedSince == 0) simpleConnectedSince = QDateTime::currentSecsSinceEpoch();
        if (!clock->isActive() && simpleModeActive) clock->start();
    } else {
        simpleConnectedSince = 0;
        clock->stop();
    }

    const auto shown = running != nullptr ? running : selected;
    QString status, detail;
    switch (state) {
        case StartStopButton::State::Running:
            status = tr("Connected");
            detail = elapsed(QDateTime::currentSecsSinceEpoch() - simpleConnectedSince);
            if (shown != nullptr) detail += QStringLiteral(" · ") + shown->outbound->DisplayAddress();
            break;
        case StartStopButton::State::Connecting:
            status = tr("Connecting…");
            detail = shown ? shown->outbound->DisplayName() : QString();
            break;
        case StartStopButton::State::Disconnecting:
            status = tr("Disconnecting…");
            break;
        case StartStopButton::State::Idle:
            status = tr("Not connected");
            detail = tr("Press the button to connect");
            break;
        case StartStopButton::State::Disabled:
            status = tr("No profiles yet");
            detail = tr("Add a profile to get started");
            break;
    }
    simpleModeContent->findChild<QLabel *>(QStringLiteral("simpleState"))->setText(status);
    setStatusText(simpleModeContent->findChild<QLabel *>(QStringLiteral("simpleSubstate")), detail);

    const auto colors = themeManager()->Colors();
    auto *server = simpleModeContent->findChild<SimpleServerCard *>(QStringLiteral("simpleServer"));
    if (shown != nullptr) {
        const Allowance allowance = allowanceOf(Configs::dataManager->groupsRepo->GetGroup(shown->gid));
        const auto shownGroup = Configs::dataManager->groupsRepo->GetGroup(shown->gid);
        // The allowance leads: it is what runs out, while the group name is only context.
        QStringList subtitle{!allowance.left.isEmpty() ? allowance.left : shown->outbound->DisplayAddress()};
        if (shownGroup) subtitle << shownGroup->name;
        server->setServer(shown->outbound->DisplayName(), subtitle.join(QStringLiteral(" · ")),
                          shown->latency != 0 ? latencyText(shown) : QString(),
                          ProfileRowDelegate::latencyColor(shown->latency, colors));
    } else {
        const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        server->setServer(tr("No profiles"), group ? group->name : tr("Add a profile to get started"), {}, {});
    }

    auto *modeRow = simpleModeContent->findChild<SimpleListRow *>(QStringLiteral("simpleModeRow"));
    switch (currentSimpleMode()) {
        case SimpleMode::WholeComputer:
            modeRow->setValue(tr("Whole computer"));
            modeRow->setHint(tr("Games, messengers and the browser"));
            break;
        case SimpleMode::BrowserOnly:
            modeRow->setValue(tr("Browser only"));
            modeRow->setHint(tr("Games and calls bypass the VPN"), true);
            break;
        case SimpleMode::Off:
            modeRow->setValue(tr("Not chosen"));
            modeRow->setHint(tr("Apps bypass the VPN"), true);
            break;
    }
    simpleModeContent->findChild<SimpleListRow *>(QStringLiteral("simpleRoutesRow"))->setHint(simpleRoutesSummary());
    simpleRoutesPage->setWholeComputer(Configs::dataManager->settingsRepo->spmode_vpn);
    simpleRoutesPage->setConnected(running != nullptr);

    auto *traffic = simpleModeContent->findChild<SimpleTrafficLine *>(QStringLiteral("simpleTraffic"));
    if (state != StartStopButton::State::Running) {
        traffic->clearSamples();
        traffic->setIdle(tr("Profile traffic"), QStringLiteral("↓ %1 · ↑ %2").arg(ReadableSize(shown ? shown->traffic_downlink : 0), ReadableSize(shown ? shown->traffic_uplink : 0)));
    }
}

void MainWindow::refreshSimpleTraffic(int down, int up) {
    if (simpleModeContent == nullptr || running == nullptr) return;
    simpleModeContent->findChild<SimpleTrafficLine *>(QStringLiteral("simpleTraffic"))->addSample(down, up);
}
