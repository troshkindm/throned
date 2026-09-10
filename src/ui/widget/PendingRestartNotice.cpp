#include "include/ui/widget/PendingRestartNotice.h"
#include "include/ui/widget/UpdateStatusWidget.h"

#include <QCoreApplication>
#include <utility>

namespace {
const QString PendingRestartId = QStringLiteral("proxy-restart-pending");
}

PendingRestartNotice::PendingRestartNotice(UpdateStatusWidget* status, std::function<void()> restart)
    : QObject(status), status_(status) {
    connect(status, &UpdateStatusWidget::noticeActionRequested, this,
            [this, restart = std::move(restart)](const QString& id) {
                if (id != PendingRestartId) return;
                clear();
                restart();
            });
    connect(status, &UpdateStatusWidget::noticeDismissed, this, [this](const QString& id) {
        if (id == PendingRestartId) clear();
    });
}

void PendingRestartNotice::noteChange(const QString& reason) {
    QString trimmed = reason.trimmed();
    if (trimmed.isEmpty()) trimmed = QCoreApplication::translate("PendingRestartNotice", "Settings");
    if (!reasons_.contains(trimmed)) {
        if (reasons_.size() < 4)
            reasons_ << trimmed;
        else if (reasons_.last() != QStringLiteral("…"))
            reasons_ << QStringLiteral("…");
    }
    status_->postNotice({PendingRestartId,
                         QCoreApplication::translate("PendingRestartNotice", "Settings changed, restart to apply"),
                         reasons_.join(QStringLiteral(", ")),
                         QCoreApplication::translate("PendingRestartNotice", "Restart"),
                         QCoreApplication::translate("PendingRestartNotice", "Ignore"),
                         UpdateStatusWidget::Severity::Tip, 10});
}

void PendingRestartNotice::clear() {
    reasons_.clear();
    status_->removeNotice(PendingRestartId);
}
