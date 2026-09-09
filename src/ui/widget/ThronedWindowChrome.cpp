#include "include/ui/widget/ThronedWindowChrome.h"

#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/setting/ThemeManager.hpp"

#include <QCoreApplication>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QWidget>

#include <QWKWidgets/widgetwindowagent.h>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <windows.h>
// Keep the Win32 printer macro out of the rest of this unity batch.
#undef SetPort
#endif

namespace {
// Every window that got the treatment, so a theme change can reach all of them.
QList<QPointer<QWK::WidgetWindowAgent>> &agents() {
    static QList<QPointer<QWK::WidgetWindowAgent>> list;
    return list;
}

// Themes load before window agents exist, so remember the material for newly opened windows.
QString &wantedBackdrop() {
    static QString value;
    return value;
}

// Wallpaper-dependent materials are only allowed in the explicitly interactive preview.
bool previewRun() {
    static const bool preview = [] {
        const auto arguments = QCoreApplication::arguments();
        if (arguments.contains(QStringLiteral("-ui-preview")) &&
            arguments.contains(QStringLiteral("-ui-preview-backdrop"))) return false;
        // Whole flags only: an -appdata path that happens to end in "-preview" is not a preview run.
        static const QStringList otherPreviews{QStringLiteral("--route-editor-preview"),
                                               QStringLiteral("--update-prompt-preview")};
        for (const QString &argument: arguments)
            if (argument.startsWith(QStringLiteral("-ui-preview")) || otherPreviews.contains(argument)) return true;
        return false;
    }();
    return preview;
}

void purgeClosedWindows() {
    agents().removeIf([](const QPointer<QWK::WidgetWindowAgent> &agent) { return agent.isNull(); });
}

const QStringList &knownBackdrops() {
    static const QStringList names{QStringLiteral("mica"), QStringLiteral("mica-alt"),
                                   QStringLiteral("acrylic-material"), QStringLiteral("dwm-blur")};
    return names;
}

#ifdef Q_OS_WIN
class MicaActivationFilter final : public QAbstractNativeEventFilter {
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
        if (eventType != "windows_generic_MSG") return false;
        const auto *msg = static_cast<MSG *>(message);
        if (msg->message != WM_NCACTIVATE || msg->wParam || IsIconic(msg->hwnd)) return false;
        for (const auto &agent: agents()) {
            if (!agent) continue;
            const auto *window = qobject_cast<QWidget *>(agent->parent());
            if (window == nullptr || !window->property("active-mica").toBool() ||
                reinterpret_cast<HWND>(window->effectiveWinId()) != msg->hwnd) continue;
            // Only retain the painted activation state; TRUE still lets Windows transfer input focus.
            DefWindowProcW(msg->hwnd, WM_NCACTIVATE, TRUE, -1);
            *result = TRUE;
            return true;
        }
        return false;
    }
};

void installMicaActivationFilter() {
    static MicaActivationFilter filter;
    QCoreApplication::instance()->installNativeEventFilter(&filter);
}
#endif

void applyTo(QWK::WidgetWindowAgent *agent) {
    if (agent == nullptr) return;
    auto *window = qobject_cast<QWidget *>(agent->parent());
    const QString wanted = wantedBackdrop();
    // DWM shares state between materials, so disable the previous effects before applying the new one.
    if (!wanted.isEmpty() || (window != nullptr && window->property("custom-style").toBool()))
        for (const QString &name: knownBackdrops())
            if (name != wanted) agent->setWindowAttribute(name, false);
    agent->setWindowAttribute(QStringLiteral("dark-mode"), themeManager()->Colors().dark);
    const bool applied = !wanted.isEmpty() && agent->setWindowAttribute(wanted, true);
    if (window == nullptr) return;

    window->setProperty("custom-style", applied);
#ifdef Q_OS_WIN
    const bool wasActiveMica = window->property("active-mica").toBool();
    const bool activeMica = applied && (wanted == QStringLiteral("mica") || wanted == QStringLiteral("mica-alt"));
    window->setProperty("active-mica", activeMica);
    if (activeMica || wasActiveMica) {
        const auto hwnd = reinterpret_cast<HWND>(window->effectiveWinId());
        if (hwnd != nullptr && !IsIconic(hwnd))
            DefWindowProcW(hwnd, WM_NCACTIVATE, activeMica || GetForegroundWindow() == hwnd, -1);
    }
#endif
    // Descendant selectors also depend on this property and must lose their cached opaque brushes.
    const auto widgets = window->findChildren<QWidget *>();
    window->setStyleSheet(window->styleSheet());
    for (QWidget *widget: widgets) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
    window->update();
}
} // namespace

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
    purgeClosedWindows();
    agents().append(agent);
#ifdef Q_OS_WIN
    installMicaActivationFilter();
#endif
    applyTo(agent);
    return titleBar;
}

void setBackdrop(const QString &attribute) {
    wantedBackdrop() = previewRun() ? QString() : attribute;
    purgeClosedWindows();
    for (const auto &agent: agents()) applyTo(agent);
}
} // namespace ThronedChrome
