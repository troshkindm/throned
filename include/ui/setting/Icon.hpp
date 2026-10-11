#pragma once

#include <QIcon>

namespace Icon {

enum class TrayIconStatus {
    None,
    Connecting,
    Running,
    SystemProxy,
    Vpn,
};

QIcon GetTrayIcon(TrayIconStatus status);

QIcon GetTaskbarIcon(TrayIconStatus status);

// Forces the next GetTrayIcon to re-read the icon files; a use_custom_icons flip is detected without it.
void InvalidateTrayIconCache();
} // namespace Icon
