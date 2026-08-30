#include "include/ui/mainwindow.h"

#include "include/ui/mainWindow/MainWindowInternal.h"
// Full definition: MainWindow's destructor lives here and destroys the unique_ptr.
#include "include/ui/mainWindow/TestRunner.h"

#include <QActionGroup>
#include <QMenu>
#include <QAction>
#include <QSignalBlocker>
#include <QStyle>

#include <algorithm>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/RouteUpdater.hpp"
#include "include/global/PeriodicRunner.hpp"
#include "include/global/Logger.hpp"
#include "include/stats/autoselector/AutoSelectorMonitor.hpp"
#include "include/ui/stats/dialog_auto_selector.h"
#include "include/sys/Process.hpp"
#include "include/sys/AutoRun.hpp"
#include "include/sys/UrlScheme.hpp"

#include "include/ui/utils/ConnectionsFilterHeader.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/stats/dialog_traffic_stats.h"
#include "include/ui/stats/dialog_runtime_stats.h"
#include "include/ui/widget/StartStopButton.hpp"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedTitleBar.h"
#include <QPainter>
#include "include/ui/widget/ThronedToggle.h"
#include "include/ui/widget/ThronedWindowChrome.h"
#include "include/control/ThronedControl.h"

#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/global/Common.h"

#include "include/ui/utils/ProfilesTableFilterHeader.h"
#include "include/ui/utils/ProfilesTableModel.h"

#include "include/ui/group/dialog_edit_group.h"

#ifdef Q_OS_WIN
#include <windows.h>
// <windows.h> defines SetPort, which under unity builds clobbers Configs::outbound::SetPort.
#undef SetPort
#else
#ifdef Q_OS_LINUX
#include <QDBusInterface>
#include <QDBusReply>
#include <sys/socket.h>
#endif
#ifdef Q_OS_MACOS
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <unistd.h>
#endif

#include <QUuid>

#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QPalette>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QDesktopServices>
#include <QTimer>
#include <QMessageBox>
#include <QDir>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif
#include <QFileDialog>
#include <QToolButton>
#include <QTabBar>
#include <QVBoxLayout>
#include <QHeaderView>
#include <include/global/HTTPRequestHelper.hpp>
#include "include/global/DeviceDetailsHelper.hpp"

void UI_InitMainWindow() {
    mainwindow = new MainWindow;
}

namespace {
    constexpr int kMaxPingTargets = 3;

    const QList<QColor> &pingTargetColors() {
        static const QList<QColor> colors{
            QColor(QStringLiteral("#2F91FF")),
            QColor(QStringLiteral("#FF9F43")),
            QColor(QStringLiteral("#A66CFF")),
        };
        return colors;
    }

    QList<QPair<QString, QString>> pingTargetPresets() {
        return {
            {QObject::tr("Cloudflare"), QStringLiteral("1.1.1.1:53")},
            {QObject::tr("Google"), QStringLiteral("8.8.8.8:53")},
            {QObject::tr("Quad9"), QStringLiteral("9.9.9.9:53")},
            {QObject::tr("AdGuard"), QStringLiteral("94.140.14.14:53")},
        };
    }

    QIcon colorDotIcon(const QColor &color) {
        QPixmap pixmap(12, 12);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(2, 2, 8, 8));
        return QIcon(pixmap);
    }
}

QStringList MainWindow::pingMonitorTargets() const {
    QStringList targets;
    const auto &settings = *Configs::dataManager->settingsRepo;
    for (auto target : settings.udp_monitor_targets) {
        target = target.trimmed();
        if (!target.isEmpty() && !targets.contains(target)) targets << target;
        if (targets.size() == kMaxPingTargets) break;
    }
    if (targets.isEmpty()) {
        auto fallback = settings.udp_test_target.trimmed();
        if (fallback.isEmpty()) fallback = QStringLiteral("1.1.1.1:53");
        targets << fallback;
    }
    return targets;
}

void MainWindow::updatePingLegend(const QStringList &targets, const QList<int> &proxyMs, const int directMs) {
    if (pingTargetsButton != nullptr) {
        pingTargetsButton->setToolTip(tr("UDP targets (%1/%2):\n%3")
                                          .arg(targets.size()).arg(kMaxPingTargets).arg(targets.join('\n')));
        pingTargetsButton->setAccessibleName(tr("Choose UDP monitor targets"));
    }
    if (pingLegendLabel == nullptr) return;

    QStringList items;
    const auto &colors = pingTargetColors();
    const bool allProxyMissing = !proxyMs.isEmpty()
        && std::all_of(proxyMs.cbegin(), proxyMs.cend(), [](const int value) { return value < 0; });
    if (allProxyMissing && directMs >= 0) {
        items << QStringLiteral("<span style='color:#ff6b6b'><b>%1</b></span>")
                     .arg(tr("No DNS/UDP reply through proxy; direct works").toHtmlEscaped());
    }
    for (int i = 0; i < targets.size(); ++i) {
        const auto latest = i < proxyMs.size()
            ? (proxyMs.at(i) < 0 ? tr("no reply") : QStringLiteral("%1 ms").arg(proxyMs.at(i)))
            : QString{};
        items << QStringLiteral("<span style='color:%1'>●</span> %2%3")
                     .arg(colors.at(i).name(), targets.at(i).toHtmlEscaped(),
                          latest.isEmpty() ? QString{} : QStringLiteral(" <b>%1</b>").arg(latest));
    }
    if (!targets.isEmpty()) {
        const auto latest = directMs == -2 ? QString{}
            : directMs < 0 ? tr("no reply") : QStringLiteral("%1 ms").arg(directMs);
        items << QStringLiteral("<span style='color:#8295A6'>┄</span> %1%2")
                     .arg(tr("direct (%1)").arg(targets.first()).toHtmlEscaped(),
                          latest.isEmpty() ? QString{} : QStringLiteral(" <b>%1</b>").arg(latest));
    }
    pingLegendLabel->setText(items.join(QStringLiteral(" &nbsp; ")));
}

void MainWindow::setPingMonitorTargets(const QStringList &requested, const bool save) {
    QStringList targets;
    for (auto target : requested) {
        target = target.trimmed();
        if (!target.isEmpty() && !targets.contains(target)) targets << target;
        if (targets.size() == kMaxPingTargets) break;
    }
    if (targets.isEmpty()) targets << QStringLiteral("1.1.1.1:53");

    auto &settings = *Configs::dataManager->settingsRepo;
    settings.udp_monitor_targets = targets;
    if (save) settings.Save();

    pingHistory_.clear();
    pingSpikeActive_ = false;
    if (pingChartWidget != nullptr) {
        QList<MiniChartSeriesStyle> styles;
        const auto &colors = pingTargetColors();
        for (int i = 0; i < targets.size(); ++i) styles << MiniChartSeriesStyle{colors.at(i), Qt::SolidLine, false};
        styles << MiniChartSeriesStyle{QColor(QStringLiteral("#8295A6")), Qt::DashLine, false};
        pingChartWidget->setSeriesStyles(styles);
        pingChartWidget->setCaption(QStringLiteral("UDP"));
    }
    updatePingLegend(targets);
}

void MainWindow::rebuildPingTargetsMenu() {
    if (pingTargetsButton == nullptr || pingTargetsButton->menu() == nullptr) return;
    auto *menu = pingTargetsButton->menu();
    menu->clear();

    const auto selected = pingMonitorTargets();
    auto candidates = pingTargetPresets();
    const auto addCustom = [&candidates](const QString &target) {
        if (target.isEmpty()) return;
        const auto exists = std::any_of(candidates.cbegin(), candidates.cend(),
                                        [&target](const auto &item) { return item.second == target; });
        if (!exists) candidates << qMakePair(QObject::tr("Custom"), target);
    };
    addCustom(Configs::dataManager->settingsRepo->udp_test_target.trimmed());
    for (const auto &target : selected) addCustom(target);

    for (const auto &[name, target] : candidates) {
        auto *action = menu->addAction(QStringLiteral("%1 — %2").arg(name, target));
        action->setCheckable(true);
        action->setChecked(selected.contains(target));
        const int selectedIndex = selected.indexOf(target);
        action->setIcon(colorDotIcon(selectedIndex >= 0
                                         ? pingTargetColors().at(selectedIndex)
                                         : palette().color(QPalette::Mid)));
        connect(action, &QAction::toggled, this, [this, action, target](const bool enabled) {
            auto targets = pingMonitorTargets();
            if (enabled) {
                if (targets.contains(target)) return;
                if (targets.size() >= kMaxPingTargets) {
                    const QSignalBlocker blocker(action);
                    action->setChecked(false);
                    MessageBoxWarning(tr("UDP monitor"), tr("Choose at most three targets."));
                    return;
                }
                targets << target;
            } else {
                if (targets.size() == 1) {
                    const QSignalBlocker blocker(action);
                    action->setChecked(true);
                    MessageBoxWarning(tr("UDP monitor"), tr("Keep at least one target selected."));
                    return;
                }
                targets.removeAll(target);
            }
            setPingMonitorTargets(targets, true);
        });
    }

    menu->addSeparator();
    auto *hint = menu->addAction(tr("Custom target is configured in Settings → Testing"));
    hint->setEnabled(false);
}

// Caller must hold coreProcessMutex (reads core_process lock-free by design).
bool MainWindow::verify_core_pid(QLocalSocket *socket) {
    if (!core_process) return false;
    qint64 expectedPid = core_process->processId();
    if (expectedPid <= 0) return false;

#if defined(Q_OS_LINUX)
    struct ucred cred = {};
    socklen_t credLen = sizeof(cred);
    if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == 0) {
        return static_cast<qint64>(cred.pid) == expectedPid;
    }
    return false;
#elif defined(Q_OS_MACOS)
    pid_t pid = 0;
    socklen_t pidLen = sizeof(pid);
    if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_LOCAL, LOCAL_PEERPID, &pid, &pidLen) == 0) {
        return static_cast<qint64>(pid) == expectedPid;
    }
    return false;
#elif defined(Q_OS_WIN)
    ULONG pid = 0;
    HANDLE hPipe = reinterpret_cast<HANDLE>(static_cast<qintptr>(socket->socketDescriptor()));
    if (GetNamedPipeClientProcessId(hPipe, &pid)) {
        return static_cast<qint64>(pid) == expectedPid;
    }
    return false;
#else
    Q_UNUSED(socket)
    return true;
#endif
}

