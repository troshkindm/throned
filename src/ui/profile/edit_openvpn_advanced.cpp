#include "include/ui/profile/edit_openvpn_advanced.h"

#include <QComboBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLayout>
#include <QScreen>
#include <QScrollBar>
#include <QTableWidgetItem>

#include "include/global/GuiUtils.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/profile/editor_table_utils.h"

namespace {
    const QStringList kNetworks = {"", "udp", "udp4", "udp6", "tcp", "tcp4", "tcp6"};
    const QStringList kPullFilterActions = {"accept", "ignore", "reject"};
}

EditOpenVPNAdvanced::EditOpenVPNAdvanced(QWidget *parent, const std::shared_ptr<Configs::Profile> &_ent)
    : QDialog(parent)
    , ui(new Ui::EditOpenVPNAdvanced)
{
    ui->setupUi(this);
    ent = _ent;
    auto outbound = ent->OpenVPN();

    ui->mode->addItems({"", "tls", "static_key"});
    ui->topology->addItems({"", "net30", "p2p", "subnet"});
    ui->auth_retry->addItems({"", "none", "nointeract", "interact"});
    ui->key_direction->addItems({"", "server", "client"});
    ui->tls_server_name_type->addItems({"", "subject", "name", "name-prefix"});
    ui->tls_remote_certificate_tls->addItems({"", "server", "client", "none"});
    ui->tls_certificate_profile->addItems({"", "legacy", "preferred", "insecure", "suiteb"});
    ui->tls_ns_certificate_type->addItems({"", "server", "client"});
    ui->tls_version_min->addItems({"", "1.0", "1.1", "1.2", "1.3"});
    ui->tls_version_max->addItems({"", "1.0", "1.1", "1.2", "1.3"});
    ui->mss_fix_mode->addItems({"", "mtu", "fixed"});
    ui->compression->addItems({"", "none", "no", "lz4", "lz4-v2", "stub", "stub-v2", "disabled", "off"});
    ui->compression_lzo->addItems({"", "none", "no", "yes", "adaptive", "asym", "disabled", "off"});
    ui->allow_compression->addItems({"", "no", "asym", "yes"});

    ui->servers_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->pull_filters_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->servers_table->verticalHeader()->setVisible(false);
    ui->pull_filters_table->verticalHeader()->setVisible(false);

    ui->mode->setCurrentText(outbound->mode);
    ui->topology->setCurrentText(outbound->topology);
    ui->auth_retry->setCurrentText(outbound->auth_retry);
    ui->remote_random->setChecked(outbound->remote_random);
    ui->address->setText(outbound->address.join(","));
    ui->peer_address->setText(outbound->peer_address);
    ui->peer_address_ipv6->setText(outbound->peer_address_ipv6);

    for (const auto &server: outbound->servers) {
        addServerRow(server->server, EditorNumText(server->server_port), server->network);
    }

    ui->static_key_path->setText(outbound->static_key_path);
    ui->key_direction->setCurrentText(outbound->key_direction);
    ui->cipher->setText(outbound->cipher);
    CACHE.staticKey = outbound->static_key;

    auto tls = outbound->tls;
    ui->tls_server_name->setText(tls->server_name);
    ui->tls_server_name_type->setCurrentText(tls->server_name_type);
    ui->tls_certificate_path->setText(tls->certificate_path);
    ui->tls_client_certificate_path->setText(tls->client_certificate_path);
    ui->tls_client_key_path->setText(tls->client_key_path);
    ui->control_wrap_key_path->setText(tls->control_wrap->key_path);
    ui->tls_crl_path->setText(tls->crl_path);
    ui->tls_remote_certificate_ku->setText(tls->remote_certificate_ku.join(","));
    ui->tls_remote_certificate_eku->setText(tls->remote_certificate_eku);
    ui->tls_remote_certificate_tls->setCurrentText(tls->remote_certificate_tls);
    ui->tls_certificate_profile->setCurrentText(tls->certificate_profile);
    ui->tls_ns_certificate_type->setCurrentText(tls->ns_certificate_type);
    ui->tls_version_min->setCurrentText(tls->version_min);
    ui->tls_version_max->setCurrentText(tls->version_max);
    ui->tls_cipher->setText(tls->cipher);
    ui->tls_groups->setText(tls->groups);
    CACHE.peerFingerprint = tls->peer_fingerprint;

    ui->data_ciphers->setText(outbound->data_ciphers.join(","));
    ui->data_ciphers_fallback->setText(outbound->data_ciphers_fallback);
    ui->auth->setText(outbound->auth);
    ui->mss_fix->setText(EditorNumText(outbound->mss_fix));
    ui->mss_fix_disabled->setChecked(outbound->mss_fix_disabled);
    ui->mss_fix_mode->setCurrentText(outbound->mss_fix_mode);
    ui->fragment->setText(EditorNumText(outbound->fragment));
    ui->replay_window->setText(EditorNumText(outbound->replay_window));
    ui->replay_window_time->setText(outbound->replay_window_time);

    ui->compression->setCurrentText(outbound->compression);
    ui->compression_lzo->setCurrentText(outbound->compression_lzo);
    ui->allow_compression->setCurrentText(outbound->allow_compression);

    ui->route_no_pull->setChecked(outbound->route_no_pull);
    ui->routes->setText(outbound->routes.join(","));
    ui->route_gateway->setText(outbound->route_gateway);
    ui->route_metric->setText(EditorNumText(outbound->route_metric));
    ui->redirect_gateway->setChecked(outbound->redirect_gateway);
    ui->redirect_gateway_flags->setText(outbound->redirect_gateway_flags.join(","));
    ui->redirect_private->setChecked(outbound->redirect_private);
    ui->block_ipv6->setChecked(outbound->block_ipv6);
    for (const auto &filter: outbound->pull_filters) {
        addPullFilterRow(filter->action, filter->text);
    }

    ui->ping_interval->setText(outbound->ping_interval);
    ui->ping_restart->setText(outbound->ping_restart);
    ui->ping_restart_disabled->setChecked(outbound->ping_restart_disabled);
    ui->renegotiate_interval->setText(outbound->renegotiate_interval);
    ui->renegotiate_disabled->setChecked(outbound->renegotiate_disabled);
    ui->renegotiate_bytes->setText(EditorNumText(outbound->renegotiate_bytes));
    ui->renegotiate_packets->setText(EditorNumText(outbound->renegotiate_packets));
    ui->tls_timeout->setText(outbound->tls_timeout);
    ui->handshake_window->setText(outbound->handshake_window);
    ui->explicit_exit_notify->setText(EditorNumText(outbound->explicit_exit_notify));

    ui->static_key->setText(CACHE.staticKey.isEmpty() ? tr("Not set") : tr("Already set"));
    ui->tls_peer_fingerprint->setText(CACHE.peerFingerprint.isEmpty() ? tr("Not set") : tr("Already set"));

    connect(ui->static_key, &QPushButton::clicked, this,
            [=, this] { editPem(ui->static_key, tr("Static Key"), CACHE.staticKey); });
    connect(ui->tls_peer_fingerprint, &QPushButton::clicked, this,
            [=, this] { editPem(ui->tls_peer_fingerprint, tr("Peer Fingerprint"), CACHE.peerFingerprint); });

    connect(ui->servers_table_add, &QPushButton::clicked, this, [=, this] { addServerRow("", "", ""); });
    connect(ui->servers_table_remove, &QPushButton::clicked, this, [=, this] {
        ui->servers_table->removeRow(ui->servers_table->currentRow());
    });
    connect(ui->pull_filters_table_add, &QPushButton::clicked, this, [=, this] { addPullFilterRow("", ""); });
    connect(ui->pull_filters_table_remove, &QPushButton::clicked, this, [=, this] {
        ui->pull_filters_table->removeRow(ui->pull_filters_table->currentRow());
    });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    ADD_ASTERISK(this)

    // QScrollArea::sizeHint() caps at 36 font heights, and the nested form layouts stay inert until activated.
    for (auto *nested: ui->scroll_content->findChildren<QLayout *>()) nested->activate();
    const auto margins = layout()->contentsMargins();
    const auto width = ui->scroll->widget()->sizeHint().width() + 2 * ui->scroll->frameWidth() +
                       ui->scroll->verticalScrollBar()->sizeHint().width() + margins.left() + margins.right();

    const auto *scr = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (scr != nullptr) resize(QSize(width, sizeHint().height()).boundedTo(scr->availableGeometry().size()));
}

