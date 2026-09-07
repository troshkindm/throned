#include "include/ui/widget/WindowNotices.h"

#include <QCoreApplication>
#include <QTimer>

#include "include/database/SettingsRepo.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/UpdateStatusWidget.h"

namespace {
const QString MicaTipId = QStringLiteral("windows11-mica-v1");
const QString MicaTheme = QStringLiteral("Mica (Windows 11)");

void rememberNotice(Configs::SettingsRepo &settings, const QString &id) {
    if (settings.dismissed_notices.contains(id)) return;
    settings.dismissed_notices.append(id);
    settings.Save();
}
} // namespace

void InstallWindowNotices(UpdateStatusWidget *status, Configs::SettingsRepo &settings) {
    const auto arguments = QCoreApplication::arguments();
    if (arguments.contains(QStringLiteral("-ui-preview")) &&
        !arguments.contains(QStringLiteral("-ui-preview-notices"))) return;

    const auto refresh = [status, &settings] {
        if (const auto *skin = themeManager()->Skin(); skin != nullptr && skin->name == MicaTheme) rememberNotice(settings, MicaTipId);
        // Platform eligibility comes from the skin catalogue, including Windows build requirements.
        if (themeManager()->Skin(MicaTheme) == nullptr || settings.dismissed_notices.contains(MicaTipId)) {
            status->removeNotice(MicaTipId);
            return;
        }
        status->postNotice({MicaTipId,
                            QCoreApplication::translate("WindowNotices", "Try Mica"),
                            QCoreApplication::translate("WindowNotices", "A soft Windows 11 backdrop for Throned."),
                            QCoreApplication::translate("WindowNotices", "Enable"),
                            QCoreApplication::translate("WindowNotices", "Don't show again")});
    };
    QObject::connect(status, &UpdateStatusWidget::noticeDismissed, status, [&settings](const QString &id) {
        if (id == MicaTipId) rememberNotice(settings, id);
    });
    QObject::connect(status, &UpdateStatusWidget::noticeActionRequested, status, [status, &settings](const QString &id) {
        if (id != MicaTipId || themeManager()->Skin(MicaTheme) == nullptr) return;
        settings.theme = MicaTheme;
        rememberNotice(settings, id);
        status->removeNotice(id);
        themeManager()->ApplyTheme(MicaTheme);
    });
    QObject::connect(themeManager(), &ThemeManager::themeChanged, status, refresh);
    QTimer::singleShot(0, status, refresh);
}
