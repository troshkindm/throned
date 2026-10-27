#pragma once

#include <QMainWindow>
#include <include/global/HTTPRequestHelper.hpp>
#ifndef Q_MOC_RUN
#include <core/gen/libcore.pb.h>
#endif

#include "include/global/Configs.hpp"
#include "include/stats/connections/connectionLister.hpp"
#include "include/ui/widget/ThroughputChart.h"
#include "include/ui/stats/MiniChartWidget.h"
#include "include/stats/traffic/TrafficLine.hpp"
#include "include/database/entities/Profile.h"
#include "include/database/entities/RouteProfile.h"
#include "include/database/entities/AppRoutes.h"
#ifdef Q_OS_LINUX
#include <QtDBus>
#endif

#include <deque>
#include <optional>
#include <QKeyEvent>
#include "include/ui/widget/TrayIcon.hpp"
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QQueue>
#include <QWaitCondition>
#include <QProcess>
#include <QTextDocument>
#include <QShortcut>
#include <QKeySequence>
#include <QSet>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QSemaphore>
#include <QMutex>
#include <QThreadPool>

#include "include/database/entities/GroupSort.h"
#include "include/engine/EnginePolicy.h"
#include "include/engine/EngineTypes.h"
#include "include/engine/tests/TestObserver.h"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/utils/DataViewHtmlGenerator.h"
#include "include/ui/utils/GuiUtils.hpp"
#include "include/ui/utils/ProfilesFilterProxyModel.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "ui_mainwindow.h"

class MainWindow;
namespace UiPreview {
void VerifyTestProgress(MainWindow *window, const QString &prefix);
}
namespace Configs {
enum simpleAction : int;
struct SimpleTarget;
} // namespace Configs

namespace Configs {
class Group;
struct SubInfo;
namespace AppRoutes {
struct Route;
}
} // namespace Configs

namespace Engine {
class ConnectionManager;
class CoreSupervisor;
class ExitController;
class TestRunner;
class VpnAuthController;
} // namespace Engine

class QMessageBox;
class TrayProfileSelector;
class RoutingQuickMenu;
class TrayOtpCodes;
class GlobalHotkeys;
class DialogVpnAuth;
class UpdateStatusWidget;
class TestProgressWidget;
class PendingRestartNotice;
class HijackDeprecationNotice;
class DialogScanner;
class DialogIpLists;
class CommandPalette;

namespace Qv2ray::ui {
class SyntaxHighlighter;
}
namespace SettingsIndex {
struct Item;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

enum class RefreshAnchor {
    // Re-select the same profiles by id; select nothing if they are gone.
    KeepPlace,
    // As above, but if all of them were deleted select whatever took their row.
    Removal,
};

#include "include/ui/stats/diagnostics_window.h"

using ExitReason = Engine::ExitReason;

class MainWindow : public QMainWindow, public Engine::EnginePolicy, public Engine::TestObserver {
    Q_OBJECT

public:
    using ConnectionState = Engine::ConnectionState;
    using StartOutcome = Engine::StartOutcome;
    using StartRequest = Engine::StartRequest;
    using ModeChange = Engine::ModeChange;

    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

    void refresh_proxy_list(const QList<int> &ids = {}, bool mayNeedReset = false,
                            RefreshAnchor anchor = RefreshAnchor::KeepPlace);

    void show_group(int gid);

    void show_group_tab_menu(const QPoint &tabBarPos);

    void refresh_groups();

    // Paints the subscription allowance onto its group tab and puts the numbers in the tooltip.
    void applySubscriptionReadout(int index, const std::shared_ptr<Configs::Group> &group);
    void notifySubscriptionState(const std::shared_ptr<Configs::Group> &group,
                                 const Configs::SubInfo &sub, int days);

    void refreshSubscriptionReadouts();

    void setupStartPickMenu();

    void runSiteReachability(const QList<int> &profileIDs);

    void clearProfileSelection();

    void setupAnnounceStrip();

    void refreshAnnounceStrip();

    void dismissAnnounce();

    void showSubscriptionPopover(int tabIndex);

    void hoverSubscriptionCard(int tabIndex);

    void endSubscriptionHover();

    class SubscriptionPopover *subscriptionCard();

    void updateTabToolTip(int gid);

    void refresh_status(const QString &traffic_update = "");

    // Lines of the config about to run; must arrive before its first add_traffic_point.
    void set_traffic_lines(const QList<Stats::TrafficLine> &lines);

