#include "include/ui/profile/edit_mieru.h"

#include "include/global/GuiUtils.hpp"

EditMieru::EditMieru(QWidget *parent) : QWidget(parent), ui(new Ui::EditMieru) {
    ui->setupUi(this);
    ui->transport->addItems(Configs::mieruTransports);
    ui->multiplexing->addItems(Configs::mieruMultiplexing);
}

EditMieru::~EditMieru() {
    delete ui;
}

void EditMieru::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->Mieru();

    ui->username->setText(outbound->username);
    ui->password->setText(outbound->password);
    ui->transport->setCurrentText(outbound->transport.isEmpty() ? "TCP" : outbound->transport);
    ui->multiplexing->setCurrentText(outbound->multiplexing);
    ui->traffic_pattern->setText(outbound->traffic_pattern);
    ui->server_ports->setText(outbound->server_ports);
}

bool EditMieru::onEnd() {
    auto outbound = this->ent->Mieru();

    outbound->username = ui->username->text().trimmed();
    outbound->password = ui->password->text();
    outbound->transport = ui->transport->currentText().trimmed();
    outbound->multiplexing = ui->multiplexing->currentText().trimmed();
    outbound->traffic_pattern = ui->traffic_pattern->text().trimmed();
    outbound->server_ports = ui->server_ports->text().trimmed();

    return true;
}
