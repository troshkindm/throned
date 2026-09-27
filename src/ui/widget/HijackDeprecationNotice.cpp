#include "include/ui/widget/HijackDeprecationNotice.h"

#include "include/database/MarkersRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/ui/widget/UpdateStatusWidget.h"

#include <QCoreApplication>
#include <QTimer>
#include <utility>

namespace {
const QString HijackNoticeId = QStringLiteral("hijack-deprecated");

bool previewWithoutNotices() {
    const auto arguments = QCoreApplication::arguments();
    return arguments.contains(QStringLiteral("-ui-preview")) &&
           !arguments.contains(QStringLiteral("-ui-preview-notices"));
}
} // namespace

HijackDeprecationNotice::HijackDeprecationNotice(UpdateStatusWidget* status, Configs::SettingsRepo& settings,
                                                 Configs::MarkersRepo& markers, std::function<void()> openSettings)
    : QObject(status), status_(status), settings_(settings), markers_(markers) {
    connect(status, &UpdateStatusWidget::noticeActionRequested, this,
            [openSettings = std::move(openSettings)](const QString& id) {
                if (id == HijackNoticeId) openSettings();
            });
    connect(status, &UpdateStatusWidget::noticeDismissed, this, [this](const QString& id) {
        if (id == HijackNoticeId) markers_.Mark(Configs::Markers::HijackDeprecated);
    });
    QTimer::singleShot(0, this, &HijackDeprecationNotice::refresh);
}

void HijackDeprecationNotice::refresh() {
    const bool inUse = settings_.enable_dns_server || settings_.enable_redirect;
    if (!inUse || previewWithoutNotices() || markers_.IsMarked(Configs::Markers::HijackDeprecated)) {
        status_->removeNotice(HijackNoticeId);
        return;
    }
    QString detail = QCoreApplication::translate(
        "HijackDeprecationNotice",
        "Hijack (Preferences > Routing Settings > Hijack) is deprecated and will be removed in the next release.");
#ifdef Q_OS_WIN
    detail += QLatin1Char(' ') + QCoreApplication::translate(
                                     "HijackDeprecationNotice",
                                     "The System DNS option depends on it and will be removed along with it.");
#endif
    detail += QLatin1Char(' ') + QCoreApplication::translate("HijackDeprecationNotice", "Tun mode covers the same use case.");
    // A tip, not a warning: a pending restart the user just caused must keep the slot ahead of it.
    status_->postNotice({HijackNoticeId,
                         QCoreApplication::translate("HijackDeprecationNotice", "Hijack is deprecated"),
                         detail,
                         QCoreApplication::translate("HijackDeprecationNotice", "Routing Settings"),
                         QCoreApplication::translate("HijackDeprecationNotice", "Don't show again")});
}
