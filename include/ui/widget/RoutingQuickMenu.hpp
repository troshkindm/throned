#pragma once

#include <QFrame>
#include <QPoint>
#include <QString>

#include <functional>

class QCheckBox;
class QLabel;
class QPushButton;

// The popover behind the routing segment of the status bar: it shows the active
// routing profile, switches the traffic that matches no rule between direct and
// proxy, and turns the profile's own rules on or off.
//
// It is a transient tool window rather than a QMenu because it hosts real
// controls (a segmented switch and a checkbox with help text), and because a
// menu on Linux may be drawn by the shell instead of Qt.
class RoutingQuickMenu final : public QFrame {
    Q_OBJECT
public:
    // Every read goes through the config layer; only the actions and the "apply
    // it now" step are injected, so the widget stays out of MainWindow's internals.
    struct Callbacks {
        std::function<void(int outboundID)> setDefaultOutbound;
        std::function<void(bool enabled)> setApplyProfileRules;
        std::function<void()> openProfile; // edit the active profile
        std::function<void()> manageProfiles;
    };

    explicit RoutingQuickMenu(Callbacks cb, QWidget *parent = nullptr);

    // Show anchored above `globalPos`, kept fully on the containing screen.
    void popupAt(const QPoint &globalPos);

    // "Default · Direct" — the summary the status bar shows for the active profile.
    static QString statusSummary();

protected:
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void rebuild();

    Callbacks m_cb;
    bool m_armed = false;
    QLabel *m_profileName = nullptr;
    QPushButton *m_direct = nullptr;
    QPushButton *m_proxy = nullptr;
    QCheckBox *m_applyRules = nullptr;
};
