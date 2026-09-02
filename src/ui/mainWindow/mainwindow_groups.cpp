#include "include/ui/mainwindow.h"

#include <QAbstractItemView>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QStyle>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/SubInfo.h"
#include "include/database/GroupsRepo.h"
#include "include/ui/group/dialog_edit_group.h"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/mainWindow/TestRunner.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/GroupTabBar.h"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/SubscriptionPopover.hpp"


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
    page->setProperty("thronedCard", true);
    page->setAttribute(Qt::WA_StyledBackground, true);
    // The card paints its border inside its own rect, so the table has to sit one
    // pixel in or it covers that border with its own opaque background. The .ui
    // page carries this inset; pages built for the other groups did not, which is
    // why the left edge existed on the first tab and nowhere else.
    if (auto *pageLayout = page->layout(); pageLayout != nullptr) {
        pageLayout->setContentsMargins(1, 0, 1, 1);
    }
    page->style()->unpolish(page);
    page->style()->polish(page);
    if (announceHost != nullptr) page->layout()->addWidget(announceHost);
    page->layout()->addWidget(ui->profilesTableView);
    refreshAnnounceStrip();

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
    // Rebuilding tabs can repaint the backing group. Favourites are an external
    // view, so restore the strip's deliberately suppressed group emphasis.
    syncGroupTabSelection();
}

// The strip right of the last tab belongs to the tabWidget, not the tab bar.
void MainWindow::on_tabWidget_customContextMenuRequested(const QPoint &p) {
    show_group_tab_menu(ui->tabWidget->tabBar()->mapFrom(ui->tabWidget, p));
}

void MainWindow::show_group_tab_menu(const QPoint &p) {
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

        // The button lives on this strip, so this is the first place someone looks
        // for it after hiding it.
        menu.addSeparator();
        addFavoritesButtonAction(menu);
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
    if (clickedGroup != nullptr && Configs::ParseSubInfo(clickedGroup->info).valid) {
        connect(menu.addAction(tr("Subscription details")), &QAction::triggered, this,
                [=, this] { showSubscriptionPopover(clickedIndex); });
        menu.addSeparator();
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

namespace {
    // Thresholds a plan can cross. The mask is recomputed from the current state on
    // every readout, so a renewal clears the bits on its own and the notice can fire
    // again next period without any history being kept.
    enum SubNotice { NoticeWeek = 1 << 0, NoticeThreeDays = 1 << 1, NoticeLastDay = 1 << 2, NoticeQuota = 1 << 3 };

    constexpr double kQuotaWarnFrom = 0.75;
    constexpr double kQuotaCriticalFrom = 0.90;

    GroupTabBar::Urgency subscriptionUrgency(const Configs::SubInfo &sub, int days) {
        auto urgency = GroupTabBar::Urgency::Normal;
        if (const double used = sub.usedFraction(); used >= kQuotaCriticalFrom)
            urgency = GroupTabBar::Urgency::Critical;
        else if (used >= kQuotaWarnFrom)
            urgency = GroupTabBar::Urgency::Warning;

        if (days < 0) return urgency;
        if (days <= 2) return GroupTabBar::Urgency::Critical;
        if (days <= 7 && urgency == GroupTabBar::Urgency::Normal) return GroupTabBar::Urgency::Warning;
        return urgency;
    }
}

void MainWindow::notifySubscriptionState(const std::shared_ptr<Configs::Group> &group,
                                         const Configs::SubInfo &sub, int days) {
    int due = 0;
    if (days >= 0) {
        if (days <= 7) due |= NoticeWeek;
        if (days <= 3) due |= NoticeThreeDays;
        if (days <= 1) due |= NoticeLastDay;
    }
    if (sub.usedFraction() >= kQuotaCriticalFrom) due |= NoticeQuota;

    const int fresh = due & ~group->provider.notifiedMask;
    if (due == group->provider.notifiedMask && fresh == 0) return;
    group->provider.notifiedMask = due;
    Configs::dataManager->groupsRepo->Save(group);
    if (fresh == 0 || tray == nullptr) return;

    // Only the sharpest new threshold speaks; the rest are already implied by it.
    QString body;
    if (fresh & NoticeLastDay) body = days == 0 ? tr("Expires today.") : tr("Expires tomorrow.");
    else if (fresh & NoticeThreeDays) body = tr("%n day(s) left.", "", days);
    else if (fresh & NoticeWeek) body = tr("%n day(s) left.", "", days);
    else if (fresh & NoticeQuota)
        body = sub.total > 0
                   ? tr("%1 of %2 used.").arg(ReadableSize(sub.used()), ReadableSize(sub.total))
                   : tr("Almost out of traffic.");

    tray->showMessage(group->name, body, QSystemTrayIcon::Warning, 10000);
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

    const int days = sub.daysLeft(QDateTime::currentSecsSinceEpoch());
    bar->setUsage(index, sub.usedFraction(), subscriptionUrgency(sub, days));

    QStringList lines;
    lines << (sub.total == 0
                  ? tr("Used %1 of an unlimited plan").arg(ReadableSize(sub.used()))
                  : tr("%1 of %2 used, %3 left")
                        .arg(ReadableSize(sub.used()), ReadableSize(sub.total), ReadableSize(qMax<qint64>(0, sub.total - sub.used()))));

    if (days >= 0) {
        lines << (days == 0 ? tr("Expires today (%1)").arg(DisplayTime(sub.expire, QLocale::ShortFormat))
                            : tr("%n day(s) left (%1)", "", days).arg(DisplayTime(sub.expire, QLocale::ShortFormat)));
    }
    if (group->sub_last_update > 0) {
        lines << tr("Updated %1").arg(DisplayTime(group->sub_last_update, QLocale::ShortFormat));
    }
    bar->setTabToolTip(index, lines.join('\n'));

    notifySubscriptionState(group, sub, days);
}


void MainWindow::showSubscriptionPopover(int tabIndex) {
    auto *bar = ui->tabWidget->groupTabBar();
    if (bar == nullptr) return;
    const auto group = Configs::dataManager->groupsRepo->GetGroup(bar->tabData(tabIndex).toInt());
    if (group == nullptr) return;

    auto *popover = new SubscriptionPopover([](int gid) {
        if (const auto target = Configs::dataManager->groupsRepo->GetGroup(gid);
            target != nullptr && !target->url.isEmpty()) {
            Subscription::groupUpdater->AsyncUpdate(target->url, gid);
        }
    }, this);
    const QRect rect = bar->tabRect(tabIndex);
    popover->popupFor(group, bar->mapToGlobal(QPoint(rect.left(), rect.bottom())));
}
// A subscription refresh rewrites the allowance but leaves the tabs standing, so the
// readout is repainted on its own rather than through a full rebuild of the strip.
void MainWindow::refreshSubscriptionReadouts() {
    auto *bar = ui->tabWidget->groupTabBar();
    if (bar == nullptr) return;
    for (int i = 0; i < bar->count(); i++) {
        applySubscriptionReadout(i, Configs::dataManager->groupsRepo->GetGroup(bar->tabData(i).toInt()));
    }
    refreshAnnounceStrip();
}
// A subscription can carry a line of text from its provider. It rides with the
// profile table into whichever group page is current, so one widget serves them all.
namespace {
    QString announceFingerprint(const QString &text) {
        return QString::fromLatin1(
            QCryptographicHash::hash(text.trimmed().toUtf8(), QCryptographicHash::Md5).toHex());
    }
}

void MainWindow::setupAnnounceStrip() {
    announceHost = new QWidget(this);
    auto *hostLayout = new QVBoxLayout(announceHost);
    // Inset from the card border so the strip reads as a notice inside the pane.
    hostLayout->setContentsMargins(7, 7, 7, 6);
    hostLayout->setSpacing(0);

    auto *strip = new QFrame(announceHost);
    strip->setObjectName(QStringLiteral("subAnnounceStrip"));
    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(10, 5, 6, 5);
    layout->setSpacing(8);

    announceIcon = new QLabel(strip);
    announceIcon->setObjectName(QStringLiteral("subAnnounceIcon"));
    announceIcon->setFixedSize(16, 16);
    layout->addWidget(announceIcon);

    announceText = new QLabel(strip);
    announceText->setObjectName(QStringLiteral("subAnnounceText"));
    // The text is the provider's, so it must never be read as markup.
    announceText->setTextFormat(Qt::PlainText);
    announceText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusElidedLabels.append(announceText);
    announceText->installEventFilter(this);
    layout->addWidget(announceText, 1);

    announceClose = new QToolButton(strip);
    announceClose->setObjectName(QStringLiteral("subAnnounceClose"));
    announceClose->setCursor(Qt::PointingHandCursor);
    announceClose->setFocusPolicy(Qt::NoFocus);
    announceClose->setFixedSize(18, 18);
    announceClose->setIconSize(QSize(12, 12));
    announceClose->setToolTip(tr("Dismiss until the provider changes it"));
    connect(announceClose, &QToolButton::clicked, this, &MainWindow::dismissAnnounce);
    layout->addWidget(announceClose);

    hostLayout->addWidget(strip);
    announceHost->hide();

    const auto retint = [this] {
        const auto colors = themeManager->Colors();
        announceIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Campaign, colors.accent, 16));
        announceClose->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Close, colors.textSubtle, 12));
    };
    retint();
    connect(themeManager, &ThemeManager::themeChanged, this, retint);
}

void MainWindow::refreshAnnounceStrip() {
    if (announceHost == nullptr) return;
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    const QString text = group == nullptr ? QString() : group->provider.announce.trimmed();
    const bool unseen = !text.isEmpty()
                     && announceFingerprint(text) != group->provider.announceSeen;
    announceHost->setVisible(unseen);
    if (!unseen) return;
    setStatusText(announceText, text);
}

void MainWindow::dismissAnnounce() {
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (group == nullptr) return;
    group->provider.announceSeen = announceFingerprint(group->provider.announce);
    Configs::dataManager->groupsRepo->Save(group);
    announceHost->hide();
}
