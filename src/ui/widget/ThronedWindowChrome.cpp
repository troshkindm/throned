#include "include/ui/widget/ThronedWindowChrome.h"

#include "include/ui/widget/ThronedTitleBar.h"

#include <QString>
#include <QWidget>

#include <QWKWidgets/widgetwindowagent.h>

namespace ThronedChrome {
ThronedTitleBar *install(QWidget *window, const QString &context) {
    auto *titleBar = new ThronedTitleBar(context, window);
    if (window == nullptr) return titleBar;

    auto *agent = new QWK::WidgetWindowAgent(window);
    // A refused setup leaves the ordinary native title bar visible above ours,
    // which is ugly but usable -- unlike a frameless window with no way to move it.
    if (!agent->setup(window)) return titleBar;

    agent->setTitleBar(titleBar);
    // Naming the buttons by role is what earns the Snap Layout flyout on Windows 11;
    // without it the window is merely frameless again.
    agent->setSystemButton(QWK::WindowAgentBase::Minimize, titleBar->minimizeButton());
    agent->setSystemButton(QWK::WindowAgentBase::Maximize, titleBar->maximizeButton());
    agent->setSystemButton(QWK::WindowAgentBase::Close, titleBar->closeButton());
    return titleBar;
}
} // namespace ThronedChrome
