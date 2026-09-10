#include "include/ui/mainwindow.h"
#include "include/database/DatabaseManager.h"
#include "include/database/SettingsRepo.h"
#include "include/ui/widget/PendingRestartNotice.h"

void MainWindow::noteRestartNeeded(const QString& reason) {
    if (Configs::dataManager->settingsRepo->started_id < 0 || pendingRestartNotice == nullptr) return;
    pendingRestartNotice->noteChange(reason);
}

void MainWindow::clearRestartNeeded() {
    if (pendingRestartNotice != nullptr) pendingRestartNotice->clear();
}
