#pragma once

#include <QFrame>
#include <QPoint>

#include <functional>
#include <memory>

class QLabel;
class QPushButton;

namespace Configs { class Group; }

// Everything the subscription server says about a plan, in one surface anchored to
// the group tab whose meter the user just clicked: allowance, expiry, the refresh
// cycle, the provider's announcement and its links.
//
// A transient tool window rather than a QMenu, for the same reasons RoutingQuickMenu
// is one: it hosts real controls, and a menu on Linux may be drawn by the shell.
class SubscriptionPopover final : public QFrame {
    Q_OBJECT
public:
    explicit SubscriptionPopover(std::function<void(int gid)> updateNow, QWidget *parent = nullptr);

    // Show below `globalPos`, kept fully on the containing screen.
    void popupFor(const std::shared_ptr<Configs::Group> &group, const QPoint &globalPos);

protected:
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void fill(const std::shared_ptr<Configs::Group> &group);

    std::function<void(int gid)> m_updateNow;
    bool m_armed = false;
    int m_gid = -1;

    QLabel *m_name = nullptr;
    QPushButton *m_support = nullptr;
    QPushButton *m_website = nullptr;
    QFrame *m_announceBox = nullptr;
    QLabel *m_announce = nullptr;
    QFrame *m_meterSpent = nullptr;
    QFrame *m_meterRest = nullptr;
    QLabel *m_traffic = nullptr;
    QLabel *m_expiry = nullptr;
    QLabel *m_expiryDate = nullptr;
    QLabel *m_updated = nullptr;
    QPushButton *m_refresh = nullptr;
};
