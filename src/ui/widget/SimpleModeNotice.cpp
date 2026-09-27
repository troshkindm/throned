#include "include/ui/widget/SimpleModeNotice.h"

#include "include/database/SettingsRepo.h"
#include "include/ui/widget/UpdateStatusWidget.h"

#include <QCoreApplication>
#include <QTimer>
#include <utility>

namespace {
const QString SimpleModeTipId = QStringLiteral("simple-mode-v1");

bool simpleTipSuppressed() {
    const auto arguments = QCoreApplication::arguments();
    return arguments.contains(QStringLiteral("-ui-preview")) &&
           !arguments.contains(QStringLiteral("-ui-preview-notices"));
}
} // namespace

SimpleModeNotice::SimpleModeNotice(UpdateStatusWidget *status, Configs::SettingsRepo &settings,
                                   std::function<void()> openSimpleMode)
    : QObject(status), status_(status), settings_(settings) {
    connect(status, &UpdateStatusWidget::noticeActionRequested, this,
            [this, openSimpleMode = std::move(openSimpleMode)](const QString &id) {
                if (id != SimpleModeTipId) return;
                remember();
                status_->removeNotice(id);
                openSimpleMode();
            });
    connect(status, &UpdateStatusWidget::noticeDismissed, this, [this](const QString &id) {
        if (id == SimpleModeTipId) remember();
    });
    QTimer::singleShot(0, this, &SimpleModeNotice::refresh);
}

void SimpleModeNotice::remember() {
    if (settings_.dismissed_notices.contains(SimpleModeTipId)) return;
    settings_.dismissed_notices.append(SimpleModeTipId);
    settings_.Save();
}

void SimpleModeNotice::refresh() {
    // Having opened Simple mode by any route is the answer the tip was asking for.
    if (settings_.simple_mode) remember();
    if (settings_.simple_mode || simpleTipSuppressed() || settings_.dismissed_notices.contains(SimpleModeTipId)) {
        status_->removeNotice(SimpleModeTipId);
        return;
    }
    status_->postNotice({SimpleModeTipId,
                         QCoreApplication::translate("SimpleModeNotice", "Try Simple mode"),
                         QCoreApplication::translate("SimpleModeNotice", "A small window with one button: pick a server and connect."),
                         QCoreApplication::translate("SimpleModeNotice", "Try it"),
                         QCoreApplication::translate("SimpleModeNotice", "Don't show again")});
}
