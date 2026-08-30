#include "include/ui/profile/edit_advanced.h"

#include "include/global/GuiUtils.hpp"

#include <QGuiApplication>
#include <QInputDialog>
#include <QNetworkInterface>
#include <QScreen>
#include <QAbstractSocket>
#include "include/database/DatabaseManager.h"
#include "include/ui/profile/editor_table_utils.h"

EditAdvanced::InterfaceFields EditAdvanced::GetInterfaceFields() const {
    if (auto *openvpn = ent->OpenVPN(); openvpn != nullptr) {
        return {&openvpn->system, &openvpn->interface_name, &openvpn->udp_timeout,
                &openvpn->udp_mapping, &openvpn->udp_filtering, &openvpn->udp_nat_max};
    }
    if (auto *openconnect = ent->OpenConnect(); openconnect != nullptr) {
        return {&openconnect->system, &openconnect->interface_name, &openconnect->udp_timeout,
                &openconnect->udp_mapping, &openconnect->udp_filtering, &openconnect->udp_nat_max};
    }
    return {};
}

EditAdvanced::EditAdvanced(QWidget *parent, const std::shared_ptr<Configs::Profile> &_ent)
    : QDialog(parent)
    , ui(new Ui::EditAdvanced)
{
    ui->setupUi(this);
    ent = _ent;
    auto dialFieldsObj = ent->outbound->dialFields;
    ui->reuse_addr->setChecked(dialFieldsObj->reuse_addr);
    ui->tcp_fast_open->setChecked(dialFieldsObj->tcp_fast_open);
    ui->udp_fragment->setChecked(dialFieldsObj->udp_fragment);
    ui->tcp_multipath->setChecked(dialFieldsObj->tcp_multi_path);
    ui->connect_timeout->setText(dialFieldsObj->connect_timeout);

    for (const auto& ifc : QNetworkInterface::allInterfaces())
        m_systemInterfaces << ifc.humanReadableName();
    for (const auto& addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol)
            m_systemIpv4Addresses << addr.toString();
        else if (addr.protocol() == QAbstractSocket::IPv6Protocol)
            m_systemIpv6Addresses << addr.toString();
    }

    auto populateBindCombo = [](QComboBox* combo, const QStringList& systemItems,
                                const QStringList& history, const QString& current) {
        combo->addItem("");
        combo->addItems(systemItems);
        for (const auto& h : history) {
            if (!systemItems.contains(h))
                combo->addItem(h);
        }
        combo->setCurrentText(current);
    };

    auto* repo = Configs::dataManager->settingsRepo.get();
    populateBindCombo(ui->bind_interface,    m_systemInterfaces,    repo->dial_bind_interface_history,    dialFieldsObj->bind_interface);
    populateBindCombo(ui->inet4_bind_address, m_systemIpv4Addresses, repo->dial_inet4_bind_address_history, dialFieldsObj->inet4_bind_address);
    populateBindCombo(ui->inet6_bind_address, m_systemIpv6Addresses, repo->dial_inet6_bind_address_history, dialFieldsObj->inet6_bind_address);

    if (ent->outbound->HasTLS()) {
        auto tlsObj = ent->outbound->GetTLS();
        ui->disable_sni->setChecked(tlsObj->disable_sni);
        ui->min_version->setText(tlsObj->min_version);
        ui->max_version->setText(tlsObj->max_version);
        ui->tls_spoof_state->setCurrentIndex(tlsObj->getSpoofState());
        ui->tls_spoof->setText(tlsObj->spoof);
        // a non-editable combo drops setCurrentText while it holds no items
        ui->tls_spoof_method->addItems(Configs::tlsSpoofMethods);
        ui->tls_spoof_method->setCurrentText(tlsObj->spoof_method);
        auto syncSpoofFields = [this] {
            const bool on = ui->tls_spoof_state->currentIndex() != 2;
            ui->tls_spoof->setEnabled(on);
            ui->tls_spoof_l->setEnabled(on);
            ui->tls_spoof_method->setEnabled(on && !ui->tls_spoof->text().isEmpty());
            ui->tls_spoof_method_l->setEnabled(on && !ui->tls_spoof->text().isEmpty());
        };
        connect(ui->tls_spoof, &QLineEdit::textChanged, this, syncSpoofFields);
        connect(ui->tls_spoof_state, &QComboBox::currentIndexChanged, this, syncSpoofFields);
        syncSpoofFields();
        ui->enable_ech->setChecked(tlsObj->ech->enabled);
        ui->ech_server_name->setText(tlsObj->ech->serverName);

        if (!tlsObj->ech->config.isEmpty()) {
            ui->ech_config->setText("Already set");
            CACHE.echConfig = tlsObj->ech->config;
        }
        if (!tlsObj->certificate_public_key_sha256.isEmpty()) {
            ui->cert_sha256->setText("Already set");
            CACHE.certSha256 = tlsObj->certificate_public_key_sha256;
        }
        if (!tlsObj->client_certificate.isEmpty()) {
            ui->client_cert->setText("Already set");
            CACHE.clientCert = tlsObj->client_certificate;
        }
        if (!tlsObj->client_key.isEmpty()) {
            ui->client_key->setText("Already set");
            CACHE.clientKey = tlsObj->client_key;
        }
    } else {
        ui->tls_box->hide();
    }

    if (ent->outbound->HasQUIC()) {
        auto quicObj = ent->outbound->GetQUIC();
        ui->quic_idle_timeout->setText(quicObj->idle_timeout);
        ui->quic_keep_alive_period->setText(quicObj->keep_alive_period);
        ui->quic_stream_receive_window->setText(quicObj->stream_receive_window);
        ui->quic_connection_receive_window->setText(quicObj->connection_receive_window);
        ui->quic_max_concurrent_streams->setText(EditorNumText(quicObj->max_concurrent_streams));
        ui->quic_initial_packet_size->setText(EditorNumText(quicObj->initial_packet_size));
        ui->quic_disable_path_mtu_discovery->setCurrentIndex(quicObj->getPathMtuState());
    } else {
        ui->quic_box->hide();
    }

    if (auto fields = GetInterfaceFields(); fields.system != nullptr) {
        ui->udp_mapping->addItems({"", "endpoint_independent", "address_dependent", "address_and_port_dependent"});
        ui->udp_filtering->addItems({"", "endpoint_independent", "address_dependent", "address_and_port_dependent"});
        ui->system->setChecked(*fields.system);
        ui->interface_name->setText(*fields.interface_name);
        ui->udp_timeout->setText(*fields.udp_timeout);
        ui->udp_mapping->setCurrentText(*fields.udp_mapping);
        ui->udp_filtering->setCurrentText(*fields.udp_filtering);
        ui->udp_nat_max->setText(EditorNumText(*fields.udp_nat_max));
    } else {
        ui->interface_box->hide();
    }

    ADD_ASTERISK(this)

    // adjustSize() clamps to 2/3 of the screen.
    const auto *scr = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (scr != nullptr) resize(sizeHint().boundedTo(scr->availableGeometry().size()));
}

EditAdvanced::~EditAdvanced()
{
    delete ui;
}

void EditAdvanced::accept() {
    auto dialFieldsObj = ent->outbound->dialFields;
    dialFieldsObj->reuse_addr = ui->reuse_addr->isChecked();
    dialFieldsObj->tcp_fast_open = ui->tcp_fast_open->isChecked();
    dialFieldsObj->udp_fragment = ui->udp_fragment->isChecked();
    dialFieldsObj->tcp_multi_path = ui->tcp_multipath->isChecked();
    dialFieldsObj->connect_timeout = ui->connect_timeout->text().trimmed();
    dialFieldsObj->bind_interface = ui->bind_interface->currentText().trimmed();
    dialFieldsObj->inet4_bind_address = ui->inet4_bind_address->currentText().trimmed();
    dialFieldsObj->inet6_bind_address = ui->inet6_bind_address->currentText().trimmed();

    auto updateHistory = [](QStringList& history, const QStringList& systemItems, const QString& value) {
        if (value.isEmpty() || systemItems.contains(value)) return;
        history.removeAll(value);
        history.prepend(value);
        if (history.size() > 5) history = history.mid(0, 5);
    };

    auto* repo = Configs::dataManager->settingsRepo.get();
    updateHistory(repo->dial_bind_interface_history,    m_systemInterfaces,    dialFieldsObj->bind_interface);
    updateHistory(repo->dial_inet4_bind_address_history, m_systemIpv4Addresses, dialFieldsObj->inet4_bind_address);
    updateHistory(repo->dial_inet6_bind_address_history, m_systemIpv6Addresses, dialFieldsObj->inet6_bind_address);
    repo->Save();

    if (ent->outbound->HasTLS()) {
        auto tlsObj = ent->outbound->GetTLS();
        tlsObj->disable_sni = ui->disable_sni->isChecked();
        tlsObj->min_version = ui->min_version->text().trimmed();
        tlsObj->max_version = ui->max_version->text().trimmed();
        tlsObj->saveSpoofState(ui->tls_spoof_state->currentIndex());
        tlsObj->spoof = ui->tls_spoof->text().trimmed();
        tlsObj->spoof_method = ui->tls_spoof_method->currentText().trimmed();
        tlsObj->ech->enabled = ui->enable_ech->isChecked();
        tlsObj->ech->serverName = ui->ech_server_name->text().trimmed();
        tlsObj->ech->config = CACHE.echConfig;
        tlsObj->client_certificate = CACHE.clientCert;
        tlsObj->client_key = CACHE.clientKey;
        tlsObj->certificate_public_key_sha256 = CACHE.certSha256;
    }

    if (ent->outbound->HasQUIC()) {
        auto quicObj = ent->outbound->GetQUIC();
        quicObj->idle_timeout = ui->quic_idle_timeout->text().trimmed();
        quicObj->keep_alive_period = ui->quic_keep_alive_period->text().trimmed();
        quicObj->stream_receive_window = ui->quic_stream_receive_window->text().trimmed();
        quicObj->connection_receive_window = ui->quic_connection_receive_window->text().trimmed();
        quicObj->max_concurrent_streams = ui->quic_max_concurrent_streams->text().trimmed().toInt();
        quicObj->initial_packet_size = ui->quic_initial_packet_size->text().trimmed().toInt();
        quicObj->savePathMtuState(ui->quic_disable_path_mtu_discovery->currentIndex());
    }

    if (auto fields = GetInterfaceFields(); fields.system != nullptr) {
        *fields.system = ui->system->isChecked();
        *fields.interface_name = ui->interface_name->text().trimmed();
        *fields.udp_timeout = ui->udp_timeout->text().trimmed();
        *fields.udp_mapping = ui->udp_mapping->currentText().trimmed();
        *fields.udp_filtering = ui->udp_filtering->currentText().trimmed();
        *fields.udp_nat_max = ui->udp_nat_max->text().trimmed().toInt();
    }
    QDialog::accept();
}

