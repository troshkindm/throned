#include "include/ui/profile/edit_wireguard.h"

#include "include/api/RPC.h"
#include "include/configs/sub/warp.h"
#include "include/global/Utils.hpp"

EditWireguard::EditWireguard(QWidget *parent) : QWidget(parent), ui(new Ui::EditWireguard) {
    ui->setupUi(this);

    connect(ui->warp_autogen, &QPushButton::clicked, this, [=, this] {
        auto originalText = ui->warp_autogen->text();
        // genWarpConfig spins a nested event loop, so the button stays clickable through the request.
        ui->warp_autogen->setEnabled(false);
        ui->warp_autogen->setText(tr("Getting keypair..."));
        bool ok;
        auto keyPair = API::defaultClient->GenWgKeyPair(&ok);
        if (!ok) {
            runOnUiThread([=] { MessageBoxWarning(tr("Failed to get key pair"), keyPair.error->c_str()); });
            ui->warp_autogen->setText(originalText);
            ui->warp_autogen->setEnabled(true);
            return;
        }
        ui->warp_autogen->setText(tr("Generating config..."));
        QString error;
        auto conf = Configs_network::genWarpConfig(&error, keyPair.private_key->c_str(), keyPair.public_key->c_str());
        if (!error.isEmpty()) {
            runOnUiThread([=] { MessageBoxWarning(tr("Failed to generate warp config"), error); });
            ui->warp_autogen->setText(originalText);
            ui->warp_autogen->setEnabled(true);
            return;
        }
        ui->private_key->setText(conf->privateKey);
        ui->public_key->setText(conf->publicKey);
        ui->local_addr->setText(conf->ipv4Address + "/32," + conf->ipv6Address + "/128");
        ui->mtu->setText("1280");
        ui->persistent_keepalive->setText("30");
        // The endpoint lives in the outer profile dialog's address/port fields.
        if (auto sep = conf->endpoint.lastIndexOf(':'); sep > 0) {
            if (set_edit_text_serverAddress) set_edit_text_serverAddress(conf->endpoint.left(sep));
            if (set_edit_text_serverPort) set_edit_text_serverPort(conf->endpoint.mid(sep + 1));
        }
        ui->reserved->setText(QListInt2QListString(conf->reserved).join(","));
        ui->warp_autogen->setText(tr("Success!"));
        setTimeout([=, this] {
            ui->warp_autogen->setText(originalText);
            ui->warp_autogen->setEnabled(true);
        }, this, 2000);
    });
}

EditWireguard::~EditWireguard() {
    delete ui;
}

void EditWireguard::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->Wireguard();

#ifndef Q_OS_LINUX
    adjustSize();
#endif

    ui->private_key->setText(outbound->private_key);
    ui->public_key->setText(outbound->peer->public_key);
    ui->preshared_key->setText(outbound->peer->pre_shared_key);
    ui->reserved->setText(QListInt2QListString(outbound->peer->reserved).join(","));
    ui->persistent_keepalive->setText(outbound->peer->persistent_keepalive);
    ui->mtu->setText(Int2String(outbound->mtu));
    ui->sys_ifc->setChecked(outbound->system);
    ui->local_addr->setText(outbound->address.join(","));
    ui->workers->setText(Int2String(outbound->worker_count));

    ui->enable_amnezia->setChecked(outbound->enable_amnezia);
    ui->jc->setText(Int2String(outbound->jc));
    ui->jmin->setText(Int2String(outbound->jmin));
    ui->jmax->setText(Int2String(outbound->jmax));
    ui->s1->setText(Int2String(outbound->s1));
    ui->s2->setText(Int2String(outbound->s2));
    ui->s3->setText(Int2String(outbound->s3));
    ui->s4->setText(Int2String(outbound->s4));
    ui->h1->setText(outbound->h1);
    ui->h2->setText(outbound->h2);
    ui->h3->setText(outbound->h3);
    ui->h4->setText(outbound->h4);
    ui->i1->setText(outbound->i1);
    ui->i2->setText(outbound->i2);
    ui->i3->setText(outbound->i3);
    ui->i4->setText(outbound->i4);
    ui->i5->setText(outbound->i5);
    ui->header_protection_key->setText(outbound->header_protection_key);
    ui->content_padding_addition->setText(outbound->content_padding_addition);
    ui->rekey_after_time->setText(outbound->rekey_after_time);
    ui->rekey_timeout->setText(outbound->rekey_timeout);
    ui->reject_after_time->setText(outbound->reject_after_time);
    ui->keepalive_timeout->setText(outbound->keepalive_timeout);
    ui->max_handshake_attempts->setText(outbound->max_handshake_attempts);
    ui->random_trailers->setChecked(outbound->random_trailers);
    ui->disable_cookies->setChecked(outbound->disable_cookies);
}

bool EditWireguard::onEnd() {
    auto outbound = this->ent->Wireguard();

    outbound->private_key = ui->private_key->text().trimmed();
    outbound->peer->public_key = ui->public_key->text().trimmed();
    outbound->peer->pre_shared_key = ui->preshared_key->text().trimmed();
    auto rawReserved = ui->reserved->text();
    outbound->peer->reserved = {};
    for (const auto& item: rawReserved.split(",")) {
        if (item.trimmed().isEmpty()) continue;
        outbound->peer->reserved += item.trimmed().toInt();
    }
    outbound->peer->persistent_keepalive = ui->persistent_keepalive->text().trimmed();
    outbound->mtu = ui->mtu->text().trimmed().toInt();
    outbound->system = ui->sys_ifc->isChecked();
    outbound->address = ui->local_addr->text().replace(" ", "").split(",");
    outbound->worker_count = ui->workers->text().trimmed().toInt();

    outbound->enable_amnezia = ui->enable_amnezia->isChecked();
    outbound->jc = ui->jc->text().trimmed().toInt();
    outbound->jmin = ui->jmin->text().trimmed().toInt();
    outbound->jmax = ui->jmax->text().trimmed().toInt();
    outbound->s1 = ui->s1->text().trimmed().toInt();
    outbound->s2 = ui->s2->text().trimmed().toInt();
    outbound->s3 = ui->s3->text().trimmed().toInt();
    outbound->s4 = ui->s4->text().trimmed().toInt();
    outbound->h1 = ui->h1->text().trimmed();
    outbound->h2 = ui->h2->text().trimmed();
    outbound->h3 = ui->h3->text().trimmed();
    outbound->h4 = ui->h4->text().trimmed();
    outbound->i1 = ui->i1->text().trimmed();
    outbound->i2 = ui->i2->text().trimmed();
    outbound->i3 = ui->i3->text().trimmed();
    outbound->i4 = ui->i4->text().trimmed();
    outbound->i5 = ui->i5->text().trimmed();
    outbound->header_protection_key = ui->header_protection_key->text().trimmed();
    outbound->content_padding_addition = ui->content_padding_addition->text().trimmed();
    outbound->rekey_after_time = ui->rekey_after_time->text().trimmed();
    outbound->rekey_timeout = ui->rekey_timeout->text().trimmed();
    outbound->reject_after_time = ui->reject_after_time->text().trimmed();
    outbound->keepalive_timeout = ui->keepalive_timeout->text().trimmed();
    outbound->max_handshake_attempts = ui->max_handshake_attempts->text().trimmed();
    outbound->random_trailers = ui->random_trailers->isChecked();
    outbound->disable_cookies = ui->disable_cookies->isChecked();

    return true;
}