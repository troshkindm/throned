#pragma once

#include <QDialog>
#include <QMap>
#include <QList>
#ifndef Q_MOC_RUN
#include <core/server/gen/libcore.pb.h>
#endif

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QComboBox;
class QTabWidget;
class QTreeWidget;
class QTimer;
class QProgressBar;
class DiagnosticStep;

// Transport stays outside the window: previews and tests use the same widgets
// without starting a core or reading the user's database.
class DiagnosticsWindow final : public QDialog {
    Q_OBJECT
public:
    explicit DiagnosticsWindow(QWidget *parent = nullptr);
    void showApplication(const QString &processKey = {});
    void applyRoutePreview(const libcore::PreviewRouteResponse &result, const QString &error = {});
    void applySiteResult(const libcore::DiagnoseSiteResponse &result, const QString &error = {});
    void applyConnections(const libcore::QueryConnectionsResp &result, const QString &error = {});

signals:
    void routeRequested(const libcore::PreviewRouteRequest &request);
    void siteRequested(const libcore::DiagnoseSiteRequest &request);
    void connectionsRequested();
    void reachabilityRequested();
    void routingRulesRequested();

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
        QString outbound;
        QString network;
        QString matchedRule;
        int count = 0;
        int closed = 0;
        qint64 upload = 0;
        qint64 download = 0;
        int suspicion = 0;
    };

    void refreshTheme();
    void requestConnections();
    void startSite();
    void toggleObservation();
    void rebuildConnections();
    void syncConnectionRows();
    [[nodiscard]] const ConnectionGroup *currentGroup() const;
    void showConnection();
    void setStep(int index, const QString &title, const QString &detail, const QString &tone, qint64 ms = -1);
    void rescaleSteps();
    void endSite();
    void setVerdict(const QString &tone, const QString &title, const QString &detail, const QString &action);
    void copyReport();
    QString connectionReport() const;

    QTabWidget *tabs;
    QLineEdit *address;
    QPushButton *check;
    QPushButton *resetRoute;
    QLabel *siteContext;
    QLabel *verdictIcon;
    QLabel *verdictTitle;
    QLabel *verdictDetail;
    QProgressBar *progress;
    QLabel *pathCaption;
    QList<DiagnosticStep *> steps;
    QString verdictTone;
    QString siteReport;
    QString pinnedProcess;
    QString pinnedOutbound;
    QString routedOutbound;
    QString routedRule;
    QString routedAction;
    QString requestURL;
    QPushButton *verdictAction;
    QString verdictActionKind;
    bool siteBusy = false;

    QComboBox *apps;
    QPushButton *observe;
    QLabel *captureStatus;
    QLabel *summary;
    QTreeWidget *connections;
    QLabel *connectionTitle;
    QLabel *connectionDetail;
    QLabel *ruleDetail;
    QToolButton *explainRule;
    QPushButton *diagnoseAddress;
    QPushButton *report;
    QTimer *poll;
    QMap<QString, libcore::ConnectionMetaData> captured;
    QMap<QString, libcore::ConnectionMetaData> latest;
    QList<ConnectionGroup> groups;
    QString desiredProcess;
    bool recording = false;
    bool pollBusy = false;
    bool captureFinished = false;
    qint64 captureStarted = 0;
    qint64 captureStopped = 0;
    static constexpr int MaxConnections = 1000;
};
