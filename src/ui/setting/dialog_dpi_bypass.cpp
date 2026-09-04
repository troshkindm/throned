#include "include/ui/setting/dialog_dpi_bypass.h"

#include "include/configs/common/TLS.h"
#include "include/global/Configs.hpp"
#include "include/database/entities/RouteProfile.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/ui/mainwindow_interface.h"
#include "include/ui/setting/RouteProfileSimpleEditor.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowChrome.h"
#include "include/global/GuiUtils.hpp"

#include <QTabBar>

namespace {
    constexpr auto kRouteProfileName = "DPI Bypass";

    // Combo position -> stored key. Order matches the items added below.
    const QStringList kMethodKeys = {"spoof", "fragment", "record_fragment"};

    constexpr auto kRussiaRuleSet = "geosite-ru-blocked";
    constexpr auto kRussiaSpoofSNI = "max.ru";
}

DialogDpiBypass::DialogDpiBypass(QWidget *parent) : QDialog(parent), ui(new Ui::DialogDpiBypass) {
    ui->setupUi(this);

    // Adopts the redesigned dialog styling, which is scoped to this object name.
    setObjectName(QStringLiteral("routeProfileEditor"));
    themeManager()->RegisterStyle(this, RouteProfileSimpleEditor::dialogStyleSheet());

    setMinimumSize(680, 560);
    FitWindowToScreen(this, QSize(760, 620));

    // One page, so the tab strip is noise; the title bar carries the name and the close button.
    ui->tabWidget->tabBar()->hide();
    ui->buttonBox->hide();
    ui->tabWidget->setStyleSheet({});

    auto *root = ui->verticalLayout;
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    root->insertWidget(0, ThronedChrome::install(this, tr("DPI Bypass")));

    ui->tabWidget->setObjectName(QStringLiteral("routeBody"));
    ui->dpi_layout->setContentsMargins(16, 14, 16, 14);
    ui->dpi_layout->setSpacing(12);
    // generalBox is the card treatment the redesign uses for grouped fields.
    ui->dpi_technique_box->setObjectName(QStringLiteral("generalBox"));
    ui->dpi_lists_box->setObjectName(QStringLiteral("generalBox"));
    ui->dpi_form->setContentsMargins(14, 16, 14, 14);
    ui->dpi_form->setHorizontalSpacing(12);
    ui->dpi_form->setVerticalSpacing(10);
    ui->dpi_lists_layout->setContentsMargins(14, 16, 14, 14);
    ui->dpi_lists_layout->setSpacing(10);
    ui->dpi_apply_row->setSpacing(12);
    // Style hooks live on the object name; the ui-> pointers are unaffected.
    ui->dpi_apply->setObjectName(QStringLiteral("routeSaveButton"));
    ui->dpi_preset_ru->setObjectName(QStringLiteral("routeSecondaryButton"));
    ui->dpi_preset_clear->setObjectName(QStringLiteral("routeSecondaryButton"));
    ui->dpi_apply->setCursor(Qt::PointingHandCursor);
    ui->dpi_preset_ru->setCursor(Qt::PointingHandCursor);
    ui->dpi_preset_clear->setCursor(Qt::PointingHandCursor);

    ui->dpi_method->addItems({tr("Fake ClientHello (spoof)"), tr("TLS fragment"), tr("TLS record fragment")});
    // The empty entry means "inherit the global preset", which this preset must not offer.
    auto spoofMethods = Configs::tlsSpoofMethods;
    spoofMethods.removeAll(QString());
    ui->dpi_spoof_method->addItems(spoofMethods);

    loadDpiSettings();
    refreshDpiEnabledState();

    connect(ui->dpi_method, &QComboBox::currentIndexChanged, this, [this] { refreshDpiEnabledState(); });
    connect(ui->dpi_preset_ru, &QPushButton::clicked, this, [this] {
        ui->dpi_rule_sets->setPlainText(kRussiaRuleSet);
        ui->dpi_spoof_sni->setText(kRussiaSpoofSNI);
    });
    connect(ui->dpi_preset_clear, &QPushButton::clicked, this, [this] { ui->dpi_rule_sets->clear(); });
    connect(ui->dpi_apply, &QPushButton::clicked, this, [this] { applyDpiPreset(); });
}

DialogDpiBypass::~DialogDpiBypass() {
    delete ui;
}

void DialogDpiBypass::loadDpiSettings() const {
    const auto &settings = Configs::dataManager->settingsRepo;
    const int index = kMethodKeys.indexOf(settings->dpi_bypass_method);
    ui->dpi_method->setCurrentIndex(index < 0 ? 0 : index);
    ui->dpi_spoof_sni->setText(settings->dpi_bypass_spoof_sni);
    ui->dpi_spoof_method->setCurrentText(settings->dpi_bypass_spoof_method);
    ui->dpi_block_quic->setChecked(settings->dpi_bypass_block_quic);
    ui->dpi_rule_sets->setPlainText(settings->dpi_bypass_rule_sets.join("\n"));
}