EditOpenVPNAdvanced::~EditOpenVPNAdvanced()
{
    delete ui;
}

void EditOpenVPNAdvanced::editPem(QPushButton *button, const QString &title, QStringList &target) {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, title, "", target.join("\n"), &ok);
    if (!ok) return;
    target = txt.split("\n", Qt::SkipEmptyParts);
    button->setText(target.isEmpty() ? tr("Not set") : tr("Already set"));
}

void EditOpenVPNAdvanced::addServerRow(const QString &server, const QString &port, const QString &network) {
    auto row = ui->servers_table->rowCount();
    ui->servers_table->insertRow(row);
    ui->servers_table->setItem(row, 0, new QTableWidgetItem(server));
    ui->servers_table->setItem(row, 1, new QTableWidgetItem(port));
    EditorSetComboCell(ui->servers_table, row, 2, kNetworks, network);
}

void EditOpenVPNAdvanced::addPullFilterRow(const QString &action, const QString &text) {
    auto row = ui->pull_filters_table->rowCount();
    ui->pull_filters_table->insertRow(row);
    EditorSetComboCell(ui->pull_filters_table, row, 0, kPullFilterActions, action);
    ui->pull_filters_table->setItem(row, 1, new QTableWidgetItem(text));
}

void EditOpenVPNAdvanced::accept() {
    auto outbound = ent->OpenVPN();

    outbound->mode = ui->mode->currentText().trimmed();
    outbound->topology = ui->topology->currentText().trimmed();
    outbound->auth_retry = ui->auth_retry->currentText().trimmed();
    outbound->remote_random = ui->remote_random->isChecked();
    outbound->address = SplitAndTrim(ui->address->text(), ",", false);
    outbound->peer_address = ui->peer_address->text().trimmed();
    outbound->peer_address_ipv6 = ui->peer_address_ipv6->text().trimmed();

    outbound->servers = {};
    for (int row = 0; row < ui->servers_table->rowCount(); ++row) {
        auto server = EditorCellText(ui->servers_table, row, 0);
        if (server.isEmpty()) continue;
        auto remote = std::make_shared<Configs::OpenVPNRemote>();
        remote->server = server;
        remote->server_port = EditorCellText(ui->servers_table, row, 1).toInt();
        remote->network = EditorComboCellText(ui->servers_table, row, 2);
        outbound->servers += remote;
    }

    outbound->static_key = CACHE.staticKey;
    outbound->static_key_path = ui->static_key_path->text().trimmed();
    outbound->key_direction = ui->key_direction->currentText().trimmed();
    outbound->cipher = ui->cipher->text().trimmed();

    auto tls = outbound->tls;
    tls->server_name = ui->tls_server_name->text().trimmed();
    tls->server_name_type = ui->tls_server_name_type->currentText().trimmed();
    tls->certificate_path = ui->tls_certificate_path->text().trimmed();
    tls->client_certificate_path = ui->tls_client_certificate_path->text().trimmed();
    tls->client_key_path = ui->tls_client_key_path->text().trimmed();
    tls->control_wrap->key_path = ui->control_wrap_key_path->text().trimmed();
    tls->peer_fingerprint = CACHE.peerFingerprint;
    tls->crl_path = ui->tls_crl_path->text().trimmed();
    tls->remote_certificate_ku = SplitAndTrim(ui->tls_remote_certificate_ku->text(), ",", false);
    tls->remote_certificate_eku = ui->tls_remote_certificate_eku->text().trimmed();
    tls->remote_certificate_tls = ui->tls_remote_certificate_tls->currentText().trimmed();
    tls->certificate_profile = ui->tls_certificate_profile->currentText().trimmed();
    tls->ns_certificate_type = ui->tls_ns_certificate_type->currentText().trimmed();
    tls->version_min = ui->tls_version_min->currentText().trimmed();
    tls->version_max = ui->tls_version_max->currentText().trimmed();
    tls->cipher = ui->tls_cipher->text().trimmed();
    tls->groups = ui->tls_groups->text().trimmed();

    outbound->data_ciphers = SplitAndTrim(ui->data_ciphers->text(), ",", false);
    outbound->data_ciphers_fallback = ui->data_ciphers_fallback->text().trimmed();
    outbound->auth = ui->auth->text().trimmed();
    outbound->mss_fix = ui->mss_fix->text().trimmed().toInt();
    outbound->mss_fix_disabled = ui->mss_fix_disabled->isChecked();
    outbound->mss_fix_mode = ui->mss_fix_mode->currentText().trimmed();
    outbound->fragment = ui->fragment->text().trimmed().toInt();
    outbound->replay_window = ui->replay_window->text().trimmed().toInt();
    outbound->replay_window_time = ui->replay_window_time->text().trimmed();

    outbound->compression = ui->compression->currentText().trimmed();
    outbound->compression_lzo = ui->compression_lzo->currentText().trimmed();
    outbound->allow_compression = ui->allow_compression->currentText().trimmed();

    outbound->route_no_pull = ui->route_no_pull->isChecked();
    outbound->routes = SplitAndTrim(ui->routes->text(), ",", false);
    outbound->route_gateway = ui->route_gateway->text().trimmed();
    outbound->route_metric = ui->route_metric->text().trimmed().toInt();
    outbound->redirect_gateway = ui->redirect_gateway->isChecked();
    outbound->redirect_gateway_flags = SplitAndTrim(ui->redirect_gateway_flags->text(), ",", false);
    outbound->redirect_private = ui->redirect_private->isChecked();
    outbound->block_ipv6 = ui->block_ipv6->isChecked();

    outbound->pull_filters = {};
    for (int row = 0; row < ui->pull_filters_table->rowCount(); ++row) {
        auto text = EditorCellText(ui->pull_filters_table, row, 1);
        if (text.isEmpty()) continue;
        auto filter = std::make_shared<Configs::OpenVPNPullFilter>();
        filter->action = EditorComboCellText(ui->pull_filters_table, row, 0);
        filter->text = text;
        outbound->pull_filters += filter;
    }

    outbound->ping_interval = ui->ping_interval->text().trimmed();
    outbound->ping_restart = ui->ping_restart->text().trimmed();
    outbound->ping_restart_disabled = ui->ping_restart_disabled->isChecked();
    outbound->renegotiate_interval = ui->renegotiate_interval->text().trimmed();
    outbound->renegotiate_disabled = ui->renegotiate_disabled->isChecked();
    outbound->renegotiate_bytes = ui->renegotiate_bytes->text().trimmed().toLongLong();
    outbound->renegotiate_packets = ui->renegotiate_packets->text().trimmed().toLongLong();
    outbound->tls_timeout = ui->tls_timeout->text().trimmed();
    outbound->handshake_window = ui->handshake_window->text().trimmed();
    outbound->explicit_exit_notify = ui->explicit_exit_notify->text().trimmed().toInt();

    QDialog::accept();
}
