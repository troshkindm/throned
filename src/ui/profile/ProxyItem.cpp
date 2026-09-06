#include "include/ui/profile/ProxyItem.h"
#include "include/ui/widget/MaterialIcon.h"

#include <QMessageBox>

ProxyItem::ProxyItem(QWidget *parent, const std::shared_ptr<Configs::Profile> &ent, QListWidgetItem *item)
    : QWidget(parent), ui(new Ui::ProxyItem) {
    ui->setupUi(this);
    this->setLayoutDirection(Qt::LeftToRight);
    const auto iconColor = palette().color(QPalette::ButtonText);
    ui->change->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::SwapHorizontal, iconColor));
    ui->remove->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Delete, iconColor));

    this->item = item;
    this->ent = ent;
    if (ent == nullptr) return;

    refresh_data();
}

ProxyItem::~ProxyItem() {
    delete ui;
}

void ProxyItem::refresh_data() {
    ui->type->setText(ent->outbound->DisplayType());
    ui->name->setText(ent->outbound->DisplayName());
    ui->address->setText(ent->outbound->DisplayAddress());
    ui->traffic->setText(ent->DisplayTraffic());
    ui->test_result->setText(ent->DisplayTestResult());

    runOnThread(
        [=, this] {
            adjustSize();
            item->setSizeHint(sizeHint());
            dynamic_cast<QWidget *>(parent())->adjustSize();
        },
        this);
}

void ProxyItem::on_remove_clicked() {
    if (!this->remove_confirm ||
        QMessageBox::question(this, tr("Confirmation"), tr("Remove %1?").arg(ent->outbound->DisplayName())) == QMessageBox::StandardButton::Yes) {
        // TODO do remove (or not) -> callback
        delete item;
    }
}

QPushButton *ProxyItem::get_change_button() {
    return ui->change;
}