void DialogDpiBypass::saveDpiSettings() const {
    const auto &settings = Configs::dataManager->settingsRepo;
    settings->dpi_bypass_method = kMethodKeys.value(ui->dpi_method->currentIndex(), kMethodKeys.first());
    settings->dpi_bypass_spoof_sni = ui->dpi_spoof_sni->text().trimmed();
    settings->dpi_bypass_spoof_method = ui->dpi_spoof_method->currentText();
    settings->dpi_bypass_block_quic = ui->dpi_block_quic->isChecked();

    QStringList sets;
    for (const auto &line: ui->dpi_rule_sets->toPlainText().split('\n')) {
        if (const auto trimmed = line.trimmed(); !trimmed.isEmpty()) sets << trimmed;
    }
    settings->dpi_bypass_rule_sets = sets;
}

void DialogDpiBypass::refreshDpiEnabledState() const {
    const bool spoofing = ui->dpi_method->currentIndex() == kMethodKeys.indexOf("spoof");
    ui->dpi_spoof_sni->setEnabled(spoofing);
    ui->dpi_spoof_sni_l->setEnabled(spoofing);
    ui->dpi_spoof_method->setEnabled(spoofing);
    ui->dpi_spoof_method_l->setEnabled(spoofing);
}

void DialogDpiBypass::applyDpiPreset() {
    saveDpiSettings();
    const auto &settings = Configs::dataManager->settingsRepo;

    if (settings->dpi_bypass_rule_sets.isEmpty()) {
        ui->dpi_status->setText(tr("Add at least one domain list first."));
        return;
    }
    if (settings->dpi_bypass_method == "spoof" && settings->dpi_bypass_spoof_sni.isEmpty()) {
        ui->dpi_status->setText(tr("Spoofing needs a decoy SNI."));
        return;
    }

    std::shared_ptr<Configs::RouteProfile> target;
    for (const auto &profile: Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
        if (profile->name == kRouteProfileName) {
            target = profile;
            break;
        }
    }
    const bool isNew = target == nullptr;
    if (isNew) {
        target = std::make_shared<Configs::RouteProfile>();
        target->name = kRouteProfileName;
    }

    target->defaultOutboundID = Configs::directID;
    target->Rules.clear();

    auto dnsRule = std::make_shared<Configs::RouteRule>();
    dnsRule->name = "Route DNS";
    dnsRule->action = "hijack-dns";
    dnsRule->protocol = "dns";
    target->Rules << dnsRule;

    if (settings->dpi_bypass_block_quic) {
        auto quicRule = std::make_shared<Configs::RouteRule>();
        quicRule->name = "Block QUIC";
        quicRule->action = "reject";
        quicRule->network = "udp";
        quicRule->port << "443";
        target->Rules << quicRule;
    }

    // route-options only tags the connection and keeps matching, so the listed
    // domains still fall through to the profile's direct default.
    auto bypassRule = std::make_shared<Configs::RouteRule>();
    bypassRule->name = "DPI bypass";
    bypassRule->action = "route-options";
    bypassRule->rule_set = settings->dpi_bypass_rule_sets;
    if (settings->dpi_bypass_method == "spoof") {
        bypassRule->tls_spoof = settings->dpi_bypass_spoof_sni;
        bypassRule->tls_spoof_method = settings->dpi_bypass_spoof_method;
    } else if (settings->dpi_bypass_method == "fragment") {
        bypassRule->tls_fragment = true;
    } else {
        bypassRule->tls_record_fragment = true;
    }
    target->Rules << bypassRule;

    if (isNew) {
        if (!Configs::dataManager->routesRepo->AddRouteProfile(target)) {
            ui->dpi_status->setText(tr("Could not save the routing profile."));
            return;
        }
    } else {
        Configs::dataManager->routesRepo->UpdateRouteProfiles({target});
    }
    settings->current_route_id = target->id;

    // Without a direct profile there is nothing to start, so the bypass would have no carrier.
    QString extra;
    if (Configs::dataManager->profilesRepo->GetProfileIdsByType("direct").isEmpty()) {
        auto directProfile = Configs::ProfilesRepo::NewProfile("direct");
        directProfile->outbound->name = tr("Direct (no proxy)");
        if (Configs::dataManager->profilesRepo->AddProfile(directProfile)) {
            extra = " " + tr("Added a \"%1\" profile to the current group.").arg(directProfile->outbound->name);
        }
    }

    Configs::dataManager->settingsRepo->Save();
    // Picking the profile is not enough: the status bar still names the old one and
    // a running core keeps the old routes until it is rebuilt.
    if (auto *mainWindow = GetMainWindow()) {
        mainWindow->refreshRoutingStatus();
        if (settings->started_id >= 0) mainWindow->profile_start(settings->started_id);
    }
    ui->dpi_status->setText(
        (isNew ? tr("Created routing profile \"%1\" and selected it.") : tr("Updated routing profile \"%1\" and selected it."))
        .arg(kRouteProfileName) + extra + " " + tr("Turn on Tun Mode, then start a direct profile."));
}
