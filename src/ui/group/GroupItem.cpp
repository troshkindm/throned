#include "include/ui/group/GroupItem.h"

#include "include/ui/group/dialog_edit_group.h"
#include "include/ui/group/dialog_manage_groups.h"
#include "include/ui/utils/GuiUtils.hpp"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/SubInfo.h"

#include <QMessageBox>
#include <QPointer>

#include "include/database/GroupsRepo.h"
#include "include/engine/EngineHost.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/ui/stats/dialog_endpoint_details.h"

QString FormatSubInfo(const QString &info) {
    const auto sub = Configs::ParseSubInfo(info);
    if (!sub.valid) return "";
    return QObject::tr("Used: %1 Remain: %2 Expire: %3")
        .arg(ReadableSize(sub.used()),
             (sub.total == 0) ? QString::fromUtf8("\u221E") : ReadableSize(qMax<qint64>(0, sub.total - sub.used())),
             DisplayTime(sub.expire, QLocale::ShortFormat));
}

GroupItem::GroupItem(QWidget *parent, const std::shared_ptr<Configs::Group> &ent, QListWidgetItem *item) : QWidget(parent), ui(new Ui::GroupItem) {
    ui->setupUi(this);
    this->setLayoutDirection(Qt::LeftToRight);

    this->parentWindow = parent;
    this->ent = ent;
    this->item = item;
    if (ent == nullptr) return;

    ui->subinfo->setTextFormat(Qt::PlainText);

    connect(this, &GroupItem::edit_clicked, this, &GroupItem::on_edit_clicked);
    connect(Subscription::updater(), &Subscription::GroupUpdater::asyncUpdateCallback, this, [=, this](int gid) { if (gid == this->ent->id) refresh_data(); });

    refresh_data();
}

GroupItem::~GroupItem() {
    delete ui;
}

void GroupItem::refresh_data() {
    ui->name->setText(ent->name);

    auto type = ent->url.isEmpty() ? tr("Basic") : tr("Subscription");
    if (ent->archive) type = tr("Archive") + " " + type;
    type += " (" + Int2String(ent->Profiles().length()) + ")";
    ui->type->setText(type);

    if (ent->url.isEmpty()) {
        ui->url->hide();
        ui->subinfo->hide();
        ui->update_sub->hide();
    } else {
        ui->url->setText(ent->url);
        QStringList info;
        if (ent->sub_last_update != 0) {
            info << tr("Last update: %1").arg(DisplayTime(ent->sub_last_update, QLocale::ShortFormat));
        }
        auto subinfo = FormatSubInfo(ent->info);
        if (!ent->info.isEmpty()) {
            info << subinfo;
        }
        if (info.isEmpty()) {
            ui->subinfo->hide();
        } else {
            ui->subinfo->show();
            ui->subinfo->setText(info.join(" | "));
        }
    }
    runOnThread(
        [=, this] {
            adjustSize();
            item->setSizeHint(sizeHint());
            dynamic_cast<QWidget *>(parent())->adjustSize();
        },
        this);
}

void GroupItem::on_update_sub_clicked() {
    Subscription::updater()->RefreshGroup(ent->id, nullptr, true);
}

void GroupItem::on_edit_clicked() {
    auto dialog = new DialogEditGroup(ent, parentWindow);
    connect(dialog, &QDialog::finished, this, [=, this] {
        dialog->deleteLater();
        if (dialog->result() != QDialog::Accepted) return;
        if (!Configs::dataManager->groupsRepo->Save(ent)) {
            MessageBoxWarning(software_name, tr("This group was deleted while you were editing it."));
            return;
        }
        refresh_data();
        MW_dialog_message(MwMessage::GroupsChanged, dialog->RestartNeeded() ? QStringList{MwArg::RestartProxy} : QStringList{});
    });
    dialog->show();
}

void GroupItem::on_remove_clicked() {
    auto *manage = qobject_cast<DialogManageGroups *>(parentWindow);
    if (manage == nullptr) return;
    const int id = ent->id;
    if (const auto error = manage->deleteGroup(id, true)) {
        if (error->code != QLatin1String("last_group")) MessageBoxWarning(software_name, GroupDeleteRefusalText(*error));
        return;
    }
    // The question box and a blocking stop run nested loops, where a refresh may remove this row; the box hangs off the dialog.
    const QPointer<GroupItem> self(this);
    if (QMessageBox::question(manage, tr("Confirmation"), tr("Remove %1?").arg(ent->name)) != QMessageBox::StandardButton::Yes || self == nullptr) {
        return;
    }
    if (const auto running = Engine::host()->connection()->runningSnapshot(); running != nullptr && running->gid == id) {
        Engine::host()->connection()->stop({.block = true});
        if (self == nullptr) return;
    }
    if (const auto error = manage->deleteGroup(id)) {
        MessageBoxWarning(software_name, GroupDeleteRefusalText(*error));
        return;
    }
    manage->refreshGroups();
}