static bool themeUsesDarkLog(const QString &theme) {
    return themeManager->IsDarkTheme(theme);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    const bool uiPreviewMode = Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview"));
    mainwindow = this;
    setAcceptDrops(true);
    MW_dialog_message = [=,this](MwMessage cmd, QStringList args) {
        runOnUiThread([=,this]
        {
            dialog_message_impl(cmd, args);
        });
    };
    MW_handle_deeplink = [=,this](const QString &url) {
        runOnUiThread([=,this]
        {
            handle_deeplink_impl(url);
        });
    };
    MW_import_files = [=,this](const QStringList &paths) {
        runOnUiThread([=,this]
        {
            importFromFiles(paths);
        });
    };

    AutoRun_FixTaskIfNeeded();
    AutoRun_MigrateIfNeeded();

    UrlScheme_RegisterIfNeeded();

    // migrate old themes
    bool isNum;
    Configs::dataManager->settingsRepo->theme.toInt(&isNum);
    if (isNum) {
        Configs::dataManager->settingsRepo->theme = QStringLiteral("Throned Midnight");
    } else if (!themeManager->ThronedThemes().contains(Configs::dataManager->settingsRepo->theme)) {
        // Retire the old platform/QSS theme mix in favour of palettes that cover
        // every redesigned screen consistently.
        Configs::dataManager->settingsRepo->theme = themeUsesDarkLog(Configs::dataManager->settingsRepo->theme)
            ? QStringLiteral("Throned Midnight") : QStringLiteral("System");
    }
    themeManager->ApplyTheme(Configs::dataManager->settingsRepo->theme);
    ui->setupUi(this);

    // MainPreview's exact production shell: the same title bar, block order,
    // dimensions and palette that generate docs/ui-preview/main-en.png.
    auto *redesignedCentral = new QWidget(this);
    redesignedCentral->setObjectName(QStringLiteral("previewRoot"));
    auto *rootLayout = new QVBoxLayout(redesignedCentral);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(ThronedChrome::install(this));

    auto *body = new QWidget(redesignedCentral);
    body->setObjectName(QStringLiteral("body"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(10, 10, 10, 10);
    bodyLayout->setSpacing(7);

    // The command bar belongs to the window chrome, not to the content: it sits
    // edge to edge directly under the title bar so both read as one header band
    // closed by a single hairline, instead of a card floating over another card.
    auto *commandBar = new QFrame(redesignedCentral);
    commandBar->setObjectName(QStringLiteral("commandBar"));
    commandBar->setFixedHeight(54);
    auto *commandLayout = new QHBoxLayout(commandBar);
    commandLayout->setContentsMargins(14, 7, 10, 7);
    commandLayout->setSpacing(5);

    const QList<QPair<QToolButton *, MaterialIcon::Glyph>> navigation{
        {ui->toolButton_program, MaterialIcon::Glyph::Desktop},
        {ui->toolButton_preferences, MaterialIcon::Glyph::Settings},
        {ui->toolButton_testing, MaterialIcon::Glyph::Users},
        {ui->toolButton_routing, MaterialIcon::Glyph::Routes},
        {ui->toolButton_tools, MaterialIcon::Glyph::Tools},
    };
    for (const auto &[button, glyph] : navigation) {
        button->setParent(commandBar);
        button->setStyleSheet({});
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(19, 19));
        button->setMinimumWidth(0);
        button->setFixedHeight(38);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        commandLayout->addWidget(button);
        if (button != navigation.constLast().first) {
            auto *separator = new QFrame(commandBar);
            separator->setObjectName(QStringLiteral("vSeparator"));
            separator->setFixedSize(1, 33);
            commandLayout->addWidget(separator);
        }
    }
    commandLayout->addStretch(1);

    auto addToggle = [commandBar, commandLayout](const QString &text, QCheckBox *toggle) {
        auto *label = new QLabel(text, commandBar);
        label->setObjectName(QStringLiteral("controlLabel"));
        commandLayout->addWidget(label);
        toggle->setParent(commandBar);
        toggle->hide();
        auto *visualToggle = new ThronedToggle(toggle->isChecked(), commandBar);
        visualToggle->bindTo(toggle);
        commandLayout->addWidget(visualToggle);
    };
    addToggle(tr("TUN mode"), ui->checkBox_VPN);
    auto *modeSeparator = new QFrame(commandBar);
    modeSeparator->setObjectName(QStringLiteral("vSeparator"));
    modeSeparator->setFixedSize(1, 33);
    commandLayout->addWidget(modeSeparator);
    addToggle(tr("System proxy"), ui->checkBox_SystemProxy);
    ui->checkBox_VPN->setParent(commandBar);
    ui->system_dns->setParent(commandBar);
    ui->system_dns->hide();
    ui->toolButton_startstop->setParent(commandBar);
    ui->toolButton_startstop->setFixedSize(40, 40);
    commandLayout->addSpacing(8);
    commandLayout->addWidget(ui->toolButton_startstop);
    rootLayout->addWidget(commandBar);

    ui->data_view->setParent(redesignedCentral);
    ui->data_view->setObjectName(QStringLiteral("selectionStatus"));
    ui->data_view->setFixedHeight(0);
    ui->data_view->hide();

    ui->splitter->setParent(body);
    ui->tabWidget->setStyleSheet({});
    ui->stats_widget->setStyleSheet({});
    ui->stats_widget->setAutoFillBackground(false);
    ui->tabWidget->setObjectName(QStringLiteral("groupsCard"));
    ui->stats_widget->setObjectName(QStringLiteral("logsCard"));
    statsPanelHost = ui->stats_widget;
    while (statsPanelHost->parentWidget() != nullptr
           && statsPanelHost->parentWidget() != ui->splitter) {
        statsPanelHost = statsPanelHost->parentWidget();
    }
    if (statsPanelHost->parentWidget() != ui->splitter) statsPanelHost = ui->stats_widget;
    statsPanelHost->setObjectName(QStringLiteral("statsPanelHost"));
    statsPanelHost->setAttribute(Qt::WA_StyledBackground, true);
    if (statsPanelHost->layout() != nullptr) statsPanelHost->layout()->setContentsMargins(0, 0, 0, 0);
    ui->tabWidget->tabBar()->setUsesScrollButtons(false);
    ui->stats_widget->tabBar()->setUsesScrollButtons(false);
    auto *logTools = new QWidget(ui->stats_widget);
    logTools->setObjectName(QStringLiteral("logTools"));
    auto *logToolsLayout = new QHBoxLayout(logTools);
    logToolsLayout->setContentsMargins(0, 0, 8, 5);
    logToolsLayout->setSpacing(6);
    // One menu instead of six controls: at 1024 px the strip had no room left for
    // the tabs themselves.
    auto *logMenuButton = new QToolButton(logTools);
    logMenuButton->setObjectName(QStringLiteral("panelIconButton"));
    logMenuButton->setPopupMode(QToolButton::InstantPopup);
    logMenuButton->setCursor(Qt::PointingHandCursor);
    logMenuButton->setFocusPolicy(Qt::NoFocus);
    logMenuButton->setFixedSize(28, 28);
    logMenuButton->setIconSize(QSize(18, 18));
    logMenuButton->setProperty("statsPage", ui->Logs->objectName());
    logMenuButton->setToolTip(tr("Log tools"));
    auto *logMenu = new QMenu(logMenuButton);
    logMenu->setObjectName(QStringLiteral("logToolsMenu"));
    auto *clearAction = logMenu->addAction(tr("Clear"));
    auto *copyAction = logMenu->addAction(tr("Copy all"));
    auto *autoScrollAction = logMenu->addAction(tr("Auto-scroll"));
    autoScrollAction->setCheckable(true);
    autoScrollAction->setChecked(Configs::dataManager->settingsRepo->log_auto_scroll);
    logMenu->addSeparator();
    auto *levelMenu = logMenu->addMenu(tr("Level"));
    levelMenu->setObjectName(QStringLiteral("logToolsMenu"));
    levelMenu->setToolTipsVisible(true);
    levelMenu->setToolTip(tr("Hides log lines below this level, and sets the core's own log level for the next start"));
    levelMenu->setTitle(tr("Level") + QStringLiteral("        ")
                        + Configs::SingBox::NormalizeLogLevel(
                              Configs::dataManager->settingsRepo->log_level).toUpper());
    logLevelActions = new QActionGroup(this);
    for (const auto &level : Configs::SingBox::LogLevels) {
        auto *levelAction = levelMenu->addAction(level.toUpper());
        levelAction->setCheckable(true);
        levelAction->setData(level);
        logLevelActions->addAction(levelAction);
    }
    auto *filterAction = logMenu->addAction(tr("Filter..."));
    logMenuButton->setMenu(logMenu);
    // InstantPopup normally anchors a menu at the button's left edge. This
    // button lives against the window's right edge, so that default position
    // sends most of the menu off-screen. Anchor its right edge instead and
    // clamp it to the active screen for multi-monitor setups.
    connect(logMenu, &QMenu::aboutToShow, logMenu, [logMenu, logMenuButton] {
        QTimer::singleShot(0, logMenu, [logMenu, logMenuButton] {
            const QSize menuSize = logMenu->sizeHint().expandedTo(logMenu->minimumSizeHint());
            const QPoint buttonBottomRight = logMenuButton->mapToGlobal(
                QPoint(logMenuButton->width(), logMenuButton->height() + 4));
            QPoint menuPos(buttonBottomRight.x() - menuSize.width(), buttonBottomRight.y());
            if (QScreen *screen = logMenuButton->screen()) {
                const QRect available = screen->availableGeometry().adjusted(4, 4, -4, -4);
                menuPos.setX(qBound(available.left(), menuPos.x(),
                                    qMax(available.left(), available.right() - menuSize.width() + 1)));
                if (menuPos.y() + menuSize.height() > available.bottom())
                    menuPos.setY(logMenuButton->mapToGlobal(QPoint(0, -menuSize.height() - 4)).y());
                menuPos.setY(qMax(available.top(), menuPos.y()));
            }
            logMenu->move(menuPos);
        });
    });
    logToolsLayout->addWidget(logMenuButton);
    connect(clearAction, &QAction::triggered, this, [this] { clear_log_view(); });
    connect(copyAction, &QAction::triggered, this, [this] {
        QApplication::clipboard()->setText(ui->masterLogBrowser->toPlainText());
    });
    connect(autoScrollAction, &QAction::toggled, this, [autoScrollAction](bool enabled) {
        Configs::dataManager->settingsRepo->log_auto_scroll = enabled;
        Configs::dataManager->settingsRepo->Save();
        const auto colors = themeManager->Colors();
        autoScrollAction->setIcon(enabled
            ? MaterialIcon::icon(MaterialIcon::Glyph::Check, colors.success, 17) : QIcon());
    });
    connect(logLevelActions, &QActionGroup::triggered, this, [this, levelMenu](QAction *action) {
        Configs::dataManager->settingsRepo->log_level = action->data().toString();
        Configs::dataManager->settingsRepo->Save();
        levelMenu->setTitle(tr("Level") + QStringLiteral("        ")
                            + action->data().toString().toUpper());
        updateLogFilterFields();
    });
    connect(filterAction, &QAction::triggered, this, [this] { openLogSettings(); });
    statsPanelTools = {logMenuButton};
    ui->stats_widget->setCornerWidget(logTools, Qt::TopRightCorner);
    bodyLayout->addWidget(ui->splitter, 1);

    // The closed panel is its own card. Squashing the tab widget instead left the
    // tabs sitting bare on the window with a sliver of the page under them.
    statsStrip = new QFrame(body);
    statsStrip->setObjectName(QStringLiteral("logsStrip"));
    auto *stripLayout = new QHBoxLayout(statsStrip);
    stripLayout->setContentsMargins(9, 4, 7, 4);
    stripLayout->setSpacing(2);
    statsStrip->setFixedHeight(39);
    for (int tab = 0; tab < ui->stats_widget->count(); tab++) {
        auto *tabButton = new QToolButton(statsStrip);
        tabButton->setObjectName(QStringLiteral("stripTab"));
        tabButton->setText(ui->stats_widget->tabText(tab));
        tabButton->setCursor(Qt::PointingHandCursor);
        tabButton->setFocusPolicy(Qt::NoFocus);
        QWidget *page = ui->stats_widget->widget(tab);
        tabButton->setProperty("statsPage", page->objectName());
        connect(tabButton, &QToolButton::clicked, this, [this, page] {
            ui->stats_widget->setCurrentWidget(page);
            setStatsPanelOpen(true);
        });
        stripLayout->addWidget(tabButton);
        statsStripTabs.append(tabButton);
        if (page == ui->connections_tab) {
            statsConnectionStripCount = new QToolButton(statsStrip);
            statsConnectionStripCount->setObjectName(QStringLiteral("stripCountBadge"));
            statsConnectionStripCount->setCursor(Qt::PointingHandCursor);
            statsConnectionStripCount->setFocusPolicy(Qt::NoFocus);
            statsConnectionStripCount->setFixedWidth(31);
            statsConnectionStripCount->setToolTip(tr("Connections"));
            connect(statsConnectionStripCount, &QToolButton::clicked, this, [this, page] {
                ui->stats_widget->setCurrentWidget(page);
                setStatsPanelOpen(true);
            });
            stripLayout->addWidget(statsConnectionStripCount);
        }
    }
    const int connectionsTab = ui->stats_widget->indexOf(ui->connections_tab);
    if (connectionsTab >= 0) {
        ui->stats_widget->setTabText(connectionsTab, tr("Connections"));
        statsConnectionTabCount = new QLabel(QStringLiteral("0"), ui->stats_widget->tabBar());
        statsConnectionTabCount->setObjectName(QStringLiteral("tabCountBadge"));
        statsConnectionTabCount->setAlignment(Qt::AlignCenter);
        statsConnectionTabCount->setFixedWidth(31);
        statsConnectionTabCount->setAttribute(Qt::WA_TransparentForMouseEvents);
        ui->stats_widget->tabBar()->setTabButton(connectionsTab, QTabBar::RightSide,
                                                 statsConnectionTabCount);
    }
    stripLayout->addStretch(1);
    auto *stripHint = new QLabel(tr("Click a tab to open"), statsStrip);
    stripHint->setObjectName(QStringLiteral("stripHint"));
    stripLayout->addWidget(stripHint);
    stripLayout->addSpacing(7);
    statsStripToggle = new QToolButton(statsStrip);
    statsStripToggle->setObjectName(QStringLiteral("panelIconButton"));
    statsStripToggle->setCursor(Qt::PointingHandCursor);
    statsStripToggle->setFocusPolicy(Qt::NoFocus);
    statsStripToggle->setFixedSize(28, 28);
    statsStripToggle->setIconSize(QSize(18, 18));
    statsStripToggle->setToolTip(tr("Show logs and connections"));
    connect(statsStripToggle, &QToolButton::clicked, this, [this] { setStatsPanelOpen(true); });
    stripLayout->addWidget(statsStripToggle);
    bodyLayout->addWidget(statsStrip);

    // Bottom chrome mirrors the header: a full-width strip closed by a hairline
    // rather than another bordered card stacked inside the content area.
    auto *statusCard = new QFrame(redesignedCentral);
    statusCard->setObjectName(QStringLiteral("statusCard"));
    statusCard->setFixedHeight(56);
    auto *statusLayout = new QHBoxLayout(statusCard);
    statusLayout->setContentsMargins(15, 6, 13, 6);
    statusLayout->setSpacing(18);
    // Каждая ячейка: подпись сверху, значение снизу. Cells used to differ in
    // shape - one of them was two lines - and the strip was sized by whichever
    // was tallest, leaving the rest floating. A caption/value grid gives the bar
    // a rhythm without wrapping anything in a box.
    statusDirectSpeed = new QLabel(statusCard);
    // No glyphs down here: five of them cost ~135 px of a 1024 px strip, which is
    // what pushed the routing summary into an ellipsis at the default window size.
    struct StatusItem { QLabel *value; QString caption; int stretch; };
    const QList<StatusItem> statusItems{
        {ui->label_running, tr("Connection"), 5},
        {ui->label_inbound, tr("Inbound"), 4},
        {ui->label_speed, QStringLiteral("Proxy"), 4},
        {statusDirectSpeed, QStringLiteral("Direct"), 4},
    };
    QList<QPair<QLabel *, MaterialIcon::Glyph>> mutedIcons;
    for (const auto &[value, caption, stretch] : statusItems) {
        auto *cell = new QWidget(statusCard);
        cell->setObjectName(QStringLiteral("statusCell"));
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(9);
        auto *text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(1);
        auto *captionLabel = new QLabel(caption, cell);
        captionLabel->setObjectName(QStringLiteral("statusCaption"));
        captionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->addWidget(captionLabel);
        if (value == ui->label_running) statusConnectionCaption = captionLabel;
        value->setParent(cell);
        value->setObjectName(QStringLiteral("statusValue"));
        // Ignored: a long value must not widen its cell and shove the neighbours.
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        value->installEventFilter(this);
        statusElidedLabels.append(value);
        text->addWidget(value);
        cellLayout->addLayout(text, 1);
        statusLayout->addWidget(cell, stretch);
    }
    if (statusConnectionCaption != nullptr) {
        statusConnectionCaption->installEventFilter(this);
        statusElidedLabels.append(statusConnectionCaption);
    }

    // Routing segment: a summary of the active profile that opens the quick menu.
    // Icon and text live in one clickable frame, so the whole block is the hit
    // target instead of just the glyph of text inside it.
    auto *routingButton = new QFrame(statusCard);
    routingButton->setObjectName(QStringLiteral("routingStatusButton"));
    routingButton->setCursor(Qt::PointingHandCursor);
    routingButton->setToolTip(tr("Routing profile and default traffic"));
    routingButton->setAttribute(Qt::WA_Hover, true);
    routingButton->installEventFilter(this);
    auto *routingButtonLayout = new QHBoxLayout(routingButton);
    routingButtonLayout->setContentsMargins(8, 3, 10, 3);
    routingButtonLayout->setSpacing(9);
    auto *routingText = new QVBoxLayout;
    routingText->setContentsMargins(0, 0, 0, 0);
    routingText->setSpacing(1);
    auto *routingCaption = new QLabel(tr("Routing"), routingButton);
    routingCaption->setObjectName(QStringLiteral("statusCaption"));
    routingText->addWidget(routingCaption);
    auto *routingStatus = new QLabel(routingButton);
    routingStatus->setObjectName(QStringLiteral("routingStatus"));
    routingStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    routingStatus->installEventFilter(this);
    statusElidedLabels.append(routingStatus);
    routingCaption->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    routingText->addWidget(routingStatus);
    routingButtonLayout->addLayout(routingText, 1);
    statusLayout->addStretch(1);
    statusLayout->addWidget(routingButton, 4);
    auto *selectionCard = new QFrame(redesignedCentral);
    selectionCard->setObjectName(QStringLiteral("selectionCard"));
    auto *selectionLayout = new QHBoxLayout(selectionCard);
    selectionLayout->setContentsMargins(15, 8, 13, 8);
    selectionLayout->setSpacing(10);
    auto *selectionIcon = new QLabel(selectionCard);
    selectionLayout->addWidget(selectionIcon);
    auto *selectionText = new QLabel(selectionCard);
    selectionText->setObjectName(QStringLiteral("selectionText"));
    selectionLayout->addWidget(selectionText);
    selectionLayout->addStretch(1);
    // A URL test rides on TCP, so it stays green on a path that drops UDP -- which
    // is exactly what breaks Hysteria/TUIC/WireGuard. This measures that path.
    // Support lives or dies on being able to ask for one paste instead of a
    // questionnaire, so this sits in the top-level menu rather than under Tools.
    auto *diagnosticsAction = new QAction(tr("Copy Diagnostics"), this);
    ui->menu_program->insertAction(ui->menu_exit, diagnosticsAction);
    connect(diagnosticsAction, &QAction::triggered, this, [this] { copyDiagnostics(); });

    auto *udpTestAction = new QAction(tr("UDP Latency Test Selected"), this);
    ui->menu_test_item->insertAction(ui->actionSpeedtest_Selected, udpTestAction);
    connect(udpTestAction, &QAction::triggered, this, [=, this] {
        testRunner->runUdpTests(get_now_selected_list());
    });

    const QList<QPair<QString, QAction *>> selectionActions{
        {tr("TCP latency"), ui->actionUrl_Test_Selected},
        {tr("UDP latency"), udpTestAction},
        {tr("Speed test"), ui->actionSpeedtest_Selected},
        {tr("Resolve IP"), ui->actionResolve_Selected_Out_IP},
    };
    for (const auto &[text, action] : selectionActions) {
        auto *button = new QPushButton(text, selectionCard);
        button->setObjectName(QStringLiteral("selectionAction"));
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        selectionLayout->addWidget(button);
    }
    selectionCard->setFixedHeight(68);
    selectionCard->hide();
    rootLayout->addWidget(body, 1);
    rootLayout->addWidget(ui->data_view);
    rootLayout->addWidget(selectionCard);
    rootLayout->addWidget(statusCard);

    // Icons are rasterised, so they have to be repainted whenever the theme
    // changes; otherwise a blue glyph survives into a warm palette.
    const auto retintIcons = [navigation, mutedIcons, selectionIcon, clearAction,
                              copyAction, autoScrollAction, filterAction] {
        const auto colors = themeManager->Colors();
        for (const auto &[button, glyph] : navigation)
            button->setIcon(MaterialIcon::icon(glyph, colors.textMuted, 19));
        for (const auto &[label, glyph] : mutedIcons)
            label->setPixmap(MaterialIcon::pixmap(glyph, colors.textMuted, 18));
        selectionIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::List, colors.accent, 21));
        clearAction->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Delete, colors.textMuted, 17));
        copyAction->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Copy, colors.textMuted, 17));
        autoScrollAction->setIcon(autoScrollAction->isChecked()
            ? MaterialIcon::icon(MaterialIcon::Glyph::Check, colors.success, 17) : QIcon());
        filterAction->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Filter, colors.textMuted, 17));
    };
    retintIcons();
    connect(themeManager, &ThemeManager::themeChanged, this, retintIcons);
    refreshRoutingStatus();

    setMinimumSize(960, 680);
    FitWindowToScreen(this);
    ui->centralwidget = redesignedCentral;
    setCentralWidget(redesignedCentral);

    ui->profilesTableView->setAlternatingRowColors(false);
    ui->profilesTableView->setShowGrid(false);
    ui->profilesTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->profilesTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->profilesTableView->verticalHeader()->setDefaultSectionSize(34);
    ui->profilesTableView->setCornerButtonEnabled(false);
    auto tablePalette = ui->profilesTableView->palette();
    tablePalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#143C48")));
    tablePalette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#FFFFFF")));
    ui->profilesTableView->setPalette(tablePalette);
    ui->connections->setShowGrid(false);
    // Every tab page of the bottom panel is a card too, for the same reason the
    // group pages are: the view inside fills its viewport square.
    for (int tab = 0; tab < ui->stats_widget->count(); ++tab) {
        QWidget *page = ui->stats_widget->widget(tab);
        page->setProperty("thronedPanelPage", true);
        page->setAttribute(Qt::WA_StyledBackground, true);
    }
    ui->masterLogBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    const QString mainStyleTemplate = QStringLiteral(R"(
* { font-size: %BASE_FONT_PX%px; color: #F1F3F5; }
QMainWindow { background: #1B1E23; }
QWidget#previewRoot { background: #1B1E23; border: 1px solid #2F3136; }
QWidget#body { background: #1B1E23; }
QFrame#titleBar { background: #1B1E23; border: none; }
QLabel#titleBrand { font-size: 18px; font-weight: 700; }
QLabel#titleVersion { color: #747B85; font-size: 10px; font-weight: 500; padding-top: 5px; }
QLabel#titleContext { font-size: 14px; font-weight: 650; color: #D8DCE1; }
QFrame#titleBar QToolButton { background: transparent; border: none; }
QFrame#titleBar QToolButton:hover { background: #292D33; }
QFrame#titleBar QToolButton#titleClose:hover { background: #C42B35; }
QFrame#vSeparator { background: #2F3136; border: none; }
QFrame#commandBar {
    background: #1B1E23; border: none; border-bottom: 1px solid #2F3136;
}
QFrame#statusCard, QFrame#selectionCard {
    background: #1B1E23; border: none; border-top: 1px solid #2F3136;
}
QFrame#commandBar QToolButton {
    background: transparent; border: none; border-radius: 6px; font-weight: 550; padding: 7px 9px;
}
QFrame#commandBar QToolButton:hover { background: #292D33; }
QFrame#commandBar QToolButton::menu-indicator { image: none; width: 0px; }
QLabel#controlLabel { font-weight: 550; }
QWidget#logTools { background: transparent; }
QFrame#tableTools {
    background: #171B21; border: 1px solid #343A42; border-radius: 7px;
}
QPushButton#logToolButton {
    background: #222529; border: 1px solid #2F3136; border-radius: 5px; padding: 6px 10px;
}
QPushButton#logToolButton:hover { background: #292D33; border-color: #4A4F57; }
QToolButton#tableFilterButton {
    background: transparent; border: none; border-right: 1px solid #343A42;
    border-radius: 6px;
}
QToolButton#tableFilterButton:hover { background: #292D33; }
QToolButton#tableFilterButton:checked { background: #182B38; }
QLineEdit#serverSearch {
    background: transparent; border: none; border-radius: 6px;
    padding: 6px 9px 6px 4px;
}
QLineEdit#serverSearch:focus { background: #1B222A; }
QFrame#logsStrip { background: #171B21; border: 1px solid #2F3136; border-radius: 7px; }
QFrame#logsStrip QToolButton#stripTab {
    background: transparent; border: none; color: #A4ABB4;
    padding: 4px 11px; border-radius: 5px; font-weight: 500;
}
QFrame#logsStrip QToolButton#stripTab:hover { color: #F1F3F5; background: #222529; }
QFrame#logsStrip QToolButton#stripCountBadge {
    background: transparent; border: none; color: #747C86; padding: 4px 2px;
    border-radius: 5px; font-weight: 550;
}
QFrame#logsStrip QToolButton#stripCountBadge:hover { color: #A4ABB4; background: #222529; }
QFrame#logsStrip QLabel#stripHint { color: #747C86; background: transparent; font-size: 11px; }
QToolButton#panelIconButton { background: transparent; border: 1px solid #2F3136; border-radius: 5px; padding: 3px; }
QToolButton#panelIconButton:hover { background: #222529; }
QToolButton#panelIconButton:checked { background: #182B38; border-color: #2E749A; }
QToolButton#panelIconButton::menu-indicator { image: none; width: 0px; }
QToolButton#connectionRowCloseButton { background: transparent; border: none; border-radius: 4px; padding: 2px; }
QToolButton#connectionRowCloseButton:hover { background: #3A2227; }
QTabWidget#groupsCard, QTabWidget#logsCard { background: transparent; }
QTabWidget#groupsCard::pane { background: transparent; border: none; top: 0px; }
QTabWidget#groupsCard::tab-bar { left: 6px; }
QTabWidget#groupsCard QTabBar { background: transparent; qproperty-drawBase: 0; }
QTabWidget#groupsCard QTabBar::tab {
    background: #222529; border: 1px solid #343A42; border-radius: 5px;
    padding: 6px 11px; margin-right: 6px; margin-bottom: 6px;
    color: #C2C7CE; font-weight: 500;
}
QTabWidget#groupsCard QTabBar::tab:hover { color: #F1F3F5; background: #292D33; border-color: #4A4F57; }
QTabWidget#groupsCard QTabBar::tab:selected { color: #F1F3F5; background: #24282D; border-color: #3D444D; }
QWidget#statsPanelHost {
    background: #171B21; border: 1px solid #343A42; border-radius: 8px;
}
QTabWidget#logsCard::pane { background: transparent; border: none; top: 0px; }
QTabWidget#logsCard::tab-bar { left: 5px; }
QTabWidget#logsCard QTabBar { background: transparent; qproperty-drawBase: 0; }
QTabWidget#logsCard QTabBar::tab {
    background: transparent; border: 1px solid transparent; border-radius: 5px;
    padding: 5px 11px; margin: 4px 2px 4px 0; color: #A4ABB4; font-weight: 500;
}
QTabWidget#logsCard QTabBar::tab:hover { color: #F1F3F5; background: #222529; }
QTabWidget#logsCard QTabBar::tab:selected { color: #F1F3F5; background: #292D33; border-color: #343A42; }
QTabWidget#logsCard QTabBar QLabel#tabCountBadge {
    color: #747C86; background: transparent; border: none; font-weight: 550;
}
QMenu#logToolsMenu {
    background: #22262C; border: 1px solid #3A414A; border-radius: 7px; padding: 6px;
}
QMenu#logToolsMenu::item { padding: 7px 30px 7px 28px; border-radius: 4px; }
QMenu#logToolsMenu::item:selected { background: #2D333B; }
QMenu#logToolsMenu::item:disabled { color: #747C86; }
QMenu#logToolsMenu::separator { height: 1px; background: #3A414A; margin: 4px 7px; }
QWidget[thronedCard="true"] {
    background: #171B21; border: 1px solid #343A42; border-radius: 8px;
}
QWidget[thronedPanelPage="true"] { background: transparent; border: none; }
/* The group page paints the card; the table and its headers stay transparent so
   the rounded corners are not filled in square by the view. */