    // values: [down, up] per line of the last set_traffic_lines, in its order.
    void add_traffic_point(qint64 monoMs, bool coreRunning, const QVector<double> &values,
                           double proxyDown, double proxyUp, double directDown, double directUp);

    // User start actions only: in select mode the profile becomes the pick. Engine restarts call the ConnectionManager directly.
    void profile_start(int _id = -1);

    // One paste with the state a support answer needs, secrets masked.
    QString collectDiagnostics();

    void copyDiagnostics();
    void openDiagnostics(const QString &processKey = {}, int section = -1);
    void openDiagnosticsTarget(const QString &target);
    DiagnosticsWindow::LocalState diagnosticsLocalState();
    DiagnosticsWindow::Usage diagnosticsUsage(int rangeDays);
    // Unix ms when the core last came up; 0 when it is not running.
    qint64 coreStartedAt = 0;

    // Refresh the routing segment of the status bar from the active profile.
    void refreshRoutingStatus();

    // The UDP column stays out of the way until the group has something to put in it.
    void refreshUdpColumnVisibility();

    // Switches the profile table between the compact one-line rows and the
    // two-line ones, including the column set the model exposes.
    void refreshProfileRowStyle();

    // The log/connections panel collapses to its tab strip; the strip stays
    // visible so the panel can be found again.
    void setStatsPanelOpen(bool open, bool save = true);

    void profile_stop(bool crash = false, bool block = false, bool manual = false, bool interactive = true);
    void profile_start(const StartRequest &request);

    int get_profile_to_start();

    // Safe from any thread.
    std::shared_ptr<Configs::Profile> runningSnapshot() const;

    void toggle_system_proxy();

    void start_select_mode(QObject *context, const std::function<void(int)> &callback);

    // Returns a line per global hotkey that could not be registered.
    QStringList RegisterHotkey(bool unregister);

    bool IsGlobalHotkeySupported() const;

    bool StopVPNProcess();

    // Whole poll snapshot in the lister's order, never a delta. UI thread only.
    void UpdateConnectionList(const QList<Stats::ConnectionMetadata> &connections);

    void UpdateDataView(bool force = false);

    // Pushes the auto-selector snapshot into the data view, toggles the Tools
    // entry, and refreshes the dialog if it is open.
    void refresh_auto_selector_view();

    // Non-owning: cleared by the dialog's finished() handler.
    class DialogAutoSelector *m_autoSelectorDialog = nullptr;

    void setDownloadReport(const DownloadProgressReport &report, bool show);

    void showIpListsDialog(int selectListId = -1);

    void showScannerDialog();

    void refreshScannerDataView(bool force = false);

signals:

    void profile_selected(int id);

public slots:

    void on_commitDataRequest();

    void on_menu_exit_triggered();

private slots:

    void on_masterLogBrowser_customContextMenuRequested(const QPoint &pos);

    void on_menu_basic_settings_triggered();

    void on_menu_routing_settings_triggered();

    void on_menu_vpn_settings_triggered();

    void on_menu_dpi_bypass_triggered();

    void on_menu_preset_settings_triggered();

    void on_menu_otp_manager_triggered();

    void on_menu_scanner_triggered();

    void on_menu_integration_settings_triggered();

    void on_menu_add_from_input_triggered();

    void on_menu_add_from_clipboard_triggered();

    void on_menu_clone_triggered();

    void on_menu_delete_repeat_triggered();

    void on_menu_delete_triggered();

    void on_menu_reset_traffic_triggered();

    void on_menu_copy_links_triggered();

    void on_menu_copy_links_nkr_triggered();

    void on_menu_export_config_triggered();

    void display_qr_link(bool nkrFormat = false);

    void on_menu_scan_qr_triggered();

    void on_menu_clear_test_result_triggered();

    void on_menu_manage_groups_triggered();

    void on_menu_select_all_triggered();

    void on_menu_remove_unavailable_triggered();

    void on_menu_remove_invalid_triggered();

    void on_menu_remove_insecure_triggered();

    void on_menu_resolve_selected_triggered();

    void on_menu_resolve_domain_triggered();

    void on_menu_update_subscription_triggered();

    void on_profilesTableView_doubleClicked(const QModelIndex &index);

    void on_profilesTableView_customContextMenuRequested(const QPoint &pos);

    void on_tabWidget_currentChanged(int index);

    void on_tabWidget_customContextMenuRequested(const QPoint &p);

private:
    Ui::MainWindow *ui;
    // Monotonic, and invalid while the window is active or was never activated; see trayClickEvent().
    QElapsedTimer sinceWindowDeactivated;
    ProfilesTableModel *profilesTableModel = nullptr;
    class ProfileRowDelegate *profileRowDelegate = nullptr;
    class QStyledItemDelegate *compactRowDelegate = nullptr;
    class QToolButton *statsPanelToggle = nullptr;
    int statsStripHeight = 39;
    bool m_searchedEveryGroup = false;
    class QVariantAnimation *rowFlashAnimation = nullptr;
    class QToolButton *favoritesButton = nullptr;
    class QToolButton *groupAddButton = nullptr;
    class QAction *favoriteAction = nullptr;
    class QAction *clearSelectionAction = nullptr;
    class QWidget *profilesEmptyState = nullptr;
    class QLabel *profilesEmptyIcon = nullptr;
    class QLabel *profilesEmptyTitle = nullptr;
    class QLabel *profilesEmptySub = nullptr;
    class QPushButton *profilesEmptyAction = nullptr;
    class QuickAddOverlay *quickAddOverlay = nullptr;
    class QToolButton *statsStripToggle = nullptr;
    class QFrame *statsStrip = nullptr;
    class QVariantAnimation *statsPanelAnimation = nullptr;
    qreal statsPanelOpenProgress = 0.0;
    // Direct child of the splitter that owns stats_widget. Hiding only the tab
    // widget leaves this wrapper in the splitter, where it keeps an empty share
    // of the window height.
    class QWidget *statsPanelHost = nullptr;
    QList<class QToolButton *> statsStripTabs;
    class QLabel *statsConnectionTabCount = nullptr;
    // The Connections strip tab: it carries the live count in its own label.
    class QToolButton *statsConnectionStripCount = nullptr;
    class QActionGroup *logLevelActions = nullptr;
    QList<QWidget *> statsPanelTools;
    class QMenu *startPickMenu = nullptr;
    // The subscription notice rides with the profile table into the current group page.
    class QWidget *announceHost = nullptr;
    class QLabel *announceIcon = nullptr;
    class QLabel *announceText = nullptr;
    class QToolButton *announceClose = nullptr;
    class SubscriptionPopover *subscriptionPopover = nullptr;
    QTimer *subscriptionHoverDelay = nullptr;
    // What the view is attached to: rows from the view or its selection model are
    // proxy rows, not profilesTableModel rows.
    ProfilesFilterProxyModel *profilesFilterModel = nullptr;
    TrayIcon *tray = nullptr;
    QMenu *trayMenu = nullptr;
    // Tray "Select Server"/"Select Routing" open this small Qt-drawn popup instead of a
    // submenu, because a tray submenu isn't painted by Qt on Linux (SNI/DBusMenu) or macOS
    // (native NSMenu) and so can't reliably expand a dynamic list. Recreated on each open.
    QAction *trayConnectAction = nullptr;
    QPointer<TrayProfileSelector> traySelector;
    QPointer<DiagnosticsWindow> diagnosticsWindow;
    void openTraySelector(bool routing);
    QPointer<RoutingQuickMenu> routingQuickMenu;
    void openRoutingQuickMenu(const QPoint &globalPos);
    QLabel *statusConnectionCaption = nullptr;
    // One-click "test this connection"; the right-click menu on the label stays too.
    class QToolButton *statusConnectionTest = nullptr;
    QLabel *statusDirectSpeed = nullptr;
    QList<QPointer<QLabel>> statusElidedLabels;
    void setStatusText(QLabel *label, const QString &text);
    void addRuleFromConnection(const QString &entry, int action);
    void addRulesFromConnection(const QStringList &entries, int action);
    QPointer<TrayOtpCodes> trayOtpCodes;
    void openTrayOtpCodes();
    QShortcut *shortcut_esc = new QShortcut(QKeySequence::Cancel, this);
    //
    // Shared by the test sweeps and the batch profile scans (remove-invalid).
    QThreadPool *parallelCoreCallPool = new QThreadPool(this);
    // Engine::host()'s objects, in mainwindow_engine_control.cpp.
    Engine::ConnectionManager *connection() const;
    Engine::CoreSupervisor *supervisor() const;
    Engine::TestRunner *tests() const;
    Engine::VpnAuthController *vpnAuth() const;
    Engine::ExitController *exitController() const;
    QTextDocument *qvLogDocument = new QTextDocument(this);
    QString title_error;
    std::optional<Icon::TrayIconStatus> icon_status;
    QString traffic_update_cache;
    qint64 last_test_time = 0;
    bool select_mode = false;
    // Esc leaves select mode; enabled only meanwhile, so the profile table keeps Esc for clearing its selection.
    QShortcut *shortcut_select_cancel = nullptr;
    void finish_select_mode(int profileId);
    ExitReason exit_reason = ExitReason::None;
    // Set by request_unattended_exit: no dialog during the teardown, and a restart stays in the tray when the window was hidden.
    bool m_exitUnattended = false;
    bool m_exitKeepTray = false;
    QMutex mu_download_update;
    QMutex mu_download_dashboard;
    class ConnectionsTreeModel *connectionsModel = nullptr;
    class ConnectionsTreeFilterProxyModel *connectionsFilterModel = nullptr;
    class ConnectionsFilterHeader *connectionFilterHeader = nullptr;
    QHash<QString, bool> m_processExpanded; // per-process choices; the rest follow m_processesExpandedByDefault
    bool m_processesExpandedByDefault = false;
    QTimer *connectionFilterDebounce = nullptr;
    QToolButton *connectionExpandButton = nullptr;
    QToolButton *connectionCloseAllButton = nullptr;
    QToolButton *connectionFilterButton = nullptr;
    QIcon connectionCloseIcon;
    QIcon connectionExpandIcon;
    QIcon connectionCollapseIcon;
    int toolTipID;
    ThroughputChart *speedChartWidget;
    QList<Stats::TrafficLine> m_trafficLines;
    using LiveRates = Engine::LiveRates;
    class RuntimeStatsWidget *runtimeStatsWidget = nullptr;
    // Latency over time, beside the throughput chart: a path can carry bytes fine
    // and still have a UDP round trip that wanders, which is what breaks QUIC.
    MiniChartWidget *pingChartWidget = nullptr;
    QCheckBox *pingMonitorToggle = nullptr;
    QToolButton *pingTargetsButton = nullptr;
    QLabel *pingLegendLabel = nullptr;
    QTimer *pingMonitorTimer = nullptr;
    std::atomic<bool> pingProbeInFlight_{false};
    UpdateStatusWidget *updateStatusWidget = nullptr;
    TestProgressWidget *testProgressWidget = nullptr;
    PendingRestartNotice *pendingRestartNotice = nullptr;
    HijackDeprecationNotice *hijackDeprecationNotice = nullptr;
    QString pendingUpdateAssetName;
    QString pendingUpdateDownloadUrl;
    std::atomic<bool> updateCheckInProgress_{false};
    std::atomic<bool> updateCheckRetryAfterConnect_{false};
    std::atomic<qint64> lastUpdateProgressMs_{0};

    // One tick of the monitor. Both paths are measured because a number on its own
    // cannot say whether the proxy or the connection underneath it went bad.
    struct PingSample {
        qint64 at = 0; // epoch seconds
        QStringList targets;
        QList<int> proxyMs; // -1 means nothing came back within the timeout
        int directMs = -1;
    };
    QList<PingSample> pingHistory_;
    // Edge-triggered so one bad stretch produces one log line, not fifty.
    bool pingSpikeActive_ = false;

    void pollPingMonitor();

    void recordPingSample(const QStringList &targets, const QList<int> &proxyMs, int directMs);

    [[nodiscard]] QStringList pingMonitorTargets() const;

    void setPingMonitorTargets(const QStringList &targets, bool save);

    void rebuildPingTargetsMenu();

    void updatePingLegend(const QStringList &targets, const QList<int> &proxyMs = {}, int directMs = -2);

    [[nodiscard]] QString pingHistoryReport() const;
    //
    // for data view
    // Repaint throttle, in ms since epoch. Atomic: worker threads drive it too.
    std::atomic<qint64> lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();
    DataViewHtmlGenerator dataViewHtmlGenerator_;

    QList<QShortcut *> hiddenMenuShortcuts;
    GlobalHotkeys *globalHotkeys = nullptr;

    QString addressFilterString;
    QString nameFilterString;
    QString typeFilterString;
    QString countryFilterString;
    QString globalFilterString;

    QTimer *m_filterRefreshDebounce = nullptr;

    // Only meaningful between a saveProfileFocusState() and its restore.
    bool m_profilesTableHadFocus = false;
    int m_profilesScrollValue = 0;

    QStringList includeKeywords;
    QStringList excludeKeywords;
    QRegularExpression includeCombined;
    QRegularExpression excludeCombined;
    // -1 until the first updateLogFilterFields(), so startup is not treated as a change.
    int minLogLevelRank = -1;
    // Level the running core was configured with, so a selection below it can say so.
    int coreLogLevelRank_ = -1;
    QMutex logMutex;
    QQueue<QString> logQueue;
    QWaitCondition logWaiter;
    Qv2ray::ui::SyntaxHighlighter *logHighlighter = nullptr;

    QMutex logPendingMutex;
    QString logPendingText;
    bool logFlushScheduled = false;

    // Immutable snapshot of the log filter fields. The log thread copies these
    // under logMutex (Qt containers are copy-on-write, so it's O(1)) and then
    // filters without holding the lock, so producers calling append_log() are
    // never blocked on the regex/keyword work.
    // UI-thread view state. m_logLines is what the view renders (capped at max_log_line);
    // while the user is scrolled up, arrivals wait in m_logHeld so the view stays frozen.
    struct LogLine {
        QString text;
        bool visible = true;
    };
    std::deque<LogLine> m_logLines;
    std::deque<QString> m_logHeld;
    bool m_logFollow = true;
    QRegularExpression m_logSearch;
    QTimer *m_logSearchDebounce = nullptr;
    QToolButton *logFilterButton = nullptr;
    QToolButton *logJumpLatestButton = nullptr;

    struct LogFilter {
        bool enableInclude = false;
        bool enableExclude = false;
        QStringList includeKeywords;
        QStringList excludeKeywords;
        QRegularExpression includeCombined;
        QRegularExpression excludeCombined;
        int minLevelRank = 0;
    };

    void append_log(const QString &log);

    void log_process_loop();

    void flush_log_batch();

    void setupLogView();

    void releaseHeldLogs();

    // Caps both queues at max_log_line; returns how many dropped lines were visible (rendered blocks).
    int trimLogLines();

    void rebuildLogView();

    void applyLogSearch();

    void setLogFilterVisible(bool visible);

    void updateLogStatus();

    bool should_print_log(const QString &log, const LogFilter &filter);

    void clear_log_view();

    void updateLogFilterFields();

    // (Re)installs the log syntax highlighter, deleting any previous one so
    // highlighters don't stack up (and keep re-highlighting) on theme changes.
    void setLogHighlighter(bool darkMode);

    void applyProfileFilters();

    QList<int> get_now_selected_list();
    void refresh_startstop_button();

    // View slots of ConnectionManager and VpnAuthController, in mainwindow_view.cpp.
    void showRestartReasons();

    void showVpnEndpointStatus(const QString &summary, const QString &detail, bool problem);

    static QString restartReasonText(Engine::RestartReason reason);

    QList<int> get_selected_or_group();

    void saveProfileFocusState();

    void restoreProfileFocusState(RefreshAnchor anchor);

    void selectProfileRows(const QList<int> &rows);

    void focusProfilesTable(bool selectFirst);

    void clearUnavailableProfiles(bool confirm = true, QList<int> profileIDs = {});

    // Returns how many profiles it cleared.
    int clear_test_results(const QList<int> &profileIds);

    // Geometry, size and splitter into the settings, unsaved.
    void saveWindowState();

    void request_unattended_exit(ExitReason reason);

    // Runs after the user confirmed; title heads the warning when Engine::GroupService refuses.
    void delete_group(int gid, const QString &title);

    void dialog_message_impl(MwMessage cmd, const QStringList &args);

    void handle_deeplink_impl(const QString &url);

    void handle_addsub(const QString &url, const QString &name);

    void handle_import_route(const QString &url);

    // throne://remoteRoute?data=<...> : add one or more remote routing profiles. The data is
    // (base64 of) a JSON array of {url, auto_update[, name]} objects.
    void handle_add_remote_routes(const QString &url);

    void openRouteProfileEditor(int routeId);

    // Routes user-supplied text: throne:// links go to the deeplink handler, the
    // rest to the subscription/profile importer.
    void import_or_handle_deeplink(const QString &text);

    // A pasted url asks whether it is a subscription or a proxy link; everything else is imported as is.
    void import_text(const QString &text);

    void refresh_proxy_list_column_size();

    void refresh_sort_indicator();

    // Keeps the current group sorted by `action` from now on.
    void sortCurrentGroup(GroupSortAction action);

    void stopSortingCurrentGroup();

    void refresh_proxy_list_impl(const QList<int> &ids = {}, bool mayNeedReset = false);

