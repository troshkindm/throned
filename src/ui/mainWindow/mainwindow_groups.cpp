#include "include/ui/mainwindow.h"

#include <QAbstractItemView>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QStyle>
#include <QVBoxLayout>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/SubInfo.h"
#include "include/database/GroupsRepo.h"
#include "include/ui/group/dialog_edit_group.h"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/mainWindow/TestRunner.h"


void MainWindow::on_tabWidget_currentChanged(int index) {
    if (Configs::dataManager->settingsRepo->refreshing_group_list) return;
    const auto gid = tabIndex2GroupId(index);
    if (gid == Configs::dataManager->settingsRepo->current_group) return;
    show_group(gid);
}

void MainWindow::show_group(int gid) {
    if (Configs::dataManager->settingsRepo->refreshing_group) return;
    Configs::dataManager->settingsRepo->refreshing_group = true;

    const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) {
        MessageBoxWarning(tr("Error"), QString("No such group: %1").arg(gid));
        Configs::dataManager->settingsRepo->refreshing_group = false;
        return;
    }

    if (Configs::dataManager->settingsRepo->current_group != gid) {
        saveProfileFocusState();
        if (auto lastGroup = Configs::dataManager->groupsRepo->CurrentGroup()) {
            lastGroup->scroll_last_profile = ui->profilesTableView->firstVisibleRow();
            Configs::dataManager->groupsRepo->Save(lastGroup);
        }
        Configs::dataManager->settingsRepo->current_group = gid;
        Configs::dataManager->settingsRepo->Save();
    }

    // The page, not the table, paints the rounded card: a QTableView fills its
    // viewport square, so the pane's corners never survive underneath it.
    auto *page = ui->tabWidget->widget(groupId2TabIndex(gid));
    page->setObjectName(QStringLiteral("profilesPage"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->style()->unpolish(page);
    page->style()->polish(page);
    page->layout()->addWidget(ui->profilesTableView);

    refresh_proxy_list({}, true);

    // scroll_last_profile came from firstVisibleRow(), so it is a proxy row.
    const int rowCount = profilesFilterModel->rowCount();
    int targetRow = group->scroll_last_profile;
    if (targetRow >= rowCount && rowCount > 0) targetRow = rowCount - 1;
    QTimer::singleShot(0, ui->profilesTableView, [=, this]() {
        if (targetRow >= 0) {
            if (QModelIndex idx = profilesFilterModel->index(targetRow, 0); idx.isValid()) {
                ui->profilesTableView->scrollTo(idx, QAbstractItemView::PositionAtTop);
            }
        }
        refresh_proxy_list_column_size();
    });

    Configs::dataManager->settingsRepo->refreshing_group = false;
}

void MainWindow::refresh_groups() {
    Configs::dataManager->settingsRepo->refreshing_group_list = true;

    for (int i = ui->tabWidget->count() - 1; i > 0; i--) {
        ui->tabWidget->removeTab(i);
    }

    int index = 0;
    for (const auto &gid: Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (index == 0) {
            ui->tabWidget->setTabText(0, group->name);
        } else {
            auto widget2 = new QWidget();
            auto layout2 = new QVBoxLayout();
            layout2->setContentsMargins(QMargins());
            layout2->setSpacing(0);
            widget2->setLayout(layout2);
            ui->tabWidget->addTab(widget2, group->name);
        }
        ui->tabWidget->tabBar()->setTabData(index, gid);
        applySubscriptionReadout(index, group);
        index++;
    }

    if (Configs::dataManager->groupsRepo->CurrentGroup() == nullptr) {
        Configs::dataManager->settingsRepo->current_group = -1;
        ui->tabWidget->setCurrentIndex(groupId2TabIndex(0));
        const auto tabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
        show_group(tabOrder.count() > 0 ? tabOrder.first() : 0);
    } else {
        ui->tabWidget->setCurrentIndex(groupId2TabIndex(Configs::dataManager->settingsRepo->current_group));
        show_group(Configs::dataManager->settingsRepo->current_group);
    }

    Configs::dataManager->settingsRepo->refreshing_group_list = false;
}

void MainWindow::on_tabWidget_customContextMenuRequested(const QPoint &p) {
    const int clickedIndex = ui->tabWidget->tabBar()->tabAt(p);
    if (clickedIndex == -1) {
        QMenu menu(this);
        connect(menu.addAction(tr("Add new Group")), &QAction::triggered, this, [=,this]{
            auto ent = Configs::dataManager->groupsRepo->NewGroup();
            auto dialog = new DialogEditGroup(ent, this);
            const int ret = dialog->exec();
            dialog->deleteLater();

            if (ret == QDialog::Accepted) {
                Configs::dataManager->groupsRepo->AddGroup(ent);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
        });

        menu.exec(ui->tabWidget->tabBar()->mapToGlobal(p));
        return;
    }

    ui->tabWidget->setCurrentIndex(clickedIndex);
    QMenu menu(this);

    const auto clickedGroup = Configs::dataManager->groupsRepo->GetGroup(Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex]);

    connect(menu.addAction(tr("Add new Group")), &QAction::triggered, this, [=,this]{
        auto ent = Configs::dataManager->groupsRepo->NewGroup();
        auto dialog = new DialogEditGroup(ent, this);
        const int ret = dialog->exec();
        dialog->deleteLater();

        if (ret == QDialog::Accepted) {
            Configs::dataManager->groupsRepo->AddGroup(ent);
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
    });
    connect(menu.addAction(tr("Edit selected Group")), &QAction::triggered, this, [=,this]{
        const auto id = Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex];
        auto ent = Configs::dataManager->groupsRepo->GetGroup(id);
        auto dialog = new DialogEditGroup(ent, this);
        connect(dialog, &QDialog::finished, this, [=,this] {
            if (dialog->result() == QDialog::Accepted) {
                Configs::dataManager->groupsRepo->Save(ent);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
            dialog->deleteLater();
        });
        dialog->show();
    });
    if (Configs::dataManager->groupsRepo->GetAllGroupIds().size() > 1) {
        connect(menu.addAction(tr("Delete selected Group")), &QAction::triggered, this, [=,this] {
            const auto id = Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex];
            if (QMessageBox::question(this, tr("Confirmation"), tr("Remove %1?").arg(Configs::dataManager->groupsRepo->GetGroup(id)->name)) ==
                QMessageBox::StandardButton::Yes) {
                if (running != nullptr) {
                    if (running->gid == id) profile_stop(false, true, false);
                }
                Configs::dataManager->groupsRepo->DeleteGroup(id);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
        });
    }
    if (clickedGroup != nullptr && !clickedGroup->url.isEmpty()) {
        connect(menu.addAction(tr("Update subscription")), &QAction::triggered, this, [=,this]{
            const auto id = Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex];
            auto group = Configs::dataManager->groupsRepo->GetGroup(id);
            if (group->url.isEmpty()) return;
            if (mw_sub_updating) return;
            mw_sub_updating = true;
            Subscription::groupUpdater->AsyncUpdate(group->url, group->id, [&] { mw_sub_updating = false; }, true);
        });
    }
    if (clickedGroup != nullptr) {
        connect(menu.addAction(tr("Url Test selected Group")), &QAction::triggered, this, [=,this]{
            testRunner->runUrlTests(clickedGroup->Profiles());
        });
        connect(menu.addAction(tr("Speed Test selected Group")), &QAction::triggered, this, [=,this]{
            testRunner->runSpeedTests(clickedGroup->Profiles());
        });
    }
    menu.exec(ui->tabWidget->tabBar()->mapToGlobal(p));
}

// The tab carries the subscription's state: a usage hairline under the name, and
// the numbers behind it in the tooltip, so the strip stays one line high.
void MainWindow::applySubscriptionReadout(int index, const std::shared_ptr<Configs::Group> &group) {
    auto *bar = ui->tabWidget->groupTabBar();
    if (bar == nullptr || group == nullptr) return;

    const auto sub = Configs::ParseSubInfo(group->info);
    if (!sub.valid) {
        bar->setUsage(index, -1);
        bar->setTabToolTip(index, {});
        return;
    }

    bar->setUsage(index, sub.usedFraction());

    QStringList lines;
    lines << (sub.total == 0
                  ? tr("Used %1 of an unlimited plan").arg(ReadableSize(sub.used()))
                  : tr("%1 of %2 used, %3 left")
                        .arg(ReadableSize(sub.used()), ReadableSize(sub.total), ReadableSize(qMax<qint64>(0, sub.total - sub.used()))));

    if (const int days = sub.daysLeft(QDateTime::currentSecsSinceEpoch()); days >= 0) {
        lines << (days == 0 ? tr("Expires today (%1)").arg(DisplayTime(sub.expire, QLocale::ShortFormat))
                            : tr("%n day(s) left (%1)", "", days).arg(DisplayTime(sub.expire, QLocale::ShortFormat)));
    }
    if (group->sub_last_update > 0) {
        lines << tr("Updated %1").arg(DisplayTime(group->sub_last_update, QLocale::ShortFormat));
    }
    bar->setTabToolTip(index, lines.join('\n'));
}

// A subscription refresh rewrites the allowance but leaves the tabs standing, so the
// readout is repainted on its own rather than through a full rebuild of the strip.
void MainWindow::refreshSubscriptionReadouts() {
    auto *bar = ui->tabWidget->groupTabBar();
    if (bar == nullptr) return;
    for (int i = 0; i < bar->count(); i++) {
        applySubscriptionReadout(i, Configs::dataManager->groupsRepo->GetGroup(bar->tabData(i).toInt()));
    }
}
