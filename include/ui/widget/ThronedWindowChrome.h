#pragma once

#include <QtCore/QString>

class QWidget;
class ThronedTitleBar;

// Window chrome that leaves the native frame in place.
//
// Qt::FramelessWindowHint removes the frame outright, and with it everything the
// window manager provides for free: the open and close animations, Aero Snap, the
// Windows 11 Snap Layout flyout, the resize borders and the drop shadow. What is
// left has to be re-implemented by hand, badly. Instead the frame is kept and only
// the caption's drawing is replaced, which is how Chrome, VS Code and Discord do it.
namespace ThronedChrome {
// Builds the title bar, binds the window to the platform agent and returns the
// bar for the caller to place. This is the whole ritual: no window flags to set
// and no resizer to remember.
ThronedTitleBar *install(QWidget *window, const QString &context = {});
} // namespace ThronedChrome
