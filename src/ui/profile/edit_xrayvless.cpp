#include "include/ui/profile/edit_xrayvless.h"

EditXrayVless::EditXrayVless(QWidget *parent)
    : QWidget(parent),
      ui(new Ui::EditXrayVless) {
    ui->setupUi(this);

    QStringList flows = {""};
    flows << Configs::xrayFlows;
    ui->xray_flow->addItems(flows);
}

EditXrayVless::~EditXrayVless() {
    delete ui;
}

void EditXrayVless::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = _ent->XrayVLESS();

    ui->xray_uuid->setText(outbound->uuid);
    ui->xray_enc->setText(outbound->encryption);
    ui->xray_flow->setCurrentText(outbound->flow);
}

bool EditXrayVless::onEnd() {
    auto outbound = ent->XrayVLESS();
    outbound->uuid = ui->xray_uuid->text().trimmed();
    outbound->encryption = ui->xray_enc->text().trimmed();
    outbound->flow = ui->xray_flow->currentText().trimmed();

    return true;
}