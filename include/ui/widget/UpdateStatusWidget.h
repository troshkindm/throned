#pragma once

#include <QFrame>
#include <QMap>

class QLabel;
class QProgressBar;
class QPushButton;

// Shared footer slot: update progress takes precedence over queued tips and warnings.
class UpdateStatusWidget final : public QFrame {
    Q_OBJECT

public:
    enum class State {
        Hidden,
        Downloading,
        Preparing,
        Ready,
        Error,
        Notice,
    };
    Q_ENUM(State)

    explicit UpdateStatusWidget(QWidget *parent = nullptr);

    [[nodiscard]] State state() const { return state_; }

    void showDownloading(const QString &assetName, qint64 received, qint64 total);
    void showPreparing(const QString &assetName);
    void showReady(const QString &assetName);
    void showError(const QString &message);
    void dismiss();

    enum class Severity {
        Tip,
        Warning,
        Error,
    };
    struct Notice {
        QString id;
        QString title;
        QString detail;
        QString action;
        QString dismissText;
        Severity severity = Severity::Tip;
        int priority = 0;
    };
    void postNotice(const Notice &notice);
    void removeNotice(const QString &id);
    [[nodiscard]] QString activeNoticeId() const { return activeNoticeId_; }

signals:
    void restartRequested();
    void retryRequested();
    void noticeActionRequested(const QString &id);
    void noticeDismissed(const QString &id);

private:
    void setState(State state);
    void refreshIcon();
    static QString displayName(const QString &assetName);
    void showNextNotice();

    State state_ = State::Hidden;
    QMap<QString, Notice> notices_;
    QString activeNoticeId_;
    QLabel *icon_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *detail_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPushButton *primary_ = nullptr;
    QPushButton *secondary_ = nullptr;
};
