#include "include/ui/setting/Icon.hpp"

#include "include/global/Configs.hpp"

#include <QPainter>


QPixmap Icon::GetTrayIcon(TrayIconStatus status) {
    QPixmap pixmap;
    QPixmap pixmap_read;

    if (status == NONE)
    {
        if (Configs::dataManager->settingsRepo->use_custom_icons) {
            pixmap_read = QPixmap(QString("icons/") + "Off" + ".png");
        }
        if (pixmap_read.isNull()) {
            pixmap_read = QPixmap(QString(":/Throned/") + "Off" + ".png");
        }
        if (!pixmap_read.isNull()) pixmap = pixmap_read;
    } else if (status == RUNNING)
    {
        if (Configs::dataManager->settingsRepo->use_custom_icons) {
            pixmap_read = QPixmap(QString("icons/") + "Throned" + ".png");
            if (pixmap_read.isNull()) {
                pixmap_read = QPixmap(QString("icons/") + "Throne" + ".png");
            }
        }
        if (pixmap_read.isNull()) {
            pixmap_read = QPixmap(QString(":/Throned/") + "Throned" + ".png");
        }
        if (!pixmap_read.isNull()) pixmap = pixmap_read;
    } else if (status == SYSTEM_PROXY_DNS)
    {
        if (Configs::dataManager->settingsRepo->use_custom_icons) {
            pixmap_read = QPixmap(QString("icons/") + "Proxy-Dns" + ".png");
        }
        if (pixmap_read.isNull()) {
            pixmap_read = QPixmap(QString(":/Throned/") + "Proxy-Dns" + ".png");
        }
        if (!pixmap_read.isNull()) pixmap = pixmap_read;
    } else if (status == SYSTEM_PROXY)
    {
        if (Configs::dataManager->settingsRepo->use_custom_icons) {
            pixmap_read = QPixmap(QString("icons/") + "Proxy" + ".png");
        }
        if (pixmap_read.isNull()) {
            pixmap_read = QPixmap(QString(":/Throned/") + "Proxy" + ".png");
        }
        if (!pixmap_read.isNull()) pixmap = pixmap_read;
    } else if (status == DNS)
    {
        if (Configs::dataManager->settingsRepo->use_custom_icons) {
            pixmap_read = QPixmap(QString("icons/") + "Dns" + ".png");
        }
        if (pixmap_read.isNull()) {
            pixmap_read = QPixmap(QString(":/Throned/") + "Dns" + ".png");
        }
        if (!pixmap_read.isNull()) pixmap = pixmap_read;
    } else if (status == VPN)
    {
        if (Configs::dataManager->settingsRepo->use_custom_icons) {
            pixmap_read = QPixmap(QString("icons/") + "Tun" + ".png");
        }
        if (pixmap_read.isNull()) {
            pixmap_read = QPixmap(QString(":/Throned/") + "Tun" + ".png");
        }
        if (!pixmap_read.isNull()) pixmap = pixmap_read;
    } else
    {
        MW_show_log("Icon::GetTrayIcon: Unknown status");
        if (Configs::dataManager->settingsRepo->use_custom_icons) {
            pixmap_read = QPixmap(QString("icons/") + "Off" + ".png");
        }
        if (pixmap_read.isNull()) {
            pixmap_read = QPixmap(QString(":/Throned/") + "Off" + ".png");
        }
        if (!pixmap_read.isNull()) pixmap = pixmap_read;
    }

    return pixmap;
}

QPixmap Icon::GetTaskbarIcon(TrayIconStatus status) {
    const auto &settings = Configs::dataManager->settingsRepo;
    // The bundled icon only: a custom PNG has no room for the padding macOS adds in the dock.
    if (settings->use_custom_icons && !settings->follow_status_in_taskbar) {
        return QPixmap(QStringLiteral(":/Throned/Throned.png"));
    }
    return GetTrayIcon(status);
}
