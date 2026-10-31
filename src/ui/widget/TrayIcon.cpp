#include "include/ui/widget/TrayIcon.hpp"

#ifndef Q_OS_MAC
TrayIcon::TrayIcon(QObject *parent) : QObject(parent), m_tray(new QSystemTrayIcon(this)) {
    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayIcon::activated);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayIcon::messageClicked);
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::setIcon(const QIcon &icon) { m_tray->setIcon(icon); }
// Qt's SNI backend emits NewToolTip on every call, waking the tray host even for unchanged text (#2043).
void TrayIcon::setToolTip(const QString &text) {
    if (m_tray->toolTip() != text) m_tray->setToolTip(text);
}
void TrayIcon::setContextMenu(QMenu *menu) { m_tray->setContextMenu(menu); }
void TrayIcon::setVisible(bool visible) { m_tray->setVisible(visible); }
void TrayIcon::showMessage(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon,
                           int millisecondsTimeoutHint) {
    m_tray->showMessage(title, message, icon, millisecondsTimeoutHint);
}
bool TrayIcon::isVisible() const { return m_tray->isVisible(); }
#endif
