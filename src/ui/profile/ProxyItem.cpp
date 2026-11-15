#include "include/ui/profile/ProxyItem.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

#include <QApplication>
#include <QMessageBox>
#include <QPalette>
#include <QStyle>

#include "include/configs/generate.h"

// Throned themes keep no app-wide sheet matching colorRole, so the role is painted through the palette here.
void ProxyItem::setColorRole(QWidget *widget, const QString &role) {
    if (widget->property("colorRole").toString() == role) return;
    widget->setProperty("colorRole", role);
    const auto colors = themeManager()->Colors();
    const QColor color = role == QLatin1String("muted")     ? colors.textMuted
                         : role == QLatin1String("danger")  ? colors.danger
                         : role == QLatin1String("success") ? colors.success
                         : role == QLatin1String("warning") ? colors.warning
                                                            : QColor();
    QPalette pal = widget->palette();
    pal.setColor(QPalette::WindowText, color.isValid() ? color : QApplication::palette(widget).color(QPalette::WindowText));
    widget->setPalette(pal);
}

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
    ui->address->setText(Configs::DisplayEffectiveAddress(ent));
    ui->traffic->setText(ent->DisplayTraffic());
    ui->test_result->setText(ent->DisplayTestResult());
    ui->test_result->setToolTip({});
    setColorRole(ui->test_result, {});

    runOnThread(
        [=, this] {
            adjustSize();
            item->setSizeHint(sizeHint());
            dynamic_cast<QWidget *>(parent())->adjustSize();
        },
        this);
}

void ProxyItem::showStepResult(const QString &text, const QString &toolTip, const QString &colorRole) {
    ui->test_result->setText(text);
    ui->test_result->setToolTip(toolTip);
    setColorRole(ui->test_result, colorRole);
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
