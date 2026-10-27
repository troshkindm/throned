#include "include/ui/mainwindow.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/ui/widget/PendingRestartNotice.h"

void MainWindow::showRestartReasons() {
    if (pendingRestartNotice == nullptr) return;
    const auto reasons = connection()->restartReasons();
    if (reasons.isEmpty()) {
        pendingRestartNotice->clear();
        return;
    }
    for (const auto reason: reasons) pendingRestartNotice->noteChange(restartReasonText(reason));
}
