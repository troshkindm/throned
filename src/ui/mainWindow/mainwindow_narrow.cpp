#include "include/ui/mainwindow.h"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolButton>

#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/utils/ProfilesTableModel.h"

namespace {
constexpr int kFullSearchWidth = 268;
constexpr int kNarrowSearchWidth = 170;
} // namespace

void MainWindow::applyTopBarMetrics() {
    // MainPreview deliberately lets each compact nav item fit its own label.
    const QList<QToolButton *> menuButtons = {
        ui->toolButton_program,
        ui->toolButton_preferences,
        ui->toolButton_testing,
        ui->toolButton_routing,
        ui->toolButton_tools,
    };
    for (auto *button: menuButtons) {
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->updateGeometry();
    }
    // Measured in both shapes: the labelled header decides when to fold, the folded one how far the window may shrink.
    const bool wasNarrow = narrowLayout;
    setNarrowLayout(false);
    fullLayoutWidth = commandBarFrame != nullptr ? commandBarFrame->sizeHint().width() + 2 : 0;
    setNarrowLayout(true);
    QWidget *panel = statsPanelHost != nullptr ? statsPanelHost : ui->stats_widget;
    const QList<int> splitSizes = ui->splitter->sizes();
    const bool panelShown = !panel->isHidden();
    const bool stripShown = statsStrip != nullptr && !statsStrip->isHidden();
    // An explicit minimum stops the layout raising the floor itself, and translated nav labels can outgrow the designed one.
    const auto floorFor = [this](const QSize &content) {
        return QSize(qMax(designMinimumSize.width(), content.width()), qMax(designMinimumSize.height(), content.height()));
    };
    panel->hide();
    if (statsStrip != nullptr) statsStrip->show();
    windowMinimumClosed = floorFor(minimumSizeHint());
    panel->show();
    if (statsStrip != nullptr) statsStrip->hide();
    windowMinimumOpen = floorFor(minimumSizeHint());
    panel->setVisible(panelShown);
    if (statsStrip != nullptr) statsStrip->setVisible(stripShown);
    ui->splitter->setSizes(splitSizes);
    setNarrowLayout(wasNarrow);
    applyWindowMinimum();
    updateNarrowLayout();
}

void MainWindow::applyWindowMinimum() {
    if (!windowMinimumClosed.isValid()) return;
    const bool open = Configs::dataManager->settingsRepo->stats_panel_open;
    setMinimumSize(open ? windowMinimumOpen.expandedTo(windowMinimumClosed) : windowMinimumClosed);
    FitWindowToScreen(this);
}

void MainWindow::setNarrowLayout(bool narrow) {
    narrowLayout = narrow;
    for (auto *button: {ui->toolButton_program, ui->toolButton_preferences, ui->toolButton_testing,
                        ui->toolButton_routing, ui->toolButton_tools}) {
        button->setToolButtonStyle(narrow ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        button->setToolTip(narrow ? button->text() : QString());
    }
    for (const auto &[label, full, folded]: commandToggleLabels) {
        label->setText(narrow ? folded : full);
        label->setToolTip(narrow ? full : QString());
    }
    for (auto *cell: statusDetailCells) cell->setVisible(!narrow);
    if (statsStripHint != nullptr) statsStripHint->setVisible(!narrow);
    if (serverSearchField != nullptr) serverSearchField->setFixedWidth(narrow ? kNarrowSearchWidth : kFullSearchWidth);
    for (auto *button: selectionActionButtons) button->setVisible(!narrow);
    if (selectionActionsMenuButton != nullptr) selectionActionsMenuButton->setVisible(narrow);
    // Without scroll buttons the panel tabs would run under its corner tools; eliding keeps each one reachable.
    ui->stats_widget->tabBar()->setElideMode(narrow ? Qt::ElideRight : Qt::ElideNone);
}

void MainWindow::updateNarrowLayout() {
    if (commandBarFrame == nullptr || fullLayoutWidth <= 0) return;
    const bool narrow = width() < fullLayoutWidth;
    if (narrow != narrowLayout) setNarrowLayout(narrow);
    if (const int overflowing = metricColumnsOverflowing(); overflowing != narrowHiddenMetrics) {
        narrowHiddenMetrics = overflowing;
        applyProfileColumnVisibility();
    }
}

int MainWindow::metricColumnsOverflowing() const {
    if (profilesTableModel == nullptr || profilesTableModel->rowStyle() != ProfilesTableModel::RowStyle::Comfortable)
        return 0;
    const auto *settings = Configs::dataManager->settingsRepo.get();
    const QFont font = ui->profilesTableView->font();
    int room = ui->profilesTableView->viewport()->width() - ProfileRowDelegate::serverColumnFloor(font);
    if (settings->profiles_show_ping)
        room -= ProfileRowDelegate::metricColumnWidth(ProfilesTableModel::ColcPing, font);
    QList<int> droppable;
    if (settings->profiles_show_speed) droppable << ProfilesTableModel::ColcSpeed;
    if (settings->profiles_show_traffic) droppable << ProfilesTableModel::ColcTraffic;
    int needed = 0;
    for (const int column: droppable) needed += ProfileRowDelegate::metricColumnWidth(column, font);
    int dropped = 0;
    while (!droppable.isEmpty() && needed > room) {
        needed -= ProfileRowDelegate::metricColumnWidth(droppable.takeLast(), font);
        ++dropped;
    }
    return dropped;
}