QTableView, QTableWidget, QTextBrowser {
    background: transparent; border: none; outline: none;
    selection-color: white; selection-background-color: #143C48;
}
QHeaderView::section {
    background: transparent; color: #C2C7CE; border: none; border-right: 1px solid #2F3136;
    border-bottom: 1px solid #2F3136; padding: 5px 8px; font-weight: 500;
}
QHeaderView { background: transparent; }
QHeaderView QLineEdit {
    background: #20252B; color: #E7EAED; border: 1px solid #3A424C;
    border-radius: 5px; padding: 3px 7px;
}
QHeaderView QLineEdit:hover { border-color: #525C68; }
QHeaderView QLineEdit:focus { background: #222A33; border-color: #2F91FF; }
QHeaderView::section:vertical,
QHeaderView::section:vertical:checked,
QHeaderView::section:vertical:pressed {
    /* Opaque: paintSection() fills with QPalette::Button, which a transparent rule zeroes to black. */
    color: #8295A6; background: #171B21; border-right: 1px solid #2F3136;
}
ProfilesTableVerticalHeader {
    qproperty-sectionBackground: #171B21;
    qproperty-sectionForeground: #8295A6;
    qproperty-sectionBorder: #2F3136;
}
QTableCornerButton::section { background: #171B21; border: none; border-right: 1px solid #2F3136; border-bottom: 1px solid #2F3136; }
QTableView::item, QTableWidget::item { border-bottom: 1px solid #2F3136; padding: 3px 7px; }
QTableView::item:selected, QTableWidget::item:selected {
    color: white; background: #143C48;
    border-top: 1px solid #1D7585; border-bottom: 1px solid #1D7585;
}
QSplitter::handle { background: transparent; height: 8px; }
QTextBrowser#masterLogBrowser { padding: 8px 10px; font-family: "Cascadia Mono", "Consolas", monospace; font-size: 13px; }
QWidget#statusCell { background: transparent; }
QFrame#statusCard QLabel#statusCaption {
    color: #747C86; background: transparent; border: none; font-size: 11px;
}
QFrame#statusCard QLabel#statusValue {
    color: #F1F3F5; background: transparent; border: none; font-size: 13px;
}
QFrame#statusCard QFrame#routingStatusButton {
    background: transparent; border: 1px solid transparent; border-radius: 7px;
}
QFrame#statusCard QFrame#routingStatusButton:hover { background: #222529; border-color: #2F3136; }
QFrame#statusCard QLabel#routingStatus { color: #DDE2E7; background: transparent; border: none; font-size: 13px; }
QFrame#selectionCard QLabel#selectionText { color: #F1F3F5; font-weight: 600; }
QFrame#selectionCard QPushButton#selectionAction {
    background: #222529; border: 1px solid #2F3136; border-radius: 5px; padding: 6px 10px;
}
QFrame#selectionCard QPushButton#selectionAction:hover { background: #292D33; border-color: #4A4F57; }
QTextBrowser#selectionStatus {
    color: #E5E8EB; background: #1B1E23; border: none;
    border-top: 1px solid #2F3136; padding: 8px 15px;
}
QScrollBar:vertical { background: transparent; width: 11px; margin: 3px; }
QScrollBar::handle:vertical { background: #344759; border-radius: 4px; min-height: 34px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)");
    themeManager->RegisterStyle(this, mainStyleTemplate);

    // init shortcuts
    setActionsData();
    loadShortcuts();

    last_running_profile_id = Configs::dataManager->settingsRepo->remember_id;

    if (!Configs::dataManager->settingsRepo->mainWindowGeometry.isEmpty()) {
        auto geo = DecodeB64IfValid(Configs::dataManager->settingsRepo->mainWindowGeometry);
        this->restoreGeometry(geo);
    }

    ui->splitter->restoreState(DecodeB64IfValid(Configs::dataManager->settingsRepo->splitter_state));
    ui->splitter->setChildrenCollapsible(false);
    ui->splitter->setStretchFactor(0, 3);
    ui->splitter->setStretchFactor(1, 2);
    // Splitter states saved by the legacy layout can allocate more than half
    // the window to the log and make the profile table look compressed.  Keep
    // sensible user-adjusted states, but migrate obviously legacy proportions
    // to MainPreview's 3:2 table/log balance after the first layout pass.
    QTimer::singleShot(0, ui->splitter, [this] {
        const QList<int> sizes = ui->splitter->sizes();
        if (sizes.size() != 2) return;
        const int total = sizes[0] + sizes[1];
        if (total <= 0) return;
        // A closed panel is deliberately short; this migration must not reopen it.
        if (!Configs::dataManager->settingsRepo->stats_panel_open) return;
        if (sizes[0] * 100 < total * 52 || sizes[1] < 120)
            ui->splitter->setSizes({total * 3 / 5, total * 2 / 5});
    });
    setLogHighlighter(themeUsesDarkLog(Configs::dataManager->settingsRepo->theme));
    qvLogDocument->setUndoRedoEnabled(false);
    qvLogDocument->setMaximumBlockCount(Configs::dataManager->settingsRepo->max_log_line);
    ui->masterLogBrowser->setUndoRedoEnabled(false);
    ui->masterLogBrowser->setDocument(qvLogDocument);
    applyLogBrowserFont();
    updateLogFilterFields();
    if (!uiPreviewMode) {
        runOnThread([=, this] {
            log_process_loop();
        }, LogThread);
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [=,this](const Qt::ColorScheme& scheme) {
        setLogHighlighter(scheme == Qt::ColorScheme::Dark);
        themeManager->ApplyTheme(Configs::dataManager->settingsRepo->theme, true);
    });
#endif
    connect(themeManager, &ThemeManager::themeChanged, this, [=,this](const QString& theme){
        setLogHighlighter(themeUsesDarkLog(theme));
        scheduleProxyListRefresh();
    });
    MW_show_log = [=,this](const QString &log) {
        append_log(log);
        Logging::WriteUserLog(log);
    };

    if (Configs::dataManager->settingsRepo->random_inbound_port)
    {
        Configs::dataManager->settingsRepo->inbound_socks_port = MkPort(Configs::dataManager->settingsRepo->inbound_address);
    }

    if (!uiPreviewMode) {
        runOnNewThread([=, this] {GetDeviceDetails(); });

        auto core_path = QApplication::applicationDirPath() + "/";
        core_path += "ThronedCore";
        const bool coreDebugMode = Configs::dataManager->settingsRepo->log_level == "debug";

        Configs::dataManager->settingsRepo->core_socket_name =
            "thronedIPC-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        core_server = new QLocalServer(this);
        core_server->setSocketOptions(QLocalServer::UserAccessOption);
        if (!core_server->listen(Configs::dataManager->settingsRepo->core_socket_name)) {
            qWarning() << "Failed to start IPC server:" << core_server->errorString();
            qApp->quit();
        }

        connect(core_server, &QLocalServer::newConnection, this, [=, this]() {
            auto socket = core_server->nextPendingConnection();
            int profileId = -1;
            {
                // Hold coreProcessMutex: DS_cores may still be constructing core_process.
                QMutexLocker lock(&coreProcessMutex);
                if (!verify_core_pid(socket)) {
                    MW_show_log("[Warn] IPC connection from unexpected process rejected");
                    socket->close();
                    socket->deleteLater();
                    return;
                }
                if (core_process) {
                    profileId = core_process->start_profile_when_core_is_up;
                    core_process->start_profile_when_core_is_up = -1;
                }
            }
            setup_rpc(socket);
            Configs::dataManager->settingsRepo->core_running = true;
            LOG_INFO(QString("elevated: %1").arg(Configs::IsAdmin() ? "yes" : "no"));
            MW_dialog_message(MwMessage::CoreStarted, {Int2String(profileId)});
        });

        const auto socketFullName = core_server->fullServerName();
        runOnThread(
            [=, this] {
                QMutexLocker lock(&coreProcessMutex);
                core_process = new Configs_sys::CoreProcess(core_path, socketFullName, coreDebugMode);
                if (Configs::dataManager->settingsRepo->remember_enable &&
                    Configs::dataManager->settingsRepo->remember_id >= 0) {
                    core_process->start_profile_when_core_is_up =
                        Configs::dataManager->settingsRepo->remember_id;
                }
                core_process->Start();
            },
            DS_cores);
    }

    if (!Configs::dataManager->settingsRepo->font.isEmpty()) {
        auto font = qApp->font();
        font.setFamily(Configs::dataManager->settingsRepo->font);
        qApp->setFont(font);
    }
    if (Configs::dataManager->settingsRepo->font_size != 0) {
        auto font = qApp->font();
        font.setPointSize(Configs::dataManager->settingsRepo->font_size);
        qApp->setFont(font);
    }

    parallelCoreCallPool->setMaxThreadCount(10);
    testRunner = std::make_unique<TestRunner>(this);
    // The .ui carries Return; numpad Enter is the same gesture.
    ui->menu_start->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    connect(ui->menu_start, &QAction::triggered, this, [=,this]() { profile_start(); });
    connect(ui->menu_stop, &QAction::triggered, this, [=,this]() { profile_stop(false, false, true); });
    connect(ui->toolButton_startstop, &QAbstractButton::clicked, this, [=,this]() {
        // The button is disabled while Connecting, so a click is stop-running or start-selected.
        if (running != nullptr) profile_stop(false, false, true);
        else profile_start();
    });
    connect(ui->tabWidget->tabBar(), &QTabBar::tabMoved, this, [=,this](int from, int to) {
        QList<int> tabOrder;
        for (int i = 0; i < ui->tabWidget->tabBar()->count(); i++) {
            tabOrder += ui->tabWidget->tabBar()->tabData(i).toInt();
        }
        Configs::dataManager->groupsRepo->SetGroupsTabOrder(tabOrder);
        on_tabWidget_currentChanged(ui->tabWidget->tabBar()->currentIndex());
    });
    ui->label_running->installEventFilter(this);
    ui->label_inbound->installEventFilter(this);
    ui->splitter->installEventFilter(this);
    ui->tabWidget->installEventFilter(this);
    auto btnFilter = new QToolButton(this);
    btnFilter->setObjectName(QStringLiteral("tableFilterButton"));
    btnFilter->setToolTip(QString("%1\n%2").arg(tr("Enable Filter"), QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
    btnFilter->setShortcut(QKeySequence::Find);
    btnFilter->setCheckable(true);
    btnFilter->setIconSize(QSize(17, 17));
    btnFilter->setFixedSize(32, 32);
    connect(btnFilter, &QToolButton::toggled, static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::setFiltersVisible);
    connect(static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::closeRequested,
            btnFilter, [btnFilter] { btnFilter->setChecked(false); });
    auto *tableTools = new QFrame(ui->tabWidget);
    tableTools->setObjectName(QStringLiteral("tableTools"));
    auto *tableToolsLayout = new QHBoxLayout(tableTools);
    tableToolsLayout->setContentsMargins(0, 0, 0, 0);
    tableToolsLayout->setSpacing(0);
    tableTools->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnFilter->setFixedSize(35, 32);
    tableToolsLayout->addWidget(btnFilter);
    auto *serverSearch = new QLineEdit(tableTools);
    serverSearch->setObjectName(QStringLiteral("serverSearch"));
    serverSearch->setPlaceholderText(tr("Search servers..."));
    serverSearch->setClearButtonEnabled(true);
    serverSearch->setFixedSize(236, 32);
    auto *searchAction = serverSearch->addAction(QIcon(), QLineEdit::LeadingPosition);
    tableToolsLayout->addWidget(serverSearch);
    connect(serverSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        globalFilterString = text;
        if (m_filterRefreshDebounce) m_filterRefreshDebounce->start();
    });
    ui->tabWidget->setCornerWidget(tableTools, Qt::TopRightCorner);
    const auto retintTableTools = [btnFilter, searchAction] {
        const auto colors = themeManager->Colors();
        btnFilter->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Tune,
                                              btnFilter->isChecked() ? colors.accent : colors.textMuted, 17));
        // Qt centres a line-edit action on the frame, which puts the glyph above
        // the text's optical centre. Drawing it one pixel down inside a slightly
        // larger square lands it on the same line as the placeholder.
        QPixmap glyph(18, 18);
        glyph.fill(Qt::transparent);
        QPainter painter(&glyph);
        painter.drawPixmap(0, 1, MaterialIcon::pixmap(MaterialIcon::Glyph::Search, colors.textSubtle, 17));
        painter.end();
        searchAction->setIcon(QIcon(glyph));
    };
    retintTableTools();
    connect(btnFilter, &QToolButton::toggled, this, retintTableTools);
    connect(themeManager, &ThemeManager::themeChanged, this, retintTableTools);
    //
    RegisterHotkey(false);
    auto last_size = Configs::dataManager->settingsRepo->mw_size.split("x");
    if (last_size.length() == 2) {
        auto w = last_size[0].toInt();
        auto h = last_size[1].toInt();
        if (w > 0 && h > 0) {
            resize(w, h);
        }
    }

    // software_name
    software_name = "Throned";
    software_core_name = "sing-box";
    if (auto dashDir = QDir("dashboard"); !dashDir.exists() && QDir().mkdir("dashboard")) {
        if (auto dashFile = QFile(":/Throned/dashboard-notice.html"); dashFile.exists() && dashFile.open(QIODevice::ReadOnly))
        {
            auto data = dashFile.readAll();
            if (auto dest = QFile("dashboard/index.html"); dest.open(QIODevice::Truncate | QIODevice::WriteOnly))
            {
                dest.write(data);
                dest.close();
            }
            dashFile.close();
        }
    }
    // Leaving the dir non-empty marks it user-provided, disabling the core's own updater.
    SeedDashboard();
    if (auto iconsDir = QDir("icons"); !iconsDir.exists()) {
        QDir().mkdir("icons") ? qDebug("created icons dir") : qDebug("Failed to create icons dir");
    }

    ui->toolButton_program->setMenu(ui->menu_program);
    ui->toolButton_preferences->setMenu(ui->menu_preferences);
    ui->toolButton_routing->setMenu(ui->menuRouting_Menu);
    ui->toolButton_testing->setMenu(ui->menuTesting);
    ui->toolButton_tools->setMenu(ui->menuTools);
    ui->toolButton_program->installEventFilter(this);

    designMinimumSize = minimumSize();
    applyTopBarMetrics();

    ui->menubar->setVisible(false);
    connect(ui->actionRuntime_Stats, &QAction::triggered, this, [=, this]() {
        USE_DIALOG(DialogRuntimeStats)
    });
    ui->actionTraffic_Stats->setVisible(!Configs::dataManager->settingsRepo->disable_traffic_aggregation);
    connect(ui->actionTraffic_Stats, &QAction::triggered, this, [=, this]() {
        USE_DIALOG(DialogTrafficStats)
    });
    // refresh_auto_selector_view shows and hides this as the selector monitor starts and stops.
    ui->actionAuto_Selector->setVisible(false);
    connect(ui->actionAuto_Selector, &QAction::triggered, this, [=,this]() {
        if (m_autoSelectorDialog == nullptr) {
            m_autoSelectorDialog = new DialogAutoSelector(this);
            connect(m_autoSelectorDialog, &QDialog::finished, this, [this] {
                m_autoSelectorDialog->deleteLater();
                m_autoSelectorDialog = nullptr;
            });
        }
        m_autoSelectorDialog->refresh();
        m_autoSelectorDialog->show();
        m_autoSelectorDialog->raise();
        m_autoSelectorDialog->activateWindow();
    });
    connect(ui->actionCheck_For_Update, &QAction::triggered, this, [=,this] { runOnNewThread([=,this] { CheckUpdate(); }); });
    if (!QFile::exists(QApplication::applicationDirPath() + "/updater") && !QFile::exists(QApplication::applicationDirPath() + "/updater.exe"))
    {
        ui->actionCheck_For_Update->setDisabled(true);
    }

    setupConnectionList();
    ui->stats_widget->tabBar()->setCurrentIndex(Configs::dataManager->settingsRepo->stats_tab);
    connect(ui->stats_widget->tabBar(), &QTabBar::currentChanged, this, [=,this](int index)
    {
        Configs::dataManager->settingsRepo->stats_tab = ui->stats_widget->tabBar()->currentIndex();
        syncConnectionViewState();
    });
    syncConnectionViewState();
    connect(ui->connections->horizontalHeader(), &QHeaderView::sectionClicked, this, [=,this](int index)
    {
            // The close column has no sort of its own; without this it would fall through and reset sorting.
            if (index == ConnectionsFilterHeader::ColClose) return;

            Stats::ConnectionSort sortType;

            switch (index)
            {
            case 1: sortType = Stats::ByProcess; break;
            case 2: sortType = Stats::ByProtocol; break;
            case 3: sortType = Stats::ByOutbound; break;
            case 4: sortType = Stats::ByTraffic; break;
            case 5: sortType = Stats::BySpeed; break;
            default: sortType = Stats::Default; break;
            }

            applyConnectionSort(sortType);
    });

    speedChartWidget = new SpeedWidget(this);
    speedChartWidget->setObjectName(QStringLiteral("throughputChart"));
    speedChartWidget->setFrameShape(QFrame::NoFrame);
    speedChartWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    speedChartWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->graph_tab->layout()->addWidget(speedChartWidget);

    // Second column: UDP round trip over time. Off by default because each sample
    // costs a probe through the running core.
    auto *pingColumn = new QWidget(this);
    auto *pingColumnLayout = new QVBoxLayout(pingColumn);
    pingColumnLayout->setContentsMargins(4, 4, 4, 4);
    pingColumnLayout->setSpacing(4);
    auto *pingHeader = new QWidget(pingColumn);
    auto *pingHeaderLayout = new QHBoxLayout(pingHeader);
    pingHeaderLayout->setContentsMargins(0, 0, 0, 0);
    pingHeaderLayout->setSpacing(6);
    pingMonitorToggle = new QCheckBox(tr("Monitor UDP"), pingHeader);
    pingMonitorToggle->setToolTip(tr("Continuously probes the selected DNS-over-UDP targets through the running profile."));
    pingHeaderLayout->addWidget(pingMonitorToggle);
    pingHeaderLayout->addStretch(1);
    auto *pingCopyButton = new QPushButton(tr("Copy Diagnostics"), pingHeader);
    pingCopyButton->setObjectName(QStringLiteral("routeSecondaryButton"));
    pingCopyButton->setCursor(Qt::PointingHandCursor);
    pingCopyButton->setMaximumHeight(24);
    pingCopyButton->setToolTip(tr("Copies the ping history and the rest of the diagnostics, with secrets masked."));
    connect(pingCopyButton, &QPushButton::clicked, this, [this] { copyDiagnostics(); });
    pingHeaderLayout->addWidget(pingCopyButton);
    pingTargetsButton = new QToolButton(pingHeader);
    pingTargetsButton->setObjectName(QStringLiteral("udpTargetsButton"));
    pingTargetsButton->setAutoRaise(true);
    pingTargetsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
    pingTargetsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    pingTargetsButton->setPopupMode(QToolButton::InstantPopup);
    pingTargetsButton->setFixedSize(26, 24);
    auto *pingTargetsMenu = new QMenu(pingTargetsButton);
    pingTargetsButton->setMenu(pingTargetsMenu);
    connect(pingTargetsMenu, &QMenu::aboutToShow, this, [this] { rebuildPingTargetsMenu(); });
    pingHeaderLayout->addWidget(pingTargetsButton);
    pingColumnLayout->addWidget(pingHeader);

    pingLegendLabel = new QLabel(pingColumn);
    pingLegendLabel->setTextFormat(Qt::RichText);
    pingLegendLabel->setWordWrap(true);
    pingLegendLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pingLegendLabel->setStyleSheet(QStringLiteral("font-size: 10px;"));
    pingColumnLayout->addWidget(pingLegendLabel);
    pingChartWidget = new MiniChartWidget(pingColumn);
    pingChartWidget->setObjectName(QStringLiteral("pingChart"));
    pingChartWidget->setCapacity(120);
    pingChartWidget->setFormatter([](const double value) { return QString::number(qRound(value)) + " ms"; });
    pingChartWidget->setCaption(QStringLiteral("UDP"));
    pingChartWidget->setToolTip(tr("Each coloured line is one selected UDP target through the proxy. "
                                   "The dashed gray line is direct for the first target. A cross is a lost probe; "
                                   "a triangle is a latency spike above the current scale."));
    pingColumnLayout->addWidget(pingChartWidget, 1);
    setPingMonitorTargets(pingMonitorTargets(), false);
    if (auto *graphLayout = qobject_cast<QHBoxLayout *>(ui->graph_tab->layout())) {
        graphLayout->addWidget(pingColumn);
        graphLayout->setStretch(0, 1);
        graphLayout->setStretch(1, 1);
    } else {
        ui->graph_tab->layout()->addWidget(pingColumn);
    }

    pingMonitorTimer = new QTimer(this);
    pingMonitorTimer->setInterval(2000);
    connect(pingMonitorTimer, &QTimer::timeout, this, [this] { pollPingMonitor(); });
    connect(pingMonitorToggle, &QCheckBox::toggled, this, [this](const bool enabled) {
        Configs::dataManager->settingsRepo->monitor_ping = enabled;
        if (enabled) {
            pingMonitorTimer->start();
            pollPingMonitor();
        } else {
            pingMonitorTimer->stop();
            if (pingChartWidget) pingChartWidget->clear();
            pingHistory_.clear();
            pingSpikeActive_ = false;
            updatePingLegend(pingMonitorTargets());
        }
    });
    pingMonitorToggle->setChecked(Configs::dataManager->settingsRepo->monitor_ping);

    // table UI: model-backed view with on-demand row data
    profilesTableModel = new ProfilesTableModel(this);
    profilesFilterModel = new ProfilesFilterProxyModel(this);
    profilesFilterModel->setSourceModel(profilesTableModel);
    ui->profilesTableView->setModel(profilesFilterModel);
    connect(ui->profilesTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] {
                refresh_startstop_button();
                const int selected = ui->profilesTableView->selectionModel()->selectedRows().size();
                auto *connected = findChild<QFrame *>(QStringLiteral("statusCard"));
                auto *selection = findChild<QFrame *>(QStringLiteral("selectionCard"));
                auto *selectionText = findChild<QLabel *>(QStringLiteral("selectionText"));
                if (!connected || !selection || !selectionText || ui->data_view->isVisible()) return;
                selectionText->setText(tr("%n profiles selected", nullptr, selected));
                selection->setVisible(selected > 1);
                connected->setVisible(selected <= 1);
            });
    ui->profilesTableView->rowsSwapped = [this](int row1, int row2)
    {
        // A drop position in a filtered list says nothing about the group's real order.
        if (profilesFilterModel->hasActiveFilter()) return;
        if (row1 == row2) return;
        auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        group->EmplaceProfile(row1, row2);
        profilesTableModel->emplaceProfiles(row1, row2);
        Configs::dataManager->groupsRepo->Save(group);
    };
    connect(ui->profilesTableView->horizontalHeader(), &QHeaderView::sectionClicked, this, [=, this](int logicalIndex) {
        GroupSortAction action;
        if (proxy_last_order == logicalIndex) {
            action.descending = true;
            proxy_last_order = -1;
        } else {
            proxy_last_order = logicalIndex;
        }
        if (logicalIndex == ProfilesTableModel::ColType) {
            auto group = Configs::dataManager->groupsRepo->CurrentGroup();
            action.method = (Configs::dataManager->settingsRepo->show_config_security && group
                             && group->type_sort_by == Configs::typeBy::bySecurity)
                                ? GroupSortMethod::BySecurity
                                : GroupSortMethod::ByType;
        } else if (logicalIndex == ProfilesTableModel::ColAddress) {
            action.method = GroupSortMethod::ByAddress;
        } else if (logicalIndex == ProfilesTableModel::ColName) {
            action.method = GroupSortMethod::ByName;
        } else if (logicalIndex == ProfilesTableModel::ColTestResult) {
            action.method = GroupSortMethod::ByTestResult;
        } else if (logicalIndex == ProfilesTableModel::ColTraffic) {
            action.method = GroupSortMethod::ByTraffic;
        } else {
            return;
        }
        runOnNewThread([=, this] {
            auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
            if (currGroup == nullptr) return;
            if (!currGroup->SortProfiles(action)) {
                runOnUiThread([=] {
                    MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                });
                return;
            }
            Configs::dataManager->groupsRepo->Save(currGroup);
            runOnUiThread([=, this] {
                refresh_proxy_list({}, true);
            });
        });
    });
    connect(ui->profilesTableView->horizontalHeader(), &QHeaderView::sectionResized, this, [=, this](int, int, int) {
        auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        if (Configs::dataManager->settingsRepo->refreshing_group || group == nullptr) return;
        // Comfortable rows size themselves: the Server column stretches and the metric
        // columns are fixed, so every window resize emits this. Recording it would pin
        // the table to the width it happened to have when the layout first ran.
        if (m_adjustingColumns) return;
        if (profilesTableModel != nullptr
            && profilesTableModel->rowStyle() == ProfilesTableModel::RowStyle::Comfortable) return;
        group->column_width.clear();
        for (int i = 0; i < ui->profilesTableView->horizontalHeader()->count(); i++) {
            group->column_width.push_back(ui->profilesTableView->horizontalHeader()->sectionSize(i));
        }
        Configs::dataManager->groupsRepo->Save(Configs::dataManager->groupsRepo->CurrentGroup());
    });
    ui->profilesTableView->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->profilesTableView->horizontalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* header = ui->profilesTableView->horizontalHeader();
        int columnIndex = header->logicalIndexAt(pos);
        auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        if (group == nullptr) return;

        const bool comfortable = Configs::dataManager->settingsRepo->profile_rows_comfortable;
        // Offered from every column: it is the only place the row style is reachable.
        const auto addRowStyleAction = [this, comfortable](QMenu& menu) {
            auto* rows = menu.addAction(tr("Comfortable rows"));
            rows->setCheckable(true);
            rows->setChecked(comfortable);
            connect(rows, &QAction::triggered, this, [this](bool on) {
                Configs::dataManager->settingsRepo->profile_rows_comfortable = on;
                Configs::dataManager->settingsRepo->Save();
                refreshProfileRowStyle();
            });
        };

        if (comfortable) {
            QMenu menu(this);
            addRowStyleAction(menu);
            menu.exec(header->mapToGlobal(pos));
            return;
        }
        if (columnIndex == ProfilesTableModel::ColUDP) {
            QMenu menu(this);
            auto* toggle = menu.addAction(tr("Show UDP column"));
            toggle->setCheckable(true);
            toggle->setChecked(Configs::dataManager->settingsRepo->show_udp_column);
            addRowStyleAction(menu);
            if (menu.exec(header->mapToGlobal(pos)) != toggle) return;
            Configs::dataManager->settingsRepo->show_udp_column = toggle->isChecked();
            Configs::dataManager->settingsRepo->Save();
            refreshUdpColumnVisibility();
            return;
        }
        if (columnIndex == ProfilesTableModel::ColType) {
            if (!Configs::dataManager->settingsRepo->show_config_security) return;
            QMenu menu(this);
            auto* sortByLabel = menu.addAction(tr("Sort By:"));
            sortByLabel->setEnabled(false);

            struct TypeSortOption { Configs::typeBy value; QString label; };
            const QList<TypeSortOption> options = {
                { Configs::typeBy::byType, tr("Type") },
                { Configs::typeBy::bySecurity, tr("Security") },
            };
            for (const auto& opt : options) {
                auto* act = menu.addAction(opt.label);
                act->setData(static_cast<int>(opt.value));
                act->setCheckable(true);
                act->setChecked(group->type_sort_by == opt.value);
            }

            auto* chosen = menu.exec(header->mapToGlobal(pos));
            if (chosen == nullptr || !chosen->data().isValid()) return;

            group->type_sort_by = static_cast<Configs::typeBy>(chosen->data().toInt());
            Configs::dataManager->groupsRepo->Save(group);
            GroupSortAction action;
            action.method = group->type_sort_by == Configs::typeBy::bySecurity
                                ? GroupSortMethod::BySecurity
                                : GroupSortMethod::ByType;
            runOnNewThread([=, this] {
                auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
                if (currGroup == nullptr) return;
                if (!currGroup->SortProfiles(action)) {
                    runOnUiThread([=] {
                        MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                    });
                    return;
                }
                Configs::dataManager->groupsRepo->Save(currGroup);
                runOnUiThread([=, this] {
                    refresh_proxy_list({}, true);
                });
            });
            return;
        }
        if (columnIndex == ProfilesTableModel::ColTestResult) {
            QMenu menu(this);
            auto* includeLabel = menu.addAction(tr("Include:"));
            includeLabel->setEnabled(false);

            auto* actionShowOutIP = menu.addAction(tr("Out IP"));
            actionShowOutIP->setCheckable(true);
            actionShowOutIP->setChecked(group->test_items_to_show == Configs::testShowItems::all ||
                group->test_items_to_show == Configs::testShowItems::ipOnly);

            auto* actionShowSpeed = menu.addAction(tr("Speed"));
            actionShowSpeed->setCheckable(true);
            actionShowSpeed->setChecked(group->test_items_to_show == Configs::testShowItems::all ||
                group->test_items_to_show == Configs::testShowItems::speedOnly);

            auto updateTestItemsToShow = [this, group, actionShowOutIP, actionShowSpeed] {
                    const bool ip = actionShowOutIP->isChecked();
                    const bool speed = actionShowSpeed->isChecked();
                    if (ip && speed) group->test_items_to_show = Configs::testShowItems::all;
                    else if (ip) group->test_items_to_show = Configs::testShowItems::ipOnly;
                    else if (speed) group->test_items_to_show = Configs::testShowItems::speedOnly;
                    else group->test_items_to_show = Configs::testShowItems::none;
                    Configs::dataManager->groupsRepo->Save(group);
                    if (group->calculated_column_width.size() > ProfilesTableModel::ColTestResult) {
                        group->calculated_column_width[ProfilesTableModel::ColTestResult] = 0;
                    }
                    refresh_proxy_list();
                };

            connect(actionShowOutIP, &QAction::triggered, this, updateTestItemsToShow);
            connect(actionShowSpeed, &QAction::triggered, this, updateTestItemsToShow);

            menu.addSeparator();
            auto* sortByLabel = menu.addAction(tr("Sort By:"));
            sortByLabel->setEnabled(false);

            struct SortOption { int value; QString label; };
            QList<SortOption> options = {
                { static_cast<int>(Configs::testBy::latency), tr("Latency") },
                { static_cast<int>(Configs::testBy::dlSpeed), tr("Download Speed") },
                { static_cast<int>(Configs::testBy::ulSpeed), tr("Upload Speed") },
                { static_cast<int>(Configs::testBy::ipOut), tr("IP Out") }
            };
            for (const auto& opt : options) {
                auto* act = menu.addAction(opt.label);
                act->setData(opt.value);
                act->setCheckable(true);
                act->setChecked(static_cast<int>(group->test_sort_by) == opt.value);
            }

            auto* chosen = menu.exec(header->mapToGlobal(pos));
            if (chosen == nullptr || !chosen->data().isValid()) return;

            int testSortBy = chosen->data().toInt();
            group->test_sort_by = static_cast<Configs::testBy>(testSortBy);
            Configs::dataManager->groupsRepo->Save(group);
            GroupSortAction action;
            action.method = GroupSortMethod::ByTestResult;
            action.descending = true;
            runOnNewThread([=, this] {
                auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
                if (currGroup == nullptr) return;
                if (!currGroup->SortProfiles(action)) {
                    runOnUiThread([=] {
                        MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                        });
                    return;
                }
                Configs::dataManager->groupsRepo->Save(currGroup);
                runOnUiThread([=, this] {
                    refresh_proxy_list({}, true);
                    });
                });
            return;
        }
        if (columnIndex == ProfilesTableModel::ColTraffic) {
            QMenu menu(this);
            auto* sortByLabel = menu.addAction(tr("Sort By:"));
            sortByLabel->setEnabled(false);

            struct TrafficSortOption { int value; QString label; };
            QList<TrafficSortOption> options = {
                { 0, tr("Total") },
                { 1, tr("Downloaded") },
                { 2, tr("Uploaded") }
            };

            for (const auto& opt : options) {
                auto* act = menu.addAction(opt.label);
                act->setData(opt.value);
                act->setCheckable(true);
                act->setChecked(static_cast<int>(group->traffic_sort_by) == opt.value);
            }

            menu.addSeparator();
            auto* toggleUdp = menu.addAction(tr("Show UDP column"));
            toggleUdp->setCheckable(true);
            toggleUdp->setChecked(Configs::dataManager->settingsRepo->show_udp_column);

            auto* chosen = menu.exec(header->mapToGlobal(pos));
            if (chosen == nullptr) return;
            if (chosen == toggleUdp) {
                Configs::dataManager->settingsRepo->show_udp_column = toggleUdp->isChecked();
                Configs::dataManager->settingsRepo->Save();
                refreshUdpColumnVisibility();
                return;
            }
            if (!chosen->data().isValid()) return;

            int trafficSortBy = chosen->data().toInt();
            group->traffic_sort_by = static_cast<Configs::trafficBy>(trafficSortBy);
            Configs::dataManager->groupsRepo->Save(group);
            GroupSortAction action;
            action.method = GroupSortMethod::ByTraffic;
            action.descending = false;
            runOnNewThread([=, this] {
                auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
                if (currGroup == nullptr) return;
                if (!currGroup->SortProfiles(action)) {
                    runOnUiThread([=] {
                        MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                        });
                    return;
                }
                Configs::dataManager->groupsRepo->Save(Configs::dataManager->groupsRepo->CurrentGroup());
                runOnUiThread([=, this] {
                    refresh_proxy_list();
                    });
                });
            return;
        }
        QMenu menu(this);
        addRowStyleAction(menu);
        menu.exec(header->mapToGlobal(pos));
    });
    // Added last so it sits at the end of the corner strip, past the tools the
    // connections tab appends to it.
    statsPanelToggle = new QToolButton(this);
    statsPanelToggle->setObjectName(QStringLiteral("panelIconButton"));
    statsPanelToggle->setCursor(Qt::PointingHandCursor);
    statsPanelToggle->setFocusPolicy(Qt::NoFocus);
    statsPanelToggle->setFixedSize(28, 28);
    statsPanelToggle->setIconSize(QSize(18, 18));
    if (auto *corner = ui->stats_widget->cornerWidget(Qt::TopRightCorner);
        corner != nullptr && corner->layout() != nullptr) {
        corner->layout()->addWidget(statsPanelToggle);
    }
    connect(statsPanelToggle, &QToolButton::clicked, this,
            [this] { setStatsPanelOpen(!Configs::dataManager->settingsRepo->stats_panel_open); });
    // Clicking a tab of a closed panel opens it on that tab, so the strip is not
    // a row of dead labels.
    connect(ui->stats_widget->tabBar(), &QTabBar::tabBarClicked, this, [this](int) {
        if (!Configs::dataManager->settingsRepo->stats_panel_open) setStatsPanelOpen(true);
    });
    connect(ui->stats_widget, &QTabWidget::currentChanged, this,
            [this](int) { refreshStatsPanelTools(); });
    connect(themeManager, &ThemeManager::themeChanged, this, [this] {
        setStatsPanelOpen(Configs::dataManager->settingsRepo->stats_panel_open, false);
    });
    setStatsPanelOpen(Configs::dataManager->settingsRepo->stats_panel_open, false);

    refreshUdpColumnVisibility();
    ui->profilesTableView->verticalHeader()->setStretchLastSection(false);
    ui->profilesTableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    refreshProfileRowStyle();
    ui->profilesTableView->setTabKeyNavigation(false);
    ui->profilesTableView->horizontalHeader()->setResizeContentsPrecision(0);

    connect(ui->profilesTableView->verticalScrollBar(), &QScrollBar::valueChanged, ui->profilesTableView, [=, this] {
        if (!ui->profilesTableView->isVisible()) return;
        refresh_proxy_list_column_size();
    });

    auto *filterHeader = static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader());
    filterHeader->setLastFilterColumn(Configs::dataManager->settingsRepo->last_filter_column);
    connect(filterHeader, &ProfilesTableFilterHeader::lastFilterColumnChanged, this, [](int column)
    {
        Configs::dataManager->settingsRepo->last_filter_column = column;
        Configs::dataManager->settingsRepo->Save();
    });

    m_filterRefreshDebounce = new QTimer(this);
    m_filterRefreshDebounce->setSingleShot(true);
    m_filterRefreshDebounce->setInterval(50);
    connect(m_filterRefreshDebounce, &QTimer::timeout, this, [this] { applyProfileFilters(); });

    connect(filterHeader, &ProfilesTableFilterHeader::typeFilterChanged, this, [this](const QString& currentText)
    {
       typeFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::addressFilterChanged, this, [this](const QString& currentText)
    {
       addressFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::nameFilterChanged, this, [this](const QString& currentText)
    {
       nameFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::testFilterChanged, this, [this](const QString& currentText)
    {
       countryFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::focusTableRequested, this,
            [this](bool selectFirst) { focusProfilesTable(selectFirst); });

    this->refresh_groups();

    tray = new QSystemTrayIcon(nullptr);
    tray->setIcon(GetTrayIcon(Icon::NONE));
    QApplication::setWindowIcon(Icon::GetTrayIcon(Icon::NONE));
    trayMenu = new QMenu();
    trayMenu->addAction(ui->actionShow_window);
    trayMenu->addSeparator();
    trayMenu->addAction(ui->actionStart_with_system);
    trayMenu->addAction(ui->actionRemember_last_proxy);
    trayMenu->addAction(ui->actionAllow_LAN);
    trayMenu->addSeparator();

    auto *actSelectServer = new QAction(tr("Select Profile"), trayMenu);
    connect(actSelectServer, &QAction::triggered, this, [this]() { openTraySelector(false); });
    trayMenu->addAction(actSelectServer);
    auto *actSelectRouting = new QAction(tr("Select Routing"), trayMenu);
    connect(actSelectRouting, &QAction::triggered, this, [this]() { openTraySelector(true); });
    trayMenu->addAction(actSelectRouting);
    auto *actOtpCodes = new QAction(tr("OTP Codes"), trayMenu);
    connect(actOtpCodes, &QAction::triggered, this, [this]() { openTrayOtpCodes(); });
    trayMenu->addAction(actOtpCodes);
    // MacOS cannot reuse menus across different parents properly
    if (getOS() == Darwin) {
        auto* traySpmodeMenu = new QMenu(ui->menu_spmode->title(), trayMenu);
        traySpmodeMenu->addAction(ui->menu_spmode_system_proxy);
        traySpmodeMenu->addAction(ui->menu_spmode_vpn);
        connect(traySpmodeMenu, &QMenu::aboutToShow, this, [=,this]() {
            ui->menu_spmode_disabled->setChecked(!(Configs::dataManager->settingsRepo->spmode_system_proxy || Configs::dataManager->settingsRepo->spmode_vpn));
            ui->menu_spmode_system_proxy->setChecked(Configs::dataManager->settingsRepo->spmode_system_proxy);
            ui->menu_spmode_vpn->setChecked(Configs::dataManager->settingsRepo->spmode_vpn);
        });
        trayMenu->addMenu(traySpmodeMenu);
    } else {
        trayMenu->addMenu(ui->menu_spmode);
    }
    trayMenu->addSeparator();

    trayMenu->addAction(ui->actionRestart_Proxy);
    trayMenu->addAction(ui->actionRestart_Program);
    trayMenu->addAction(ui->menu_exit);
    tray->setVisible(!Configs::dataManager->settingsRepo->disable_tray);
    tray->setContextMenu(trayMenu);
    connect(tray, &QSystemTrayIcon::activated, qApp, [=, this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger && getOS() != Darwin) {
            trayClickEvent();
        }
    });

    ui->actionRemember_last_proxy->setChecked(Configs::dataManager->settingsRepo->remember_enable);
    ui->actionStart_with_system->setChecked(AutoRun_IsEnabled());
    ui->actionAllow_LAN->setChecked(QStringList{"::", "0.0.0.0"}.contains(Configs::dataManager->settingsRepo->inbound_address));

    connect(ui->actionHide_window, &QAction::triggered, this, [=, this](){ HideWindow(this); });
    connect(ui->menu_open_config_folder, &QAction::triggered, this, [=,this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath())); });
    connect(ui->menu_open_dashboard, &QAction::triggered, this, [=,this] { OpenDashboard(); });
    connect(ui->actionRestart_Proxy, &QAction::triggered, this, [=,this] {
        runOnThread([=, this] {
            profile_stop(true, true, true);
            core_process->Kill();
        }, DS_cores);
    });
    connect(ui->actionRestart_Program, &QAction::triggered, this, [=,this] { MW_dialog_message(MwMessage::RestartProgram, {}); });
    connect(ui->actionShow_window, &QAction::triggered, this, [=,this] { ActivateWindow(this); });
    connect(ui->actionRemember_last_proxy, &QAction::triggered, this, [=,this](bool checked) {
        Configs::dataManager->settingsRepo->remember_enable = checked;
        ui->actionRemember_last_proxy->setChecked(checked);
        Configs::dataManager->settingsRepo->Save();
    });
    connect(ui->actionStart_with_system, &QAction::triggered, this, [=,this](bool checked) {
        AutoRun_SetEnabled(checked);
        ui->actionStart_with_system->setChecked(checked);
    });
    connect(ui->actionAllow_LAN, &QAction::triggered, this, [=,this](bool checked) {
        Configs::dataManager->settingsRepo->inbound_address = checked ? "::" : "127.0.0.1";
        ui->actionAllow_LAN->setChecked(checked);
        MW_dialog_message(MwMessage::UpdateSettings, {});
    });
    //
    connect(ui->checkBox_VPN, &QCheckBox::toggled, this, [=,this](bool checked) { set_spmode_vpn(checked); });
    connect(ui->checkBox_SystemProxy, &QCheckBox::toggled, this, [=,this](bool checked) { set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode, &QMenu::aboutToShow, this, [=,this]() {
        ui->menu_spmode_disabled->setChecked(!(Configs::dataManager->settingsRepo->spmode_system_proxy || Configs::dataManager->settingsRepo->spmode_vpn));
        ui->menu_spmode_system_proxy->setChecked(Configs::dataManager->settingsRepo->spmode_system_proxy);
        ui->menu_spmode_vpn->setChecked(Configs::dataManager->settingsRepo->spmode_vpn);
    });
    connect(ui->menu_spmode_system_proxy, &QAction::triggered, this, [=,this](bool checked) { set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode_vpn, &QAction::triggered, this, [=,this](bool checked) { set_spmode_vpn(checked); });
    connect(ui->menu_spmode_disabled, &QAction::triggered, this, [=,this]() {
        set_spmode_system_proxy(false);
        set_spmode_vpn(false);
    });
    connect(ui->menu_qr, &QAction::triggered, this, [=,this]() { display_qr_link(false); });
    connect(ui->system_dns, &QCheckBox::clicked, this, [=,this](bool checked) {
        if (const auto ok = set_system_dns(checked); !ok) {
            ui->system_dns->setChecked(!checked);
        } else {
            refresh_status();
        }
    });
    if (Configs::dataManager->settingsRepo->show_system_dns) ui->system_dns->show();
    else ui->system_dns->hide();

    connect(ui->menu_server, &QMenu::aboutToShow, this, [=,this](){
        // Everything in the Test submenu acts on the selection, so it follows it as a
        // whole -- entries added later do not have to be remembered here.
        const bool hasSelection = !get_now_selected_list().empty();
        for (auto *action : ui->menu_test_item->actions()) action->setEnabled(hasSelection);
        ui->menu_test_item->setEnabled(hasSelection);
        ui->menu_resolve_selected->setEnabled(hasSelection);
        ui->actionResolve_Selected_Out_IP->setEnabled(hasSelection);
        if (testRunner->isRunning()) {
            ui->menu_server->addAction(ui->menu_stop_testing);
        } else {
            ui->menu_server->removeAction(ui->menu_stop_testing);
        }
    });

    connect(ui->menuTesting, &QMenu::aboutToShow, this, [=,this](){
        ui->actionDelete_Group->setEnabled(Configs::dataManager->groupsRepo->GetAllGroupIds().size() > 1);
        if (testRunner->isRunning()) {
            ui->menuTesting->addAction(ui->menu_stop_testing);
        } else {
            ui->menuTesting->removeAction(ui->menu_stop_testing);
        }
    });

    connect(ui->menuTools, &QMenu::aboutToShow, this, [=,this](){
        ui->actionSpeedtest_Current->setEnabled(running != nullptr);
    });

    connect(ui->actionAdd_New_Group, &QAction::triggered, this, [=,this]{
        auto ent = Configs::dataManager->groupsRepo->NewGroup();
        auto dialog = new DialogEditGroup(ent, this);
        int ret = dialog->exec();
        dialog->deleteLater();

        if (ret == QDialog::Accepted) {
            Configs::dataManager->groupsRepo->AddGroup(ent);
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
    });

    connect(ui->actionEdit_Group, &QAction::triggered, this, [=,this]{
        auto ent = Configs::dataManager->groupsRepo->CurrentGroup();
        auto dialog = new DialogEditGroup(ent, this);
        connect(dialog, &QDialog::finished, this, [=,this] {
            if (dialog->result() == QDialog::Accepted) {
                Configs::dataManager->groupsRepo->Save(ent);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
            dialog->deleteLater();
        });
        dialog->show();
    });

    connect(ui->actionDelete_Group, &QAction::triggered, this, [=,this]{
        if (Configs::dataManager->groupsRepo->GetAllGroupIds().size() <= 1) return;
        auto id = Configs::dataManager->groupsRepo->CurrentGroup()->id;
        if (QMessageBox::question(this, tr("Confirmation"), tr("Remove %1?").arg(Configs::dataManager->groupsRepo->GetGroup(id)->name)) ==
            QMessageBox::StandardButton::Yes) {
            if (running != nullptr) {
                if (running->gid == id) profile_stop(false, true, false);
            }
            Configs::dataManager->groupsRepo->DeleteGroup(id);
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
    });

    connect(ui->actionUpdate_All_Subscriptions, &QAction::triggered, this, [=,this]{
        if (QMessageBox::question(this, tr("Confirmation"), tr("Update all subscriptions?")) == QMessageBox::StandardButton::Yes) {
            UI_update_all_groups();
        }
    });

    connect(ui->actionRefresh_Column_Widths, &QAction::triggered, this, [=, this] {
        auto ent = Configs::dataManager->groupsRepo->CurrentGroup();
        ent->column_width.clear();
        Configs::dataManager->groupsRepo->Save(ent);
        show_group(ent->id);
    });

    connect(ui->menuRouting_Menu, &QMenu::aboutToShow, this, [=,this]()
    {
        ui->menuRouting_Menu->clear();
        ui->menuRouting_Menu->addAction(ui->menu_routing_settings);

        auto* actionAdblock = new QAction(ui->menuRouting_Menu);
        actionAdblock->setText(tr("Enable AdBlock"));
        actionAdblock->setCheckable(true);
        actionAdblock->setChecked(Configs::dataManager->settingsRepo->adblock_enable);
        connect(actionAdblock, &QAction::triggered, this, [=,this](bool checked) {
            Configs::dataManager->settingsRepo->adblock_enable = checked;
            actionAdblock->setChecked(checked);
            Configs::dataManager->settingsRepo->Save();
            if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
        });
        ui->menuRouting_Menu->addAction(actionAdblock);

        auto* actionWarp = new QAction(ui->menuRouting_Menu);
        actionWarp->setText(tr("Enable Warp"));
        actionWarp->setCheckable(true);
        actionWarp->setChecked(Configs::dataManager->settingsRepo->enable_warp);
        connect(actionWarp, &QAction::triggered, this, [=,this](bool checked) {
            Configs::dataManager->settingsRepo->enable_warp = checked;
            actionWarp->setChecked(checked);
            Configs::dataManager->settingsRepo->Save();
            if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
        });
        ui->menuRouting_Menu->addAction(actionWarp);

        QMenu* profilesMenu = ui->menuRouting_Menu->addMenu(QObject::tr("Download Profiles"));
        for (const QString &country : QStringList{"China", "Iran", "Russia"})
        {
            auto* action = new QAction(profilesMenu);
            action->setText(country);
            connect(action, &QAction::triggered, this, [=,this]()
            {
                auto resp = NetworkRequestHelper::HttpGet(Configs::get_jsdelivr_link("https://raw.githubusercontent.com/throneproj/routeprofiles/profile/Profile_" + country));
                if (!resp.error.isEmpty()) {
                    runOnUiThread([=] {
                        MessageBoxWarning(QObject::tr("Download Profiles"), QObject::tr("Requesting profile error: %1").arg(resp.error + "\n" + resp.data));
                    });
                    return;
                }
                handle_add_remote_routes(resp.data);
            });
            profilesMenu->addAction(action);
        }

        ui->menuRouting_Menu->addSeparator();
        for (const auto& route : Configs::dataManager->routesRepo->GetAllRouteProfiles())
        {
            auto* action = new QAction(ui->menuRouting_Menu);
            action->setText(route->name);
            action->setData(route->id);
            action->setCheckable(true);
            action->setChecked(Configs::dataManager->settingsRepo->current_route_id == route->id);
            connect(action, &QAction::triggered, this, [=,this]()
            {
                auto routeID = action->data().toInt();
                if (Configs::dataManager->settingsRepo->current_route_id == routeID) return;
                Configs::dataManager->settingsRepo->current_route_id = routeID;
                Configs::dataManager->settingsRepo->Save();
                if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
            });
            ui->menuRouting_Menu->addAction(action);
        }
    });
    connect(ui->actionClear_Test_Result, &QAction::triggered, this, [=, this]() {
        auto entIDs = get_now_selected_list();
        auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
        if (ents.empty()) return;
        for (const auto &ent: ents) {
            ent->ClearTestResults();
        }
        Configs::dataManager->profilesRepo->SaveBatch(ents);
        if (auto group = Configs::dataManager->groupsRepo->GetGroup(ents.first()->gid); group &&
            group->calculated_column_width.size() > ProfilesTableModel::ColTestResult)
            group->calculated_column_width[ProfilesTableModel::ColTestResult] = 0;
        refresh_proxy_list();
    });
    connect(ui->actionUrl_Test_Selected, &QAction::triggered, this, [=,this]() {
        testRunner->runUrlTests(get_now_selected_list());
    });
    // A url test only reports that something timed out. This walks the same path
    // stage by stage, so the one that broke names itself.
    auto *diagnoseAction = new QAction(tr("Diagnose Selected"), this);
    ui->menu_test_item->insertAction(ui->actionClear_Test_Result, diagnoseAction);
    connect(diagnoseAction, &QAction::triggered, this, [=,this]() {
        const auto selected = get_now_selected_list();
        if (selected.isEmpty()) return;
        testRunner->runDiagnostics(selected.first());
    });
    connect(ui->actionUrl_Test_Group, &QAction::triggered, this, [=,this]() {
        testRunner->runUrlTests(Configs::dataManager->groupsRepo->CurrentGroup()->Profiles());
    });
    connect(ui->actionSpeedtest_Current, &QAction::triggered, this, [=,this]()
    {
        if (running != nullptr)
        {
            testRunner->runSpeedTests({}, true);
        }
    });
    connect(ui->actionSpeedtest_Selected, &QAction::triggered, this, [=,this]()
    {
        testRunner->runSpeedTests(get_now_selected_list());
    });
    connect(ui->actionSpeedtest_Group, &QAction::triggered, this, [=,this]()
    {
        testRunner->runSpeedTests(Configs::dataManager->groupsRepo->CurrentGroup()->Profiles());
    });
    connect(ui->actionResolve_Selected_Out_IP, &QAction::triggered, this, [=,this]() {
        testRunner->runIpTests(get_now_selected_list());
    });
    connect(ui->actionResolve_Out_IP, &QAction::triggered, this, [=,this]() {
        testRunner->runIpTests(Configs::dataManager->groupsRepo->CurrentGroup()->Profiles());
    });
    connect(ui->menu_stop_testing, &QAction::triggered, this, [=,this]() { testRunner->stop(); });
    auto set_selected_or_group = [=,this](int mode) {
        // 0=group 1=select 2=unknown(menu is hide)
        ui->menu_server->setProperty("selected_or_group", mode);
    };
    connect(ui->menu_server, &QMenu::aboutToHide, this, [=,this] {
        setTimeout([=,this] { set_selected_or_group(2); }, this, 200);
    });
    set_selected_or_group(2);
    connect(ui->menu_share_item, &QMenu::aboutToShow, this, [=,this] {
        QString name;
        auto selected = get_now_selected_list();

        ui->menu_export_config->setVisible(false);
        ui->actionExport_Xray_config->setVisible(false);
        if (selected.isEmpty()) return;

        auto profile = Configs::dataManager->profilesRepo->GetProfile(selected.first());
        if (!profile) return;

        if (selected.count() == 1 && profile->DisplayTestResult().trimmed().isEmpty()) {
            ui->actionCopy_Test_Result->setVisible(false);
        } else {
            ui->actionCopy_Test_Result->setVisible(true);
        }

        ui->menu_export_config->setVisible(true);
        if (profile->outbound->IsXray() || profile->type == "chain") ui->actionExport_Xray_config->setVisible(true);
    });
    connect(ui->actionExport_Xray_config, &QAction::triggered, this, [=,this]() {
        auto ents = get_now_selected_list();
        if (ents.count() != 1) return;
        auto ent = Configs::dataManager->profilesRepo->GetProfile(ents.first());

        auto result = Configs::BuildSingBoxConfig(ent);
        if (!result->error.isEmpty()) {
            MessageBoxWarning("Build config error", result->error);
            return;
        }
        const QString singConfig = QJsonObject2QString(result->coreConfig, true);
        QStringList xrayConfigs;
        if (!result->xrayConfig.isEmpty()) {
            xrayConfigs << QJsonObject2QString(result->xrayConfig, true);
        }
        xrayConfigs.append(result->xrayFullConfigs);
        QString config_core = xrayConfigs.join("\n\n");
        const QString pairedConfigs =
            QStringLiteral("=== sing-box ===\n") + singConfig +
            QStringLiteral("\n\n=== Xray ===\n") + config_core;
        QApplication::clipboard()->setText(config_core);

        QMessageBox msg(QMessageBox::Information, tr("Config copied"), config_core);
        QPushButton *button_1 = msg.addButton(tr("Copy sing-box config"), QMessageBox::YesRole);
        QPushButton *button_pair = msg.addButton(tr("Copy paired configs"), QMessageBox::YesRole);
        QPushButton *button_2 = msg.addButton(tr("Copy test config"), QMessageBox::YesRole);
        msg.addButton(QMessageBox::Ok);
        msg.setEscapeButton(QMessageBox::Ok);
        msg.setDefaultButton(QMessageBox::Ok);
        msg.exec();
        if (msg.clickedButton() == button_1) {
            QApplication::clipboard()->setText(singConfig);
        } else if (msg.clickedButton() == button_pair) {
            QApplication::clipboard()->setText(pairedConfigs);
        } else if (msg.clickedButton() == button_2) {
            auto res = Configs::BuildTestConfig({ent});
            if (!res->error.isEmpty()) {
                MessageBoxWarning("Build Test config error", res->error);
                return;
            }
            config_core = QJsonObject2QString(res->xrayConfig, true);
            QApplication::clipboard()->setText(config_core);
        }
    });
    connect(ui->actionCopy_Test_Result, &QAction::triggered, this, [=,this]() {
        auto ents = get_now_selected_list();
        if (ents.count() == 0 || ents.count() > 1000) return;
        auto entList = Configs::dataManager->profilesRepo->GetProfileBatch(ents);
        QString res;
        int counter = 0;
        for (auto ent : entList) {
            auto testRes = ent->DisplayTestResult();
            if (!testRes.trimmed().isEmpty()) {
                res += testRes.trimmed() + "\n";
                counter++;
            }
        }
        QApplication::clipboard()->setText(res);
        MW_show_log(QString::number(counter) + tr(" Test result(s) copied to clipboard!"));
    });
    connect(ui->actionAdd_profile_from_File, &QAction::triggered, this, [=,this]()
    {
        // QFileDialog defaults to the first filter; config files routinely carry no extension.
        const auto filters = QStringList{
            tr("All files (*)"),
            tr("Config files (*.json *.conf *.txt *.yaml *.yml *.ini)"),
            tr("QR code images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"),
        };
        const auto paths = QFileDialog::getOpenFileNames(this, tr("Select profile files"), QString(), filters.join(";;"));
        if (paths.isEmpty()) return;
        importFromFiles(paths);
    });

    connect(qApp, &QGuiApplication::commitDataRequest, this, &MainWindow::on_commitDataRequest);

    if (!uiPreviewMode) {
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { refresh_status(); });
        timer->start(2000);

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [] { Configs_sys::logCounter.fetchAndStoreRelaxed(0); });
        timer->start(1000);
    }

    m_proxyListRefreshDebounce = new QTimer(this);
    m_proxyListRefreshDebounce->setSingleShot(true);
    connect(m_proxyListRefreshDebounce, &QTimer::timeout, this, [this] { refresh_proxy_list({}, false); });

    // The selector monitor emits from its own poll thread.
    connect(Stats::autoSelectorMonitor, &Stats::AutoSelectorMonitor::poolExhausted, this,
            [this](int profileID) { on_auto_selector_exhausted(profileID); }, Qt::QueuedConnection);
    connect(Stats::autoSelectorMonitor, &Stats::AutoSelectorMonitor::updated, this,
            [this] { refresh_auto_selector_view(); }, Qt::QueuedConnection);

    {
        auto* runner = Throne::PeriodicRunner::instance();
        // Interval is sign-encoded in settings (negative = disabled); < 30 min counts as off.
        const auto minutesOf = [](int v) { return v >= 30 ? v : 0; };
        runner->Add({
            tr("subscriptions"),
            [minutesOf] { return minutesOf(Configs::dataManager->settingsRepo->sub_auto_update); },
            [] { return Configs::dataManager->settingsRepo->sub_auto_update_last; },
            [](qint64 t) {
                Configs::dataManager->settingsRepo->sub_auto_update_last = t;
                Configs::dataManager->settingsRepo->Save();
            },
            [] { UI_update_all_groups(true); },
        });
        runner->Add({
            tr("routing profiles"),
            [minutesOf] { return minutesOf(Configs::dataManager->settingsRepo->route_auto_update); },
            [] { return Configs::dataManager->settingsRepo->route_auto_update_last; },
            [](qint64 t) {
                Configs::dataManager->settingsRepo->route_auto_update_last = t;
                Configs::dataManager->settingsRepo->Save();
            },
            [] { UI_update_all_remote_routes(true); },
        });
        runner->Add({
            tr("updates"),
            // Release checks are cheap and rate-limited by GitHub, so they are not
            // held to the 30-minute floor the subscription sweeps use.
            [] {
                const int v = Configs::dataManager->settingsRepo->app_auto_update;
                return v > 0 ? v : 0;
            },
            [] { return Configs::dataManager->settingsRepo->app_auto_update_last; },
            [](qint64 t) {
                Configs::dataManager->settingsRepo->app_auto_update_last = t;
                Configs::dataManager->settingsRepo->Save();
            },
            [this] { runOnNewThread([this] { CheckUpdate(true); }); },
        });
    }

    // The control surface reaches the config layer on its own; these are the
    // few operations only the window can perform.
    ThronedControl::hooks.startProfile = [this](int id) { profile_start(id); };
    ThronedControl::hooks.stopProfile = [this] { profile_stop(false, false, true); };
    ThronedControl::hooks.runningProfileId = [this] { return running ? running->id : -1; };
    ThronedControl::hooks.setTun = [this](bool enabled) { set_spmode_vpn(enabled); };
    ThronedControl::hooks.setSystemProxy = [this](bool enabled) { set_spmode_system_proxy(enabled); };
    ThronedControl::hooks.isElevated = [] { return Configs::IsAdmin(); };
    ThronedControl::hooks.updateSubscriptions = [] { UI_update_all_groups(false); };
    ThronedControl::hooks.recentLogs = [this](int wanted) {
        // The window's log document is the same text the Logs tab shows, so a
        // caller sees exactly what a person would be reading.
        const QStringList all = qvLogDocument->toPlainText().split('\n', Qt::SkipEmptyParts);
        return all.mid(qMax(0, all.size() - wanted));
    };
    ThronedControl::hooks.applyRoutingChange = [this] {
        refreshRoutingStatus();
        if (Configs::dataManager->settingsRepo->started_id >= 0)
            profile_start(Configs::dataManager->settingsRepo->started_id);
    };

    connect(tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (!pendingUpdatePrompt) return;
        const auto prompt = std::exchange(pendingUpdatePrompt, {});
        prompt();
    });

    if (!Configs::dataManager->settingsRepo->flag_tray) show();

    ui->data_view->setStyleSheet("background: transparent; border: none;");
}

MainWindow::~MainWindow() {
    delete ui;
}