void EditAdvanced::on_ech_config_clicked() {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, tr("ECH Config"), "", CACHE.echConfig.join("\n"), &ok);
    if (ok) {
        CACHE.echConfig = txt.split("\n", Qt::SkipEmptyParts);
        if (!CACHE.echConfig.isEmpty()) {
            ui->ech_config->setText("Already set");
        } else {
            ui->ech_config->setText("Not Set");
        }
    }
}

void EditAdvanced::on_client_cert_clicked() {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, tr("Client Certificate"), "", CACHE.clientCert.join("\n"), &ok);
    if (ok) {
        CACHE.clientCert = txt.split("\n", Qt::SkipEmptyParts);
        if (!CACHE.echConfig.isEmpty()) {
            ui->client_cert->setText("Already set");
        } else {
            ui->client_cert->setText("Not Set");
        }
    }
}

void EditAdvanced::on_client_key_clicked() {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, tr("Client Key"), "", CACHE.clientKey.join("\n"), &ok);
    if (ok) {
        CACHE.clientKey = txt.split("\n", Qt::SkipEmptyParts);
        if (!CACHE.echConfig.isEmpty()) {
            ui->client_key->setText("Already set");
        } else {
            ui->client_key->setText("Not Set");
        }
    }
}

void EditAdvanced::on_cert_sha256_clicked() {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, tr("Certificate sha256"), "", CACHE.certSha256.join("\n"), &ok);
    if (ok) {
        CACHE.certSha256 = txt.split("\n", Qt::SkipEmptyParts);
        if (!CACHE.echConfig.isEmpty()) {
            ui->cert_sha256->setText("Already set");
        } else {
            ui->cert_sha256->setText("Not Set");
        }
    }
}