    void refresh_proxy_list_impl_refresh_data(const QList<int> &ids = {}, bool mayNeedReset = false);

    // Replaces each server domain with the first address the core's dns-direct returns.
    void resolve_profile_domains(const QList<std::shared_ptr<Configs::Profile>> &profiles, const std::function<void()> &onFinished);

    void parseQrImage(const QPixmap *image);

    // Imports local files picked from the file dialog or dropped on the window.
    // What each file is gets decided from its bytes, never from its name: config
    // files arrive as .json, .conf, .txt or with no extension at all.
    void importFromFiles(const QStringList &paths);

    void trayClickEvent();

    void keyPressEvent(QKeyEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    void changeEvent(QEvent *event) override;

    void showEvent(QShowEvent *event) override;

    void hideEvent(QHideEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    bool event(QEvent *event) override;

    // Tell the connection lister whether its tab is actually on screen (stats tab
    // selected, window neither minimized nor hidden to tray) so it can drop to a
    // relaxed poll cadence when nobody is looking. Recomputed on tab/visibility
    // changes.
    void syncConnectionViewState();

    void dragEnterEvent(QDragEnterEvent *event);

    void dropEvent(QDropEvent *event) override;

    void applyLogBrowserFont();

    // Re-derives the top bar's sizing from the current font and translation, and
    // raises the window's minimum to whatever the layout actually needs. Called
    // at startup and on every font change.
    void applyTopBarMetrics();

    // The window minimum the .ui was designed with; applyTopBarMetrics() only ever
    // grows past this, so a smaller font returns to the designed floor.
    QSize designMinimumSize;

    // Fold the header below its labelled width instead of refusing to shrink (#5).
    struct NarrowLabel {
        class QLabel *label;
        QString full;
        QString narrow;
    };
    QList<NarrowLabel> commandToggleLabels;
    QList<QWidget *> statusDetailCells;
    QList<class QPushButton *> selectionActionButtons;
    class QPushButton *selectionActionsMenuButton = nullptr;
    class QFrame *commandBarFrame = nullptr;
    class QLabel *statsStripHint = nullptr;
    class QLineEdit *serverSearchField = nullptr;
    bool narrowLayout = false;
    int fullLayoutWidth = 0;
    int narrowHiddenMetrics = 0;
    int narrowMinimumWidth = 0;
    QWidget *fullModeContent = nullptr;
    QWidget *simpleModeContent = nullptr;
    QAction *simpleModeAction = nullptr;
    bool simpleModeActive = false;
    bool simpleModeTransition = false;
    bool simpleRestorePending = false;
    void rememberSimpleGeometry();
    void restoreSimpleGeometry();
    void setupSimpleMode();
    void setSimpleMode(bool simple, bool save = true);
    void refreshSimpleProfiles();
    void refreshSimpleSubscription();
    void refreshSimpleStatus();
    void refreshSimpleTraffic(int down, int up);
    int simpleProfileId = -1;
    qint64 simpleConnectedSince = 0;
    class SimpleServerSheet *simpleServerSheet = nullptr;
    class SimpleModeSheet *simpleModeSheet = nullptr;
    bool simplePingRunning = false;
    class ThronedCaptionButton *simpleModeToggle = nullptr;
    class SimpleModeNotice *simpleModeNotice = nullptr;
    class QFrame *simpleAnnounce = nullptr;
    void applyRoutingChange();
    void openCurrentRouteEditor();
    class QStackedWidget *simplePages = nullptr;
    class SimpleRoutesPage *simpleRoutesPage = nullptr;
    QHash<QString, bool> simpleDetectedApps;
    bool simpleAppScanStarted = false;
    void setupSimpleRoutes();
    void openSimpleRoutes();
    void reloadSimpleRoutes();
    void scanSimpleApps();
    QString simpleRoutesSummary() const;
    bool simpleRuleSetBusy = false;
    QStringList simpleRuleSetNext;
    std::function<void(bool)> simpleRuleSetNextThen;
    void downloadSimpleRuleSets(const QStringList &names, const std::function<void(bool)> &then);
    void refreshStaleRuleSets();
    void loadSimpleIcons(QHash<QString, QString> paths);
    void applySimpleRoutes(int profileId, const QList<Configs::AppRoutes::Route> &routes, int rest);
    int simpleRoutesProfileId = -1;
    QHash<int, std::pair<QList<Configs::AppRoutes::Route>, int>> simpleRouteDrafts;
    void refreshSimpleModeToggle();
    void selectSimpleProfile(int profileId);
    void openSimpleServerSheet();
    void refreshSimpleServerSheet(int groupId);
    void openSimpleModeSheet();
    void chooseSimpleMode(int mode, bool elevationConfirmed);
    void updateSimpleDensity();
    void applyWindowMinimum();
    void updateProfileMinimumHeight();
    void setNarrowLayout(bool narrow);
    void updateNarrowLayout();
    // How many metric columns, counted from the right, the table width cannot hold.
    [[nodiscard]] int metricColumnsOverflowing() const;

    // Debounced refresh_proxy_list trigger for font/theme/resize events.
    QTimer *m_proxyListRefreshDebounce = nullptr;
    void scheduleProxyListRefresh();

    bool m_adjustingColumns = false;

    void HotkeyEvent(const QString &id);

    void toggle_connection();

    void toggle_tun();

    void RegisterHiddenMenuShortcuts(bool unregister = false);
    // Register a QShortcut for every action in `menu` (recursing into submenus),
    // appending them to hiddenMenuShortcuts. Needed because the menubar is hidden,
    // so actions reachable only through popup menus get no shortcut on their own.
    // `claimed` holds the key sequences already handled (either by Qt automatically
    // or by an earlier call); shortcuts already in it are skipped to avoid the
    // ambiguous-shortcut conflict that breaks actions shared with other menus.
    void registerMenuShortcuts(QMenu *menu, QSet<QKeySequence> &claimed);
    // Collect the shortcut key sequences of every action in `menu` (recursing into
    // submenus) into `out`, without registering anything.
    void collectMenuShortcuts(QMenu *menu, QSet<QKeySequence> &out);

    void setActionsData();

    QList<QAction *> getActionsForShortcut();

    void loadShortcuts();

    QPointer<CommandPalette> commandPalette;
    QAction *commandPaletteAction = nullptr;
    QHash<QString, QAction *> settingsDialogOpeners;

    void setupCommandPalette();

    void createCommandPalette();

    void fillPaletteSettings();

    void showCommandPalette();

    void revealSetting(const SettingsIndex::Item &item);

    void applyUiMode();

    void setSimpleInterface(bool simple);

    // Takes or frees the one-settings-dialog slot (dialog_is_using) and lets an open command palette catch up.
    void setDialogInUse(bool inUse);

    void url_test_current();
    // Jumps the table to the running profile and flashes it: a status bar that
    // names a server people then cannot find is the complaint this answers.
    void revealRunningProfile();
    void flashProfileRow(int proxyRow);
    // Favourites are a view over every group, not a group of their own: the tab
    // order stays a pure list of real groups.
    void setFavoritesView(bool on);
    // The current group remains the backing page while favourites are open, but
    // its tab must not look active beside the checked favourites button.
    void syncGroupTabSelection();
    void toggleFavorite(const QList<int> &ids);
    void refreshFavoritesButtonIcon();
    // An empty table has to say which of the three reasons it is empty for.
    void refreshProfilesEmptyState();

    // Shared by the group-strip plus and the empty-group call to action.
    void showQuickAddOverlay();
    void setFavoritesButtonVisible(bool on);
    // Shared by the tab strip and the table header menus, so hiding the button
    // never leaves the user without a way to bring it back.
    void addFavoritesButtonAction(class QMenu &menu);
    // The comfortable set is configurable; the compact one carries the filter row
    // and its own UDP toggle, so it stays fixed.
    void addProfileColumnsMenu(class QMenu &menu);
    void applyProfileColumnVisibility();
    // True while a search should reach past the group the tab is showing.
    [[nodiscard]] bool searchesEveryGroup() const;

    void show_startstop_menu();

    void confirm_disable_kill_switch();

    QPointer<QMessageBox> m_killSwitchDialog;

    QPointer<DialogVpnAuth> m_vpnAuthDialog;
    QPointer<DialogScanner> m_scannerDialog;
    QPointer<DialogIpLists> m_ipListsDialog;
    QHash<int, QString> m_scannerNames;
    bool m_scannerPanelShown = false;

    // `silent` suppresses the "no update", error and unsupported-platform boxes and
    // reports a found release through the tray instead of a modal, so the periodic
    // background check never interrupts what the user is doing.
    void CheckUpdate(bool silent = false);
    void startUpdateDownload(const QString &url, const QString &assetName);
    void retryPendingUpdateCheck();
    // Set when a silent check found a release; run when the tray notification is clicked.
    std::function<void()> pendingUpdatePrompt;

    void OpenDashboard();

    void setupConnectionList();

    void refreshStatsPanelLabels();

    void refreshStatsPanelTools();

    void updateStatsPanelChevron(qreal openProgress);

    void openLogSettings();

    void setupConnectionSortMenu();

    void onConnectionContextMenu(const QPoint &pos);

    QString routeRuleAppendBlocker() const;

    enum class RuleToggle { Failed,
                            Added,
                            Moved,
                            Removed };

    // Adds rawRule to the target's rules in the current profile, or takes it out when it is already there.
    RuleToggle toggleRuleInCurrentRoute(const QString &rawRule, const Configs::SimpleTarget &target);

    void setupConnectionFilter();

    void restoreConnectionSort();

    void applyConnectionSort(Stats::ConnectionSort sort);

    void applyConnectionFilters();

    void syncConnectionSourceColumn();

    void syncConnectionExpansion();

    void setConnectionGroupsExpanded(bool expanded);

    bool connectionGroupsExpanded() const;

    void syncConnectionExpandButton();

    void closeConnections(const QStringList &ids);

    QStringList listedConnectionIds() const;

    void refreshConnectionIcons();

    friend void UiPreview::VerifyTestProgress(MainWindow *window, const QString &prefix);

    // EnginePolicy, in mainwindow_policy.cpp; reached through EngineHost::policy().
    void geoAssetMissingCategory(const QString &contextName, const QString &needed, const QString &whichFile) override;

    bool confirmGeoAssetDownload(const QString &contextName) override;

    void geoAssetDownloadFinished(const QString &error) override;

    void killSwitchProblem() override;

    void killSwitchProblemResolved() override;

    void extraCoreNotPermitted() override;

    void startFailed(const StartRequest &request, StartOutcome outcome, const QString &title, const QString &error) override;

    void strictRouteUnavailable(const QString &error) override;

    bool offerCoreReset(const QString &error) override;

    bool offerLeftoverTunAdapterRetry(const QString &error) override;

    void stopFailed(const QString &error) override;

    Engine::SlowOperation slowOperation(Engine::SlowOperationKind kind, int delayMs) override;

    bool requestCorePrivileges() override;
    // Set around a setTun the simple mode sheet already confirmed.
    bool elevationPreconfirmed = false;

    void systemProxyNeedsMixedInbound() override;

    bool vpnPromptOpen() const override;

    void vpnChallenge(const VpnAuthChallenge &challenge) override;

    void vpnCredentialsRejected(int profileId, const VpnAuthChallenge &challenge,
                                const std::function<void(const QString &username, const QString &password)> &retryWith) override;

    void vpnChallengesCleared() override;

    void settingsNeedTunRestart() override;

    bool confirmRestartProgram() override;

    void testSessionBusy(const QString &text) override;

    void exitProgram(ExitReason reason, bool unattended) override;

    // TestObserver, in mainwindow_view.cpp; synchronous, so a panel seed never lands after the next session's clear.
    void testPanelSeeded(Engine::TestKind kind, int totalProfiles) override;

    void testProgressed() override;

    void testPanelsCleared() override;

    void testViewChanged(bool force) override;

    void speedTestProgress(const QString &profileName, const libcore::SpeedTestResult &result) override;

    void testedProfilesChanged(const QList<int> &profileIds) override;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

inline MainWindow *GetMainWindow() {
    return qobject_cast<MainWindow *>(mainwindow);
}

MainWindow *UI_InitMainWindow();

// The profile the core should start once it is up, following the start_pick setting; NoProfileId for none.
int UI_BootStartProfileId();

#ifdef Q_OS_LINUX
class OrgFreedesktopPortalRequestInterface : public QDBusAbstractInterface {
    Q_OBJECT
public:
    OrgFreedesktopPortalRequestInterface(const QString &service,
                                         const QString &path,
                                         const QDBusConnection &connection,
                                         QObject *parent = nullptr);

    ~OrgFreedesktopPortalRequestInterface();

public Q_SLOTS:
    inline QDBusPendingReply<> Close() {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("Close"), argumentList);
    }

Q_SIGNALS:
    void Response(uint response, QVariantMap results);
};

namespace org {
namespace freedesktop {
namespace portal {
typedef ::OrgFreedesktopPortalRequestInterface Request;
}
} // namespace freedesktop
} // namespace org
#endif
