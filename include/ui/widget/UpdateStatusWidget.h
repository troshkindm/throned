#pragma once

#include <QFrame>

class QLabel;
class QProgressBar;
class QPushButton;

// Compact, non-modal update feedback that lives immediately above the main
// status bar. The updater owns no palette of its own: the main window's
// semantic stylesheet and ThemeManager-tinted icon are the whole appearance.
class UpdateStatusWidget final : public QFrame {
    Q_OBJECT

public:
    enum class State {
        Hidden,
        Downloading,
        Preparing,
        Ready,
        Error,
    };
    Q_ENUM(State)

    explicit UpdateStatusWidget(QWidget *parent = nullptr);

    [[nodiscard]] State state() const { return state_; }

    void showDownloading(const QString &assetName, qint64 received, qint64 total);
    void showPreparing(const QString &assetName);
    void showReady(const QString &assetName);
    void showError(const QString &message);
    void dismiss();

signals:
    void restartRequested();
    void retryRequested();

private:
    void setState(State state);
    void refreshIcon();
    static QString displayName(const QString &assetName);

    State state_ = State::Hidden;
    QLabel *icon_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *detail_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPushButton *primary_ = nullptr;
    QPushButton *secondary_ = nullptr;
};
