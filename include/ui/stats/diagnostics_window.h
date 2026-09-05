#pragma once

#include <QDialog>
#include <QHash>
#include <QMap>
#include <QList>
#ifndef Q_MOC_RUN
#include <core/server/gen/libcore.pb.h>
#endif

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QComboBox;
class QStackedWidget;
class QTreeWidget;
class QTimer;
class QProgressBar;
class QGridLayout;
class QVBoxLayout;
class QPlainTextEdit;
class DiagnosticStep;
class RailButton;
class HealthRow;
class Sparkline;

// Transport stays outside the window: previews and tests use the same widgets
// without starting a core or reading the user's database.
class DiagnosticsWindow final : public QDialog {
    Q_OBJECT
public:
    // Everything the window needs that the application already knows, so it never
    // reaches into the database or the core itself.
    struct LocalState {
        bool coreRunning = false;
        QString coreVersion;
        qint64 coreUptimeMs = -1;
        QString profileName;
        QString profileType;
        int profileLatencyMs = -1;
        bool tun = false;
        bool systemProxy = false;
        QString udpState;   // "ok", "fail", "unknown"
        QString udpDetail;
        QString environmentReport;
    };

    explicit DiagnosticsWindow(QWidget *parent = nullptr);
    void showApplication(const QString &processKey = {});
    void showAddress(const QString &url);
    void applyLocalState(const LocalState &state);
    void applyHealth(const libcore::HealthResponse &result, const QString &error = {});
    void applyRoutePreview(const libcore::PreviewRouteResponse &result, const QString &error = {});
    void applySiteResult(const libcore::DiagnoseSiteResponse &result, const QString &error = {});
    void applyMatrixResult(const QString &outbound, const libcore::DiagnoseSiteResponse &result, const QString &error = {});
    void applyConnections(const libcore::QueryConnectionsResp &result, const QString &error = {});

signals:
    void healthRequested();
    void routeRequested(const libcore::PreviewRouteRequest &request);
    void siteRequested(const libcore::DiagnoseSiteRequest &request);
    void matrixRequested(const QString &url, const QStringList &outbounds);
    void connectionsRequested();
    void closeConnectionsRequested(const QStringList &ids);
    void ruleRequested(const QString &entry, int action);
    // "rules", "dns", "profile", "dpi", "log", "reachability", "interception"
    void navigateRequested(const QString &target);

private:
    // One row per destination an application talks to, not per socket: a chat client
    // opens dozens of connections to the same host and the raw list is unreadable.
    struct ConnectionGroup {
        QString key;
        QString host;
        QString domain;
        QString dest;
        QString process;
        QString processPath;
        QString source;
        QString parentProcess;
        quint32 pid = 0;
        QString outbound;
        QString network;
        QString matchedRule;
        QStringList ids;
        int count = 0;
        int closed = 0;
        qint64 upload = 0;
        qint64 download = 0;
        int suspicion = 0;
    };

    void buildOverviewPage();
    void buildAddressPage();
    void buildApplicationsPage();
    void buildReportPage();

    void refreshTheme();
    void refreshRail();
    void refreshOverview();
    void requestConnections();
    void startSite();
    void endSite();
    void startMatrix();
    void toggleObservation();
    void rebuildConnections();
    void syncConnectionRows();
    [[nodiscard]] const ConnectionGroup *currentGroup() const;
    void resolveAncestry();
    void rebuildRuleMenu(const ConnectionGroup &group);
    void setFacts(const QList<QPair<QString, QString>> &rows, const QStringList &notes);
    void showConnection();
    void setStep(int index, const QString &title, const QString &detail, const QString &tone, qint64 ms = -1);
    void rescaleSteps();
    void setVerdict(const QString &tone, const QString &title, const QString &detail, const QStringList &actions);
    void refreshReport();
    void copyReport();
    void saveReport();
    [[nodiscard]] QString buildReport() const;
    [[nodiscard]] QString connectionReport() const;
    [[nodiscard]] QString healthReport() const;

    QStackedWidget *pages;
    QList<RailButton *> rail;

    // Overview
    QList<HealthRow *> healthRows;
    QLabel *overviewStamp;
    QPushButton *recheck;
    LocalState local;
    libcore::HealthResponse health;
    QString healthError;
    bool healthBusy = false;

    // Address
    QLineEdit *address;
    QLineEdit *compare;
    QPushButton *check;
    QPushButton *compareToggle;
    QPushButton *resetRoute;
    QLabel *siteContext;
    QLabel *pathCaption;
    QLabel *verdictIcon;
    QLabel *verdictTitle;
    QLabel *verdictDetail;
    QList<QPushButton *> verdictActions;
    QProgressBar *progress;
    QList<DiagnosticStep *> steps;
    QLabel *matrixCaption;
    QTreeWidget *matrix;
    QString verdictTone;
    QString siteReport;
    QString pinnedProcess;
    QString pinnedOutbound;
    QString routedOutbound;
    QString routedRule;
    QString routedAction;
    QString requestURL;
    QString comparingURL;
    QString comparisonSummary;
    int matrixPending = 0;
    bool siteBusy = false;

    // Applications
    QComboBox *apps;
    QPushButton *observe;
    QPushButton *onlyProblems;
    QPushButton *dropConnections;
    QToolButton *addRule;
    QLabel *captureStatus;
    QLabel *summary;
    QTreeWidget *connections;
    QLabel *connectionTitle;
    Sparkline *connectionSpark;
    QLabel *connectionDetail;
    QGridLayout *facts;
    QVBoxLayout *findings;
    QLabel *ruleDetail;
    QPushButton *diagnoseAddress;
    QTimer *poll;
    QMap<QString, libcore::ConnectionMetaData> captured;
    QMap<QString, libcore::ConnectionMetaData> latest;
    QList<ConnectionGroup> groups;
    QHash<QString, QList<qint64>> downHistory;
    QHash<QString, qint64> lastDownload;
    QHash<quint32, QPair<quint32, QString>> ancestry;
    qint64 ancestryTakenAt = 0;
    QString desiredProcess;
    bool recording = false;
    bool pollBusy = false;
    bool captureFinished = false;
    bool problemsOnly = false;
    qint64 captureStarted = 0;
    qint64 captureStopped = 0;

    // Report
    QList<QCheckBox *> reportSections;
    QCheckBox *redactToggle;
    QPlainTextEdit *reportPreview;
    QPushButton *reportCopy;
    QPushButton *reportSave;

    // Sentinel for the "no application" entry of the filter; a real path can never be this.
    static inline const QString NoProcessFilter = QStringLiteral("?no-process");
    static constexpr int DetailWidth = 268;
    static constexpr int MaxConnections = 1000;
    static constexpr int HistorySamples = 40;
};
