#include "include/ui/setting/Icon.hpp"

#include "include/global/Configs.hpp"

#include <QHash>
#include <QPixmap>

#include <optional>

namespace {
QHash<Icon::TrayIconStatus, QIcon> g_trayIcons;
std::optional<bool> g_trayIconsCustom;

QString statusName(Icon::TrayIconStatus status) {
    switch (status) {
        case Icon::TrayIconStatus::None:
            return QStringLiteral("Off");
        case Icon::TrayIconStatus::Connecting:
            return QStringLiteral("Connecting");
        case Icon::TrayIconStatus::Running:
            return QStringLiteral("Throned");
        case Icon::TrayIconStatus::SystemProxy:
            return QStringLiteral("Proxy");
        case Icon::TrayIconStatus::Vpn:
            return QStringLiteral("Tun");
    }
    MW_show_log("Icon::GetTrayIcon: Unknown status");
    return QStringLiteral("Off");
}

QIcon loadNamedIcon(const QString &name, bool useCustom, const QString &customFallback = {}) {
    if (useCustom) {
        const QString legacy = name == QStringLiteral("Throned") ? QStringLiteral("Throne") : QString();
        for (const auto &candidate: {name, customFallback, legacy}) {
            if (candidate.isEmpty()) continue;
            // QIcon(path).isNull() is not a decode check: a PNG with a valid signature and a corrupt body passes it.
            if (const auto custom = QPixmap(QStringLiteral("icons/") + candidate + QStringLiteral(".png")); !custom.isNull()) return QIcon(custom);
        }
    }
    return QIcon(QStringLiteral(":/Throned/") + name + QStringLiteral(".png"));
}
} // namespace

void Icon::InvalidateTrayIconCache() {
    g_trayIcons.clear();
}

QIcon Icon::GetTrayIcon(TrayIconStatus status) {
    const bool useCustom = Configs::dataManager->settingsRepo->use_custom_icons;
    if (g_trayIconsCustom != useCustom) {
        g_trayIcons.clear();
        g_trayIconsCustom = useCustom;
    }
    if (const auto it = g_trayIcons.constFind(status); it != g_trayIcons.cend()) return it.value();

    // A custom set without Connecting.png keeps its own Off icon rather than mixing in the bundled one.
    const QString customFallback = status == TrayIconStatus::Connecting ? statusName(TrayIconStatus::None) : QString();
    const QIcon icon = loadNamedIcon(statusName(status), useCustom, customFallback);
    g_trayIcons.insert(status, icon);
    return icon;
}

QIcon Icon::GetTaskbarIcon(TrayIconStatus status) {
    const auto &settings = Configs::dataManager->settingsRepo;
    // The bundled icon only: a custom PNG has no room for the padding macOS adds in the dock.
    if (settings->use_custom_icons && !settings->follow_status_in_taskbar) {
        return QIcon(QStringLiteral(":/Throned/Throned.png"));
    }
    return GetTrayIcon(status);
}
