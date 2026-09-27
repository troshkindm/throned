#include "include/ui/mainwindow.h"

#include <QFrame>
#include <QHeaderView>
#include <QLayout>
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

void invalidateHints(QWidget *widget) {
    for (auto *child: widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        invalidateHints(child);
    if (widget->layout() != nullptr) widget->layout()->invalidate();
    widget->updateGeometry();
}
} // namespace

void MainWindow::applyTopBarMetrics() {
    if (simpleModeActive) {
        applyWindowMinimum();
        return;
    }
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
    invalidateHints(commandBarFrame);
    fullLayoutWidth = commandBarFrame != nullptr ? commandBarFrame->sizeHint().width() + 2 : 0;
    setNarrowLayout(true);
    updateProfileMinimumHeight();
    invalidateHints(this);
    narrowMinimumWidth = qMax(designMinimumSize.width(), minimumSizeHint().width());
    setNarrowLayout(wasNarrow);
    invalidateHints(this);
    applyWindowMinimum();
    updateNarrowLayout();
}

void MainWindow::applyWindowMinimum() {
    if (simpleModeActive) {
        const QSize minimum = minimumSizeHint().expandedTo(QSize(360, 560));
        if (minimumSize() != minimum) setMinimumSize(minimum);
        return;
    }
    if (narrowMinimumWidth <= 0) return;
    const QSize content = minimumSizeHint();
    const QSize minimum(narrowLayout ? qMax(narrowMinimumWidth, content.width()) : narrowMinimumWidth,
                        qMax(designMinimumSize.height(), content.height()));
    if (minimumSize() == minimum) return;
    setMinimumSize(minimum);
    FitWindowToScreen(this);
}

void MainWindow::updateProfileMinimumHeight() {
    auto *table = ui->profilesTableView;
    table->setMinimumHeight(table->horizontalHeader()->sizeHint().height() +
                            table->verticalHeader()->defaultSectionSize() + 2 * table->frameWidth());
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
    for (auto *page: {ui->graph_tab, ui->runtime_tab}) {
        const int index = ui->stats_widget->indexOf(page);
        if (!page->property("fullTabText").isValid())
            page->setProperty("fullTabText", ui->stats_widget->tabText(index));
        const QString full = page->property("fullTabText").toString();
        const QString text = narrow ? (page == ui->graph_tab ? tr("Graph") : tr("Runtime")) : full;
        ui->stats_widget->setTabText(index, text);
        ui->stats_widget->setTabToolTip(index, narrow ? full : QString());
        for (auto *button: statsStripTabs) {
            if (button->property("statsPage").toString() != page->objectName()) continue;
            button->setText(text);
            button->setToolTip(narrow ? full : QString());
        }
    }
    // Without scroll buttons the panel tabs would run under its corner tools; eliding keeps each one reachable.
    ui->stats_widget->tabBar()->setElideMode(narrow ? Qt::ElideRight : Qt::ElideNone);
}

void MainWindow::updateNarrowLayout() {
    if (simpleModeActive) return;
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
