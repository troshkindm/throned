#include "include/ui/setting/dialog_basic_settings.h"

#include "include/ui/widget/json/JsonEditorDialog.h"
#include "include/ui/widget/json/SchemaStore.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/setting/RouteProfileSimpleEditor.h"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowChrome.h"
#include "include/ui/widget/ThronedToggle.h"
#include "include/global/GuiUtils.hpp"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/DeviceDetailsHelper.hpp"

#include <QStyleFactory>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QBrush>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <qfontdatabase.h>
#include <QDateTime>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QCheckBox>
#include <QScreen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>

#include "include/ui/mainwindow.h"

DialogBasicSettings::DialogBasicSettings(QWidget *parent)
    : QDialog(parent), ui(new Ui::DialogBasicSettings) {
    ui->setupUi(this);
    ADD_ASTERISK(this);

    ui->inbound_socks_port_l->setText(ui->inbound_socks_port_l->text().replace("Socks", "Mixed (SOCKS+HTTP)"));
    ui->log_level->addItems(Configs::SingBox::LogLevels);
    ui->xray_loglevel->addItems(Configs::Xray::XrayLogLevels);
    ui->disable_stats->setChecked(Configs::dataManager->settingsRepo->disable_traffic_stats);
    ui->proxy_scheme->setCurrentText(Configs::dataManager->settingsRepo->proxy_scheme);

    D_LOAD_STRING(inbound_address)
    CACHE.custom_inbound = Configs::dataManager->settingsRepo->custom_inbound;
    D_LOAD_INT(inbound_socks_port)
    ui->random_listen_port->setChecked(Configs::dataManager->settingsRepo->random_inbound_port);
    D_LOAD_INT(test_concurrent)
    D_LOAD_STRING(test_latency_url)
    D_LOAD_BOOL(disable_tray)
    ui->reset_proxy_on_disable_sp->setChecked(Configs::dataManager->settingsRepo->reset_proxy_on_disable_sp);
    ui->url_timeout->setText(Int2String(Configs::dataManager->settingsRepo->url_test_timeout_ms));
    ui->udp_test_target->setText(Configs::dataManager->settingsRepo->udp_test_target);
    ui->speedtest_mode->setCurrentIndex(Configs::dataManager->settingsRepo->speed_test_mode);
    ui->test_timeout->setText(Int2String(Configs::dataManager->settingsRepo->speed_test_timeout_ms));
    ui->simple_down_url->setText(Configs::dataManager->settingsRepo->simple_dl_url);
    ui->allow_beta->setChecked(Configs::dataManager->settingsRepo->allow_beta_update);
    ui->disable_mixed_inbound->setChecked(Configs::dataManager->settingsRepo->disable_mixed_inbound);
    D_LOAD_BOOL(inbound_auth)
    D_LOAD_STRING(inbound_user)
    D_LOAD_STRING(inbound_pass)

    connect(ui->custom_inbound_edit, &QPushButton::clicked, this, [=,this] {
        C_EDIT_JSON_ALLOW_EMPTY(custom_inbound, JsonEdit::SingBox::Config)
    });
    connect(ui->disable_tray, &QCheckBox::stateChanged, this, [=,this](const bool &) {
        CACHE.updateDisableTray = true;
    });
    connect(ui->random_listen_port, &QCheckBox::stateChanged, this, [=,this](const bool &state)
    {
        if (state)
        {
            ui->inbound_socks_port->setDisabled(true);
        } else
        {
            ui->inbound_socks_port->setDisabled(false);
        }
    });

#ifndef Q_OS_WIN
    ui->proxy_scheme_l->hide();
    ui->proxy_scheme->hide();
#endif

    ui->max_log_line->setText(QString::number(Configs::dataManager->settingsRepo->max_log_line));
    D_LOAD_BOOL(log_auto_scroll)
    // A value this list does not carry would silently leave the box on its first
    // item, and saving would then write that back over the real setting.
    ui->log_level->setCurrentText(Configs::SingBox::NormalizeLogLevel(Configs::dataManager->settingsRepo->log_level));
    ui->xray_loglevel->setCurrentText(Configs::dataManager->settingsRepo->xray_log_level);
    ui->enable_log_include->setChecked(Configs::dataManager->settingsRepo->log_enable_include);
    ui->enable_log_exclude->setChecked(Configs::dataManager->settingsRepo->log_enable_exclude);
    ui->log_include_keyword->setText(Configs::dataManager->settingsRepo->log_include_keyword.join("\n"));
    ui->log_exclude_keyword->setText(Configs::dataManager->settingsRepo->log_exclude_keyword.join("\n"));
    ui->log_include_regex->setText(Configs::dataManager->settingsRepo->log_include_regex.join("\n"));
    ui->log_exclude_regex->setText(Configs::dataManager->settingsRepo->log_exclude_regex.join("\n"));
    applyRegexHighlighting();

    connect(ui->log_include_regex, &QTextEdit::textChanged, this, [this] { applyRegexHighlighting(); });
    connect(ui->log_exclude_regex, &QTextEdit::textChanged, this, [this] { applyRegexHighlighting(); });

    ui->connection_statistics->setChecked(Configs::dataManager->settingsRepo->enable_stats);
    ui->disable_traffic_aggregation->setChecked(Configs::dataManager->settingsRepo->disable_traffic_aggregation);
    ui->show_sys_dns->setChecked(Configs::dataManager->settingsRepo->show_system_dns);
    connect(ui->show_sys_dns, &QCheckBox::stateChanged, this, [=]
    {
        CACHE.updateSystemDns = true;
    });
#ifndef Q_OS_WIN
    ui->show_sys_dns->hide();
#endif
    D_LOAD_BOOL(start_minimal)
    ui->skip_delete_confirm->setChecked(Configs::dataManager->settingsRepo->skip_delete_confirmation);
    D_LOAD_BOOL(show_config_security)
    ui->language->setCurrentIndex(Configs::dataManager->settingsRepo->language);
    connect(ui->language, &QComboBox::currentIndexChanged, this, [=,this](int index) {
        CACHE.needRestart = true;
    });
    connect(ui->font, &QComboBox::currentTextChanged, this, [=,this](const QString &fontName) {
        auto font = qApp->font();
        font.setFamily(fontName);
        qApp->setFont(font);
        Configs::dataManager->settingsRepo->font = fontName;
        Configs::dataManager->settingsRepo->Save();
        themeManager->RefreshRegisteredStyles();
        updateGeometry();
    });
    for (int i=7;i<=26;i++) {
        ui->font_size->addItem(Int2String(i));
    }
    ui->font_size->setCurrentText(Int2String(qApp->font().pointSize()));
    connect(ui->font_size, &QComboBox::currentTextChanged, this, [=,this](const QString &sizeStr) {
        auto font = qApp->font();
        font.setPointSize(sizeStr.toInt());
        qApp->setFont(font);
        Configs::dataManager->settingsRepo->font_size = sizeStr.toInt();
        Configs::dataManager->settingsRepo->Save();
        themeManager->RefreshRegisteredStyles();
        updateGeometry();
    });
    //
    ui->theme->setIconSize(QSize(64, 22));
    ui->theme->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (const QString &theme : themeManager->ThronedThemes())
        ui->theme->addItem(themeManager->PreviewIcon(theme), theme);
    ui->enable_custom_icon->setChecked(Configs::dataManager->settingsRepo->use_custom_icons);
    connect(ui->select_custom_icon, &QPushButton::clicked, this, [=, this] {
        auto n = QMessageBox::information(this, "Custom Icon Manual", tr(Configs::Information::CustomIconManual.toStdString().c_str()), QMessageBox::Open | QMessageBox::Cancel);
        if (n == QMessageBox::Open) {
            auto fileNames = QFileDialog::getOpenFileNames(this,
                tr("Select png icons"), QDir::homePath(), tr("Image Files (*.png)"));
            QString errors;
            for (const auto& fileName : fileNames) {
                CACHE.updateTrayIcon = true;
                QFileInfo fileInfo(fileName);
                if (auto pixMap = QPixmap(fileName); pixMap.isNull()) errors += "Failed to load " + fileName + "\n";
                else if (pixMap.width() != pixMap.height()) errors += "Image does not have equal width and height: " + fileName + "\n";
                else if (!Configs::Information::iconNames.contains(fileInfo.fileName())) errors += "Icon name is not valid: " + fileInfo.fileName() + "\n";
                else {
                    QFile::remove(QDir("icons").filePath(fileInfo.fileName()));
                    if (!QFile::copy(fileName, QDir("icons").filePath(fileInfo.fileName()))) errors += "Failed to copy " + fileName + "\n";
                }
            }
            if (!errors.isEmpty()) {
                QMessageBox::warning(this, "Select custom image error", errors);
            }
        }
    });
    //
    QString selectedTheme = Configs::dataManager->settingsRepo->theme;
    if (ui->theme->findText(selectedTheme) < 0)
        selectedTheme = themeManager->IsDarkTheme(selectedTheme) ? QStringLiteral("Throned Midnight")
                                                                 : QStringLiteral("System");
    ui->theme->setCurrentText(selectedTheme);
    //
    // Applying a theme restyles the whole application: qApp->setStyle, setPalette and
    // setStyleSheet each walk every widget. The combo hands out one change per wheel
    // notch and per arrow key, so a burst has to collapse into a single apply or the
    // dialog freezes for as long as the user keeps scrolling.
    themeApplyDebounce_ = new QTimer(this);
    themeApplyDebounce_->setSingleShot(true);
    themeApplyDebounce_->setInterval(250);
    connect(themeApplyDebounce_, &QTimer::timeout, this, &DialogBasicSettings::applySelectedTheme);
    connect(ui->theme, &QComboBox::currentTextChanged, this, [this](const QString &) {
        themeApplyDebounce_->start();
    });

    ui->user_agent->setText(Configs::dataManager->settingsRepo->user_agent);
    ui->user_agent->setPlaceholderText(Configs::dataManager->settingsRepo->GetUserAgent(true));
    D_LOAD_BOOL(net_use_proxy)
    D_LOAD_BOOL(allow_stopping_active_profile)
    D_LOAD_BOOL(sub_clear)
    D_LOAD_BOOL(sub_show_change_popup)
    D_LOAD_BOOL(net_insecure)
    D_LOAD_BOOL(sub_send_hwid)
    D_LOAD_STRING(sub_custom_hwid_params)
    D_LOAD_INT_ENABLE(sub_auto_update, sub_auto_update_enable)
    D_LOAD_INT_ENABLE(route_auto_update, route_auto_update_enable)
    D_LOAD_INT_ENABLE(app_auto_update, app_auto_update_enable)
    auto details = GetDeviceDetails();
	ui->sub_send_hwid->setToolTip(
        ui->sub_send_hwid->toolTip()
            .arg(details.hwid.isEmpty() ? "N/A" : details.hwid,
                details.os.isEmpty() ? "N/A" : details.os,
                details.osVersion.isEmpty() ? "N/A" : details.osVersion,
                details.model.isEmpty() ? "N/A" : details.model));

    ui->dns_in_port->setValidator(new QIntValidator(1, 65535, ui->dns_in_port));
    ui->dns_in_port->setText(Int2String(Configs::dataManager->settingsRepo->core_dns_in_port));

    ui->core_box_clash_listen_addr->setText(Configs::dataManager->settingsRepo->core_box_clash_listen_addr);
    ui->core_box_clash_api->setValidator(new QIntValidator(1, 65535, ui->core_box_clash_api));
    ui->core_box_clash_api->setText(Configs::dataManager->settingsRepo->core_box_clash_api > 0
                                        ? Int2String(Configs::dataManager->settingsRepo->core_box_clash_api)
                                        : "");
    ui->core_box_clash_api_secret->setText(Configs::dataManager->settingsRepo->core_box_clash_api_secret);

    ui->core_box_api_port->setValidator(new QIntValidator(1, 65535, ui->core_box_api_port));
    ui->core_box_api_port->setText(Configs::dataManager->settingsRepo->core_box_api_port > 0
                                       ? Int2String(Configs::dataManager->settingsRepo->core_box_api_port)
                                       : "");
    ui->core_box_api_secret->setText(Configs::dataManager->settingsRepo->core_box_api_secret);
    connect(ui->core_box_api_regen, &QPushButton::clicked, this, [this] {
        ui->core_box_api_secret->setText(QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-'));
    });

    ui->vless_xray_pref->addItems(Configs::Xray::XrayVlessPreferenceString);
    ui->vless_xray_pref->setCurrentIndex(Configs::dataManager->settingsRepo->xray_vless_preference);
    D_LOAD_STRING(xray_geoip_url)
    D_LOAD_STRING(xray_geosite_url)
    ui->xray_geoip_url->setPlaceholderText("https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geoip.dat");
    ui->xray_geosite_url->setPlaceholderText("https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geosite.dat");

    ui->ntp_enable->setChecked(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_server->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_port->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_interval->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_outbound->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_server->setText(Configs::dataManager->settingsRepo->ntp_server_address);
    ui->ntp_port->setText(Int2String(Configs::dataManager->settingsRepo->ntp_server_port));
    ui->ntp_interval->setCurrentText(Configs::dataManager->settingsRepo->ntp_interval);
    ui->ntp_outbound->setCurrentText(Configs::dataManager->settingsRepo->ntp_outbound);
    connect(ui->ntp_enable, &QCheckBox::stateChanged, this, [=,this](const bool &state) {
        ui->ntp_server->setEnabled(state);
        ui->ntp_port->setEnabled(state);
        ui->ntp_interval->setEnabled(state);
        ui->ntp_outbound->setEnabled(state);
    });

    // Build the production settings window from the same structure as
    // tools/ui-demo/SettingsPreview.  The original controls are reparented, so
    // loading, validation and saving continue to use the existing code paths.
    setObjectName(QStringLiteral("basicSettingsDialog"));
    setWindowTitle(tr("Settings"));

    const auto retireLegacyPage = [this](QWidget *page) {
        if (page == nullptr) return;
        page->hide();
        page->setParent(this);
    };

    const auto makeFieldRow = [this](const QString &title, const QString &hint, QWidget *control) {
        auto *row = new QFrame(this);
        row->setObjectName(QStringLiteral("settingsField"));
        auto *layout = new QGridLayout(row);
        layout->setContentsMargins(12, 9, 12, 9);
        layout->setHorizontalSpacing(18);
        layout->setVerticalSpacing(3);
        auto *label = new QLabel(title, row);
        label->setObjectName(QStringLiteral("settingsFieldTitle"));
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(label, 0, 0);
        if (!hint.isEmpty()) {
            auto *hintLabel = new QLabel(hint, row);
            hintLabel->setObjectName(QStringLiteral("settingsMuted"));
            hintLabel->setWordWrap(true);
            hintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            layout->addWidget(hintLabel, 1, 0);
        }
        control->setParent(row);
        if (!qobject_cast<QAbstractButton *>(control)) {
            control->setMinimumWidth(270);
            auto policy = control->sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Preferred);
            policy.setHorizontalStretch(0);
            control->setSizePolicy(policy);
        }
        layout->addWidget(control, 0, 1, hint.isEmpty() ? 1 : 2, 1);
        layout->setColumnStretch(0, 1);
        return row;
    };
    const auto makeToggle = [this](QCheckBox *source) {
        const bool sourceWasHidden = source->isHidden();
        auto *toggle = new ThronedToggle(source->isChecked(), this);
        toggle->bindTo(source);
        toggle->setToolTip(source->toolTip());
        toggle->setAccessibleName(source->text());
        toggle->setAccessibleDescription(source->toolTip());
        // Keep the original checkbox alive because accept() and existing signal
        // handlers still read it.  The legacy Designer page is deleted after
        // its controls are moved into the redesigned shell.
        source->setParent(toggle);
        source->hide();
        if (sourceWasHidden) toggle->hide();
        return toggle;
    };
    const auto makeSection = [this](const QString &title, const QString &subtitle) {
        auto *section = new QFrame(this);
        section->setObjectName(QStringLiteral("settingsSection"));
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *heading = new QFrame(section);
        heading->setObjectName(QStringLiteral("settingsHeading"));
        auto *headingLayout = new QVBoxLayout(heading);
        headingLayout->setContentsMargins(14, 11, 14, 9);
        headingLayout->setSpacing(2);
        auto *titleLabel = new QLabel(title, heading);
        titleLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
        headingLayout->addWidget(titleLabel);
        auto *subtitleLabel = new QLabel(subtitle, heading);
        subtitleLabel->setObjectName(QStringLiteral("settingsMuted"));
        headingLayout->addWidget(subtitleLabel);
        layout->addWidget(heading);
        return section;
    };

    // Save tab names/pages before replacing QTabWidget with a scrollable stack.
    QStringList pageNames;
    QList<QWidget *> legacyPages;
    for (int index = 0; index < ui->tabWidget->count(); ++index) {
        pageNames.append(ui->tabWidget->tabText(index));
        legacyPages.append(ui->tabWidget->widget(index));
    }
    while (ui->tabWidget->count() > 0) ui->tabWidget->removeTab(0);

    auto *commonPage = new QWidget(this);
    commonPage->setObjectName(QStringLiteral("settingsPage"));
    auto *commonLayout = new QVBoxLayout(commonPage);
    commonLayout->setContentsMargins(14, 8, 14, 8);
    commonLayout->setSpacing(10);
    auto *commonTitle = new QLabel(tr("Common settings"), commonPage);
    commonTitle->setObjectName(QStringLiteral("settingsHero"));
    commonLayout->addWidget(commonTitle);
    auto *commonSubtitle = new QLabel(tr("Everyday proxy, inbound, and test behavior."), commonPage);
    commonSubtitle->setObjectName(QStringLiteral("settingsMuted"));
    commonLayout->addWidget(commonSubtitle);

    auto *inboundSection = makeSection(tr("Inbound"), tr("Local proxy endpoint used by applications."));
    auto *inboundLayout = qobject_cast<QVBoxLayout *>(inboundSection->layout());
    inboundLayout->addWidget(makeFieldRow(tr("Listen address"), {}, ui->inbound_address));
    inboundLayout->addWidget(makeFieldRow(tr("Mixed port"), tr("SOCKS and HTTP on one local port"), ui->inbound_socks_port));
    inboundLayout->addWidget(makeFieldRow(tr("Random port"), tr("Choose a free port on every start"), makeToggle(ui->random_listen_port)));
    inboundLayout->addWidget(makeFieldRow(tr("Disable mixed inbound"), tr("Do not expose the local mixed proxy endpoint"), makeToggle(ui->disable_mixed_inbound)));
    inboundLayout->addWidget(makeFieldRow(tr("Authentication"), tr("Require credentials for local proxy clients"), makeToggle(ui->inbound_auth)));
    inboundLayout->addWidget(makeFieldRow(tr("Username"), {}, ui->inbound_user));
    inboundLayout->addWidget(makeFieldRow(tr("Password"), {}, ui->inbound_pass));
#ifdef Q_OS_WIN
    inboundLayout->addWidget(makeFieldRow(tr("System proxy scheme"), {}, ui->proxy_scheme));
#endif
    auto *customInboundRow = new QFrame(commonPage);
    customInboundRow->setObjectName(QStringLiteral("settingsField"));
    auto *customInboundLayout = new QHBoxLayout(customInboundRow);
    customInboundLayout->setContentsMargins(12, 9, 12, 9);
    auto *customInboundLabel = new QLabel(tr("Custom inbound JSON"), customInboundRow);
    customInboundLabel->setObjectName(QStringLiteral("settingsFieldTitle"));
    customInboundLayout->addWidget(customInboundLabel);
    customInboundLayout->addStretch(1);
    ui->custom_inbound_edit->setParent(customInboundRow);
    ui->custom_inbound_edit->setObjectName(QStringLiteral("settingsSecondaryButton"));
    customInboundLayout->addWidget(ui->custom_inbound_edit);
    inboundLayout->addWidget(customInboundRow);
    commonLayout->addWidget(inboundSection);

    auto *testingSection = makeSection(tr("Testing"), tr("Defaults used by latency, UDP and speed tests."));
    auto *testingLayout = qobject_cast<QVBoxLayout *>(testingSection->layout());
    testingLayout->addWidget(makeFieldRow(tr("Latency test URL"), {}, ui->test_latency_url));
    testingLayout->addWidget(makeFieldRow(tr("URL test timeout"), tr("Milliseconds before a latency test fails"), ui->url_timeout));
    testingLayout->addWidget(makeFieldRow(tr("UDP test target"),
                                          tr("host:port the UDP test queries; it has to answer DNS over UDP"),
                                          ui->udp_test_target));
    testingLayout->addWidget(makeFieldRow(tr("Concurrent tests"), {}, ui->test_concurrent));
    testingLayout->addWidget(makeFieldRow(tr("Speed test mode"), {}, ui->speedtest_mode));
    testingLayout->addWidget(makeFieldRow(tr("Speed test timeout"), {}, ui->test_timeout));
    testingLayout->addWidget(makeFieldRow(tr("Download test URL"), {}, ui->simple_down_url));
    commonLayout->addWidget(testingSection);
    commonLayout->addStretch(1);

    auto *oldRoot = ui->gridLayout;
    oldRoot->removeWidget(ui->tabWidget);
    oldRoot->removeWidget(ui->buttonBox);
    while (QLayoutItem *item = oldRoot->takeAt(0)) delete item;
    oldRoot->setContentsMargins(1, 1, 1, 1);
    oldRoot->setHorizontalSpacing(0);
    oldRoot->setVerticalSpacing(0);
    oldRoot->addWidget(ThronedChrome::install(this, tr("Settings")), 0, 0);

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("settingsBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 10, 12, 10);
    bodyLayout->setSpacing(10);
    auto *work = new QHBoxLayout;
    work->setSpacing(10);
    auto *sidebar = new QFrame(body);
    sidebar->setObjectName(QStringLiteral("settingsSidebar"));
    sidebar->setFixedWidth(220);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(10, 12, 10, 12);
    sidebarLayout->setSpacing(6);
    auto *sidebarTitle = new QLabel(tr("Settings"), sidebar);
    sidebarTitle->setObjectName(QStringLiteral("settingsSidebarTitle"));
    sidebarLayout->addWidget(sidebarTitle);

    auto *stack = new QStackedWidget(body);
    stack->setObjectName(QStringLiteral("settingsStack"));
    settingsStack_ = stack;
    auto *commonScroll = new QScrollArea(stack);
    commonScroll->setObjectName(QStringLiteral("settingsScroll"));
    commonScroll->setWidgetResizable(true);
    commonScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    commonScroll->setFrameShape(QFrame::NoFrame);
    commonScroll->setWidget(commonPage);
    stack->addWidget(commonScroll);

    if (!pageNames.isEmpty()) pageNames[0] = tr("Common");
    if (pageNames.size() > 2) pageNames[2] = tr("Appearance");
    if (pageNames.size() > 6) pageNames[6] = tr("Backups");

    const auto makeInlineControl = [this](QWidget *primary, QAbstractButton *secondary) {
        auto *host = new QWidget(this);
        host->setObjectName(QStringLiteral("settingsInlineControl"));
        auto *layout = new QHBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        primary->setParent(host);
        secondary->setParent(host);
        secondary->setObjectName(QStringLiteral("settingsSecondaryButton"));
        layout->addWidget(primary, 1);
        layout->addWidget(secondary);
        return host;
    };
    const auto prepareEditor = [](QWidget *editor) {
        editor->setMinimumHeight(92);
        editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return editor;
    };

    // Move every established control into the same SettingsPreview card system.
    // Keeping the actual Designer controls preserves all existing bindings and
    // save logic while avoiding the cramped legacy tab layouts.
    for (int index = 1; index < legacyPages.size(); ++index) {
        auto *pageScroll = new QScrollArea(stack);
        pageScroll->setObjectName(QStringLiteral("settingsScroll"));
        pageScroll->setWidgetResizable(true);
        pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pageScroll->setFrameShape(QFrame::NoFrame);
        auto *pageHost = new QWidget(pageScroll);
        pageHost->setObjectName(QStringLiteral("settingsPage"));
        auto *pageLayout = new QVBoxLayout(pageHost);
        pageLayout->setContentsMargins(14, 8, 14, 8);
        pageLayout->setSpacing(10);
        const QString pageTitle = index < pageNames.size() ? pageNames[index] : tr("Settings");
        auto *heading = new QLabel(pageTitle, pageHost);
        heading->setObjectName(QStringLiteral("settingsHero"));
        pageLayout->addWidget(heading);
        auto *subtitle = new QLabel(tr("Configure %1 settings.").arg(pageTitle.toLower()), pageHost);
        subtitle->setObjectName(QStringLiteral("settingsMuted"));
        pageLayout->addWidget(subtitle);

        const auto addToggleRow = [&](QVBoxLayout *layout, const QString &title, QCheckBox *source,
                                      const QString &hint = QString()) {
            const bool sourceWasHidden = source->isHidden();
            auto *row = makeFieldRow(title, hint, makeToggle(source));
            row->setVisible(!sourceWasHidden);
            layout->addWidget(row);
        };
        const auto addControlRow = [&](QVBoxLayout *layout, const QString &title, QWidget *control,
                                       const QString &hint = QString()) {
            layout->addWidget(makeFieldRow(title, hint, control));
        };

        switch (index) {
        case 1: { // Logging
            auto *output = makeSection(tr("Log output"), tr("Control verbosity and retained history."));
            auto *layout = qobject_cast<QVBoxLayout *>(output->layout());
            addControlRow(layout, tr("Max log lines"), ui->max_log_line);
            addToggleRow(layout, tr("Auto-scroll log"), ui->log_auto_scroll);
            addControlRow(layout, tr("Sing-box log level"), ui->log_level);
            addControlRow(layout, tr("Xray log level"), ui->xray_loglevel);
            pageLayout->addWidget(output);

            auto *include = makeSection(tr("Include filters"), tr("Keep only matching log entries when enabled."));
            layout = qobject_cast<QVBoxLayout *>(include->layout());
            addToggleRow(layout, tr("Enable include rules"), ui->enable_log_include);
            addControlRow(layout, tr("Keywords"), prepareEditor(ui->log_include_keyword));
            addControlRow(layout, tr("Regular expressions"), prepareEditor(ui->log_include_regex));
            pageLayout->addWidget(include);

            auto *exclude = makeSection(tr("Exclude filters"), tr("Hide matching log entries when enabled."));
            layout = qobject_cast<QVBoxLayout *>(exclude->layout());
            addToggleRow(layout, tr("Enable exclude rules"), ui->enable_log_exclude);
            addControlRow(layout, tr("Keywords"), prepareEditor(ui->log_exclude_keyword));
            addControlRow(layout, tr("Regular expressions"), prepareEditor(ui->log_exclude_regex));
            pageLayout->addWidget(exclude);
            break;
        }
        case 2: { // Appearance
            auto *visuals = makeSection(tr("Theme and language"), tr("Fonts, colors, language, and application icons."));
            auto *layout = qobject_cast<QVBoxLayout *>(visuals->layout());
            addControlRow(layout, tr("Color theme"), ui->theme, tr("Previewed immediately across every redesigned screen."));
            addControlRow(layout, tr("Language"), ui->language);
            addControlRow(layout, tr("Font"), ui->font);
            addControlRow(layout, tr("Font size"), ui->font_size, tr("Changes the base interface size without restarting."));
            addToggleRow(layout, tr("Use custom icons"), ui->enable_custom_icon);
            ui->select_custom_icon->setObjectName(QStringLiteral("settingsSecondaryButton"));
            addControlRow(layout, tr("Custom icon files"), ui->select_custom_icon,
                          tr("PNG files must use Throned's supported tray-icon names."));
            pageLayout->addWidget(visuals);

            auto *window = makeSection(tr("Window and statistics"), tr("Startup behavior and information shown in the main window."));
            layout = qobject_cast<QVBoxLayout *>(window->layout());
            addToggleRow(layout, tr("Connection statistics"), ui->connection_statistics);
            addToggleRow(layout, ui->disable_traffic_aggregation->text(), ui->disable_traffic_aggregation);
            addToggleRow(layout, ui->show_config_security->text(), ui->show_config_security);
            addToggleRow(layout, ui->start_minimal->text(), ui->start_minimal);
            addToggleRow(layout, ui->show_sys_dns->text(), ui->show_sys_dns);
            addToggleRow(layout, ui->disable_tray->text(), ui->disable_tray);
            addToggleRow(layout, tr("Delete profiles without confirmation"), ui->skip_delete_confirm);
            pageLayout->addWidget(window);
            break;
        }
        case 3: { // Subscription
            auto *updates = makeSection(tr("Automatic updates"), tr("Schedule subscription and routing-profile refreshes."));
            auto *layout = qobject_cast<QVBoxLayout *>(updates->layout());
            addControlRow(layout, tr("User agent"), ui->user_agent);
            addToggleRow(layout, tr("Update subscriptions automatically"), ui->sub_auto_update_enable);
            addControlRow(layout, tr("Subscription interval (minutes)"), ui->sub_auto_update);
            addToggleRow(layout, tr("Update routing profiles automatically"), ui->route_auto_update_enable);
            addControlRow(layout, tr("Routing interval (minutes)"), ui->route_auto_update);
            addToggleRow(layout, tr("Check for Throned updates automatically"), ui->app_auto_update_enable);
            addControlRow(layout, tr("Update check interval (minutes)"), ui->app_auto_update,
                          tr("A found release is announced in the tray; nothing installs on its own."));
            pageLayout->addWidget(updates);

            auto *behavior = makeSection(tr("Update behavior"), tr("Choose how imported profiles and device metadata are handled."));
            layout = qobject_cast<QVBoxLayout *>(behavior->layout());
            addToggleRow(layout, ui->allow_stopping_active_profile->text(), ui->allow_stopping_active_profile);
            addToggleRow(layout, ui->sub_clear->text(), ui->sub_clear);
            addToggleRow(layout, ui->sub_show_change_popup->text(), ui->sub_show_change_popup);
            addToggleRow(layout, ui->sub_send_hwid->text(), ui->sub_send_hwid);
            addControlRow(layout, tr("Custom system parameters"), ui->sub_custom_hwid_params,
                          tr("Optional overrides for HWID, OS, version, or model."));
            pageLayout->addWidget(behavior);
            break;
        }
        case 4: { // Core
            // Keep the complete controller setup together. All four widgets come from
            // the retired Designer page, so every one of them must be re-hosted here.
            auto *singboxApi = makeSection(tr("sing-box API"), tr("Local controller endpoint used by the built-in dashboard."));
            auto *layout = qobject_cast<QVBoxLayout *>(singboxApi->layout());
            addControlRow(layout, tr("API port"), ui->core_box_api_port);
            addControlRow(layout, tr("API secret"), makeInlineControl(ui->core_box_api_secret, ui->core_box_api_regen));
            ui->core_box_api_hint->setParent(singboxApi);
            ui->core_box_api_hint->setObjectName(QStringLiteral("settingsMuted"));
            ui->core_box_api_hint->setWordWrap(true);
            layout->addWidget(ui->core_box_api_hint);
            pageLayout->addWidget(singboxApi);

            auto *clash = makeSection(tr("Clash API"), tr("Local controller endpoint exposed by the core."));
            layout = qobject_cast<QVBoxLayout *>(clash->layout());
            addControlRow(layout, tr("Listen address"), ui->core_box_clash_listen_addr);
            addControlRow(layout, tr("API port"), ui->core_box_clash_api);
            addControlRow(layout, tr("API secret"), ui->core_box_clash_api_secret);
            pageLayout->addWidget(clash);

            auto *xray = makeSection(tr("Xray core"), tr("Profile preference used by Xray-backed VLESS connections."));
            layout = qobject_cast<QVBoxLayout *>(xray->layout());
            addControlRow(layout, tr("VLESS preference"), ui->vless_xray_pref);
            pageLayout->addWidget(xray);
            break;
        }
        case 5: { // Miscellaneous
            auto *network = makeSection(tr("Network"), tr("Application updates and outbound network behavior."));
            auto *layout = qobject_cast<QVBoxLayout *>(network->layout());
            addToggleRow(layout, ui->allow_beta->text(), ui->allow_beta);
            addToggleRow(layout, ui->net_insecure->text(), ui->net_insecure);
            addToggleRow(layout, ui->reset_proxy_on_disable_sp->text(), ui->reset_proxy_on_disable_sp);
            addToggleRow(layout, ui->net_use_proxy->text(), ui->net_use_proxy);
            pageLayout->addWidget(network);

            auto *core = makeSection(tr("Core services"), tr("Traffic statistics and the local DNS endpoint."));
            layout = qobject_cast<QVBoxLayout *>(core->layout());
            addToggleRow(layout, ui->disable_stats->text(), ui->disable_stats);
            addControlRow(layout, tr("DNS inbound port"), ui->dns_in_port);
            pageLayout->addWidget(core);

            auto *ntp = makeSection(tr("NTP client"), tr("Time synchronization used by sing-box."));
            layout = qobject_cast<QVBoxLayout *>(ntp->layout());
            addToggleRow(layout, ui->ntp_enable->text(), ui->ntp_enable);
            addControlRow(layout, tr("Server"), ui->ntp_server);
            addControlRow(layout, tr("Port"), ui->ntp_port);
            addControlRow(layout, tr("Interval"), ui->ntp_interval);
            addControlRow(layout, tr("Outbound"), ui->ntp_outbound);
            pageLayout->addWidget(ntp);

            auto *assets = makeSection(tr("Xray geo assets"), tr("Sources used for geoip.dat and geosite.dat."));
            layout = qobject_cast<QVBoxLayout *>(assets->layout());
            addControlRow(layout, tr("GeoIP asset URL"), makeInlineControl(ui->xray_geoip_url, ui->xray_geoip_download));
            addControlRow(layout, tr("Geosite asset URL"), makeInlineControl(ui->xray_geosite_url, ui->xray_geosite_download));
            pageLayout->addWidget(assets);
            break;
        }
        case 6: { // Backup and restore
            auto *create = makeSection(tr("Create backup"), tr("Choose which local data to include."));
            auto *layout = qobject_cast<QVBoxLayout *>(create->layout());
            addToggleRow(layout, ui->backup_inc_profiles->text(), ui->backup_inc_profiles);
            addToggleRow(layout, ui->backup_inc_routes->text(), ui->backup_inc_routes);
            addToggleRow(layout, ui->backup_inc_settings->text(), ui->backup_inc_settings);
            addToggleRow(layout, ui->backup_inc_otp->text(), ui->backup_inc_otp);
            addToggleRow(layout, ui->backup_inc_icons->text(), ui->backup_inc_icons);
            ui->backup_create->setObjectName(QStringLiteral("settingsSecondaryButton"));
            addControlRow(layout, tr("Backup file"), ui->backup_create);
            pageLayout->addWidget(create);

            auto *restore = makeSection(tr("Restore backup"), tr("Import data from an existing Throned backup."));
            layout = qobject_cast<QVBoxLayout *>(restore->layout());
            ui->backup_restore->setObjectName(QStringLiteral("settingsSecondaryButton"));
            addControlRow(layout, tr("Backup file"), ui->backup_restore);
            pageLayout->addWidget(restore);
            break;
        }
        case 7: { // Security -- first page off the .ui; see SettingsBindings.
            auto &settings = *Configs::dataManager->settingsRepo;

            // Creates the control, binds it to its setting and puts it in the section.
            // One call is the whole description of a preference.
            const auto addToggle = [&](QVBoxLayout *section, const QString &text,
                                       bool &target, const QString &tip = {}) {
                auto *box = new QCheckBox(text, this);
                if (!tip.isEmpty()) box->setToolTip(tip);
                bindings_.bind(box, target);
                addToggleRow(section, box->text(), box);
                return box;
            };

            auto *permissions = makeSection(tr("Permissions"), tr("Administrative privileges and startup behavior."));
            auto *layout = qobject_cast<QVBoxLayout *>(permissions->layout());
            addToggle(layout, tr("Disable Privilege request"), settings.disable_privilege_req);
#ifdef Q_OS_WIN
            alwaysStandardUser_ = addToggle(layout, tr("Always Start as Standard User"), settings.disable_run_admin,
                                            tr("Do not attempt to start as Admin unless explicitly requested"));
#endif
            pageLayout->addWidget(permissions);

            auto *certificates = makeSection(tr("Certificates"), tr("Certificate stores and TLS validation defaults."));
            layout = qobject_cast<QVBoxLayout *>(certificates->layout());
            addToggle(layout, tr("Use Mozilla Certificate Store"), settings.use_mozilla_certs);
            addToggle(layout, tr("Skip TLS certificate authentication by default (allowInsecure)"), settings.skip_cert);

            auto *fingerprint = new QComboBox(this);
            fingerprint->setEditable(true);
            fingerprint->addItems(Configs::tlsFingerprints);
            bindings_.bind(fingerprint, settings.utlsFingerprint);
            addControlRow(layout, tr("uTLS fingerprint"), fingerprint);

            pageLayout->addWidget(certificates);
            break;
        }
        default:
            legacyPages[index]->setParent(pageHost);
            legacyPages[index]->setObjectName(QStringLiteral("settingsLegacyPage"));
            legacyPages[index]->setVisible(true);
            pageLayout->addWidget(legacyPages[index], 1);
            break;
        }
        pageLayout->addStretch(1);
        pageScroll->setWidget(pageHost);
        stack->addWidget(pageScroll);
        // Destroying the page also destroys every control that was not moved into the
        // new tree, while ui->name keeps pointing at the corpse -- that is what crashed
        // Save in 1.3.6. Keeping the husk alive but detached turns a missed move into an
        // invisible control instead of a use-after-free.
        if (index <= 7) retireLegacyPage(legacyPages[index]);
    }

    // Bound controls only exist once their page has been built, so the values go in
    // here rather than alongside the loads of the pages still coming from the .ui.
    bindings_.load();

    auto *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    settingsNavGroup_ = navGroup;
    const QList<MaterialIcon::Glyph> pageIcons{
        MaterialIcon::Glyph::Settings, MaterialIcon::Glyph::List, MaterialIcon::Glyph::Desktop,
        MaterialIcon::Glyph::Reload, MaterialIcon::Glyph::Tools, MaterialIcon::Glyph::Apps,
        MaterialIcon::Glyph::Folder, MaterialIcon::Glyph::Shield,
    };
    for (int index = 0; index < stack->count(); ++index) {
        const QString name = index < pageNames.size() ? pageNames[index] : tr("Settings");
        auto *button = new QPushButton(name, sidebar);
        button->setObjectName(QStringLiteral("settingsNav"));
        button->setCheckable(true);
        button->setIcon(MaterialIcon::icon(pageIcons.value(index, MaterialIcon::Glyph::Settings), QColor(QStringLiteral("#AEB7C2")), 17));
        button->setIconSize(QSize(17, 17));
        button->setCursor(Qt::PointingHandCursor);
        navGroup->addButton(button, index);
        sidebarLayout->addWidget(button);
    }
    navGroup->button(0)->setChecked(true);
    connect(navGroup, &QButtonGroup::idClicked, stack, &QStackedWidget::setCurrentIndex);
    sidebarLayout->addStretch(1);
    auto *version = new QLabel(tr("Throned settings"), sidebar);
    version->setObjectName(QStringLiteral("settingsMuted"));
    sidebarLayout->addWidget(version);
    work->addWidget(sidebar);
    work->addWidget(stack, 1);
    bodyLayout->addLayout(work, 1);

    ui->buttonBox->setParent(body);
    if (auto *save = ui->buttonBox->button(QDialogButtonBox::Ok)) {
        save->setText(tr("Save settings"));
        save->setObjectName(QStringLiteral("settingsPrimaryButton"));
        save->setMinimumWidth(150);
    }
    if (auto *cancel = ui->buttonBox->button(QDialogButtonBox::Cancel)) {
        cancel->setText(tr("Cancel"));
        cancel->setObjectName(QStringLiteral("settingsSecondaryButton"));
        cancel->setMinimumWidth(120);
    }
    bodyLayout->addWidget(ui->buttonBox, 0, Qt::AlignRight);
    oldRoot->addWidget(body, 1, 0);
    retireLegacyPage(legacyPages.value(0));
    delete ui->tabWidget;

    const QString settingsStyleTemplate = RouteProfileSimpleEditor::dialogStyleSheet() + QStringLiteral(R"(
QDialog#basicSettingsDialog QWidget#settingsBody,
QDialog#basicSettingsDialog QWidget#settingsPage,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage,
QDialog#basicSettingsDialog QScrollArea#settingsScroll > QWidget > QWidget { background: #1B1E23; }
QDialog#basicSettingsDialog QFrame#settingsSidebar,
QDialog#basicSettingsDialog QFrame#settingsSection {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QDialog#basicSettingsDialog QLabel#settingsSidebarTitle,
QDialog#basicSettingsDialog QLabel#settingsSectionTitle,
QDialog#basicSettingsDialog QLabel#settingsFieldTitle {
    color: #F1F3F5; font-weight: 700;
}
QDialog#basicSettingsDialog QLabel#settingsHero { color: #F1F3F5; font-size: 18px; font-weight: 700; }
QDialog#basicSettingsDialog QLabel#settingsMuted { color: #AEB7C2; }
QDialog#basicSettingsDialog QFrame#settingsHeading { border: none; border-bottom: 1px solid #2F3136; }
QDialog#basicSettingsDialog QFrame#settingsField { border: none; border-top: 1px solid #25292F; }
/* An editable combo box draws through an inner line edit rather than painting itself,
   so the rules written for the plain ones never reach it and it comes out as a bare
   arrow on the dialog background. Scoped to :editable so the plain ones are untouched. */
QDialog#basicSettingsDialog QComboBox:editable {
    color: #F1F3F5; background: #171B21; border: 1px solid #2F3136;
    border-radius: 6px; padding: 4px 8px;
}
QDialog#basicSettingsDialog QComboBox:editable:focus { border-color: #237AE9; }
QDialog#basicSettingsDialog QComboBox:editable QLineEdit {
    background: transparent; border: none; padding: 0; color: #F1F3F5;
    selection-background-color: #237AE9;
}
QDialog#basicSettingsDialog QComboBox:editable::drop-down { border: none; width: 22px; }
QDialog#basicSettingsDialog QPushButton#settingsNav {
    color: #DDE2E7; background: transparent; border: 1px solid transparent;
    border-radius: 6px; padding: 9px 11px; text-align: left;
}
QDialog#basicSettingsDialog QPushButton#settingsNav:hover:!checked {
    background: #222529; border-color: #2F3136;
}
QDialog#basicSettingsDialog QPushButton#settingsNav:checked {
    color: white; background: #193452; border-color: #237AE9;
}
QDialog#basicSettingsDialog QPushButton#settingsPrimaryButton {
    color: white; background: #237AE9; border: 1px solid #3B8BF0;
    border-radius: 6px; padding: 8px 18px; font-weight: 700;
}
QDialog#basicSettingsDialog QPushButton#settingsSecondaryButton {
    color: #F1F3F5; background: #222529; border: 1px solid #2F3136;
    border-radius: 6px; padding: 8px 14px;
}
QDialog#basicSettingsDialog QScrollArea#settingsScroll { background: transparent; border: none; }
QDialog#basicSettingsDialog QStackedWidget#settingsStack { background: transparent; }
QDialog#basicSettingsDialog QWidget#settingsPage QTextEdit,
QDialog#basicSettingsDialog QWidget#settingsPage QPlainTextEdit {
    color: #F1F3F5; background: #171B21; border: 1px solid #2F3136;
    border-radius: 6px; padding: 7px 9px; selection-background-color: #237AE9;
}
QDialog#basicSettingsDialog QWidget#settingsPage QTextEdit:focus,
QDialog#basicSettingsDialog QWidget#settingsPage QPlainTextEdit:focus { border-color: #237AE9; }
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QGroupBox {
    color: #F1F3F5; background: #171B21; border: 1px solid #2F3136;
    border-radius: 7px; margin-top: 12px; padding: 12px 10px 10px 10px;
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QGroupBox::title {
    color: #DDE2E7; subcontrol-origin: margin; subcontrol-position: top left;
    left: 12px; padding: 0 6px; font-weight: 650;
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QLineEdit,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QComboBox,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QSpinBox,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QDoubleSpinBox,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QTextEdit,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QPlainTextEdit,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QListWidget,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QTableView {
    color: #F1F3F5; background: #171B21; border: 1px solid #2F3136;
    border-radius: 6px; padding: 6px 9px; selection-background-color: #237AE9;
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QLineEdit:focus,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QComboBox:focus,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QSpinBox:focus,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QTextEdit:focus,
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QPlainTextEdit:focus {
    border-color: #237AE9;
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QCheckBox {
    color: #DDE2E7; spacing: 7px; background: transparent;
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QCheckBox::indicator {
    width: 18px; height: 18px; border: 1px solid #4A535E; border-radius: 5px; background: #22272E;
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QCheckBox::indicator:checked {
    background: #237AE9; border-color: #4193F4;
    image: url(:/icon/material/check.svg);
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QPushButton {
    color: #F1F3F5; background: #222529; border: 1px solid #2F3136;
    border-radius: 6px; padding: 7px 11px;
}
QDialog#basicSettingsDialog QWidget#settingsLegacyPage QPushButton:hover {
    background: #292E35; border-color: #4A535E;
}
    )");
    themeManager->RegisterStyle(this, settingsStyleTemplate);
    setMinimumSize(900, 620);
    FitWindowToScreen(this, QSize(1000, 700));
}

void DialogBasicSettings::showLoggingPage() {
    constexpr int loggingPage = 1;
    if (settingsStack_ != nullptr && settingsStack_->count() > loggingPage)
        settingsStack_->setCurrentIndex(loggingPage);
    if (settingsNavGroup_ != nullptr)
        if (auto *button = settingsNavGroup_->button(loggingPage)) button->setChecked(true);
}

DialogBasicSettings::~DialogBasicSettings() {
    delete ui;
}

static void highlightRegexLines(QTextEdit *edit) {
    if (!edit || !edit->document()) return;
    edit->blockSignals(true);
    QTextDocument *doc = edit->document();
    QRegularExpression validator;
    for (int i = 0; i < doc->blockCount(); ++i) {
        QTextBlock block = doc->findBlockByNumber(i);
        QString line = block.text();
        QTextBlockFormat fmt = block.blockFormat();
        if (line.trimmed().isEmpty()) {
            fmt.setBackground(Qt::NoBrush);
            QTextCursor cur(block);
            cur.setBlockFormat(fmt);
            continue;
        }
        validator.setPattern(line);
        fmt.setBackground(QBrush(validator.isValid() ? Qt::darkGreen : Qt::darkRed));
        QTextCursor cur(block);
        cur.setBlockFormat(fmt);
    }
    edit->blockSignals(false);
}

void DialogBasicSettings::applyRegexHighlighting() {
    highlightRegexLines(ui->log_include_regex);
    highlightRegexLines(ui->log_exclude_regex);
}

void DialogBasicSettings::applySelectedTheme() {
    const auto theme = ui->theme->currentText();
    if (theme.isEmpty()) return;
    themeManager->ApplyTheme(theme);
    Configs::dataManager->settingsRepo->theme = theme;
    Configs::dataManager->settingsRepo->Save();
}

void DialogBasicSettings::accept() {
    bool needChoosePort = false;

    D_SAVE_STRING(inbound_address)
    Configs::dataManager->settingsRepo->custom_inbound = CACHE.custom_inbound;
    D_SAVE_INT(inbound_socks_port)
    if (!Configs::dataManager->settingsRepo->random_inbound_port && ui->random_listen_port->isChecked())
    {
        needChoosePort = true;
    }
    Configs::dataManager->settingsRepo->random_inbound_port = ui->random_listen_port->isChecked();
    D_SAVE_INT(test_concurrent)
    D_SAVE_STRING(test_latency_url)
    D_SAVE_BOOL(disable_tray)
    Configs::dataManager->settingsRepo->proxy_scheme = ui->proxy_scheme->currentText().toLower();
    Configs::dataManager->settingsRepo->speed_test_mode = ui->speedtest_mode->currentIndex();
    Configs::dataManager->settingsRepo->simple_dl_url = ui->simple_down_url->text();
    Configs::dataManager->settingsRepo->url_test_timeout_ms = ui->url_timeout->text().toInt();
    if (const auto target = ui->udp_test_target->text().trimmed(); !target.isEmpty())
        Configs::dataManager->settingsRepo->udp_test_target = target;
    Configs::dataManager->settingsRepo->speed_test_timeout_ms = ui->test_timeout->text().toInt();
    Configs::dataManager->settingsRepo->allow_beta_update = ui->allow_beta->isChecked();
    Configs::dataManager->settingsRepo->disable_mixed_inbound = ui->disable_mixed_inbound->isChecked();
    Configs::dataManager->settingsRepo->reset_proxy_on_disable_sp = ui->reset_proxy_on_disable_sp->isChecked();
    D_SAVE_BOOL(inbound_auth)
    D_SAVE_STRING(inbound_user)
    D_SAVE_STRING(inbound_pass)

    auto oldMaxLogLines = Configs::dataManager->settingsRepo->max_log_line;
    Configs::dataManager->settingsRepo->max_log_line = ui->max_log_line->text().toInt();
    if (oldMaxLogLines != Configs::dataManager->settingsRepo->max_log_line) CACHE.updateMaxLogLines = true;
    Configs::dataManager->settingsRepo->log_level = ui->log_level->currentText();
    Configs::dataManager->settingsRepo->xray_log_level = ui->xray_loglevel->currentText();
    Configs::dataManager->settingsRepo->log_enable_include = ui->enable_log_include->isChecked();
    Configs::dataManager->settingsRepo->log_enable_exclude = ui->enable_log_exclude->isChecked();
    D_SAVE_BOOL(log_auto_scroll)
    Configs::dataManager->settingsRepo->log_include_keyword = SplitAndTrim(ui->log_include_keyword->toPlainText(), "\n", false);
    Configs::dataManager->settingsRepo->log_exclude_keyword = SplitAndTrim(ui->log_exclude_keyword->toPlainText(), "\n", false);

    Configs::dataManager->settingsRepo->log_include_regex.clear();
    Configs::dataManager->settingsRepo->log_exclude_regex.clear();
    QRegularExpression regexValidator;
    for (QStringList log_include_lines = SplitAndTrim(ui->log_include_regex->toPlainText(), "\n", false); const QString &line : log_include_lines) {
        if (regexValidator.setPattern(line); regexValidator.isValid()) Configs::dataManager->settingsRepo->log_include_regex << line;
    }
    for (QStringList log_exclude_lines = SplitAndTrim(ui->log_exclude_regex->toPlainText(), "\n", false); const QString &line : log_exclude_lines) {
        if (regexValidator.setPattern(line); regexValidator.isValid()) Configs::dataManager->settingsRepo->log_exclude_regex << line;
    }

    Configs::dataManager->settingsRepo->enable_stats = ui->connection_statistics->isChecked();
    Configs::dataManager->settingsRepo->disable_traffic_aggregation = ui->disable_traffic_aggregation->isChecked();
    Configs::dataManager->settingsRepo->language = ui->language->currentIndex();
    auto oldUseCustomIcon = Configs::dataManager->settingsRepo->use_custom_icons;
    Configs::dataManager->settingsRepo->use_custom_icons = ui->enable_custom_icon->isChecked();
    if (oldUseCustomIcon != Configs::dataManager->settingsRepo->use_custom_icons) CACHE.updateTrayIcon = true;
    D_SAVE_BOOL(start_minimal)
    Configs::dataManager->settingsRepo->skip_delete_confirmation = ui->skip_delete_confirm->isChecked();
    bool profileListDisplayChanged =
        Configs::dataManager->settingsRepo->show_config_security != ui->show_config_security->isChecked();
    D_SAVE_BOOL(show_config_security)
    Configs::dataManager->settingsRepo->show_system_dns = ui->show_sys_dns->isChecked();

    if (Configs::dataManager->settingsRepo->max_log_line <= 0) {
        Configs::dataManager->settingsRepo->max_log_line = 200;
    }

    // The PeriodicRunner reads these intervals live; no timer needs restarting.

    Configs::dataManager->settingsRepo->user_agent = ui->user_agent->text();
    D_SAVE_BOOL(net_use_proxy)
    D_SAVE_BOOL(allow_stopping_active_profile)
    D_SAVE_BOOL(sub_clear)
    D_SAVE_BOOL(sub_show_change_popup)
    D_SAVE_BOOL(net_insecure)
    D_SAVE_BOOL(sub_send_hwid)
    D_SAVE_STRING(sub_custom_hwid_params)
    D_SAVE_INT_ENABLE(sub_auto_update, sub_auto_update_enable)
    D_SAVE_INT_ENABLE(route_auto_update, route_auto_update_enable)
    D_SAVE_INT_ENABLE(app_auto_update, app_auto_update_enable)

    Configs::dataManager->settingsRepo->disable_traffic_stats = ui->disable_stats->isChecked();
    Configs::dataManager->settingsRepo->core_dns_in_port = ui->dns_in_port->text().toInt();
    Configs::dataManager->settingsRepo->core_box_clash_listen_addr = ui->core_box_clash_listen_addr->text();
    Configs::dataManager->settingsRepo->core_box_clash_api = ui->core_box_clash_api->text().toInt();
    Configs::dataManager->settingsRepo->core_box_clash_api_secret = ui->core_box_clash_api_secret->text();
    Configs::dataManager->settingsRepo->core_box_api_port = ui->core_box_api_port->text().toInt();
    // Blank means "no authentication" to sing-box, so never let the field clear it.
    if (const auto secret = ui->core_box_api_secret->text(); !secret.isEmpty())
        Configs::dataManager->settingsRepo->core_box_api_secret = secret;

    Configs::dataManager->settingsRepo->xray_vless_preference = static_cast<Configs::Xray::XrayVlessPreference>(ui->vless_xray_pref->currentIndex());
    D_SAVE_STRING(xray_geoip_url)
    D_SAVE_STRING(xray_geosite_url)

    Configs::dataManager->settingsRepo->enable_ntp = ui->ntp_enable->isChecked();
    Configs::dataManager->settingsRepo->ntp_server_address = ui->ntp_server->text();
    Configs::dataManager->settingsRepo->ntp_server_port = ui->ntp_port->text().toInt();
    Configs::dataManager->settingsRepo->ntp_interval = ui->ntp_interval->currentText();
    Configs::dataManager->settingsRepo->ntp_outbound = ui->ntp_outbound->currentText();

    // Security

    // Has to be asked before the bindings overwrite the stored value.
    if (alwaysStandardUser_ != nullptr
        && Configs::dataManager->settingsRepo->disable_run_admin != alwaysStandardUser_->isChecked()) {
        CACHE.updateDisableAdmin = true;
    }
    bindings_.save();

    // A quick Save can arrive before the preview debounce fires. Flush it while
    // the dialog and its combo still exist instead of silently losing the choice.
    if (themeApplyDebounce_ != nullptr && themeApplyDebounce_->isActive()) {
        themeApplyDebounce_->stop();
        applySelectedTheme();
    }

    QStringList changes;
    if (CACHE.needRestart) changes << MwArg::NeedRestart;
    if (CACHE.updateDisableTray) changes << MwArg::DisableTray;
    if (CACHE.updateSystemDns) changes << MwArg::SystemDns;
    if (CACHE.updateTrayIcon) changes << MwArg::TrayIcon;
    if (CACHE.updateMaxLogLines) changes << MwArg::MaxLogLines;
    if (CACHE.updateDisableAdmin) changes << MwArg::DisableAdmin;
    if (needChoosePort) changes << MwArg::ChoosePort;
    if (profileListDisplayChanged) changes << MwArg::ProfileListDisplay;
    MW_dialog_message(MwMessage::UpdateSettings, changes);
    QDialog::accept();
}

// v1 archives carry no "parts" metadata and are read back as full snapshots.
static constexpr quint32 BACKUP_FORMAT_VERSION = 2;
static constexpr int BACKUP_CONTENT_VERSION = 2;

static Configs::BackupParts BackupPartsFromMeta(quint32 formatVersion, const QJsonObject& meta,
                                                const QMap<QString, QByteArray>& files) {
    Configs::BackupParts p;
    bool hasIcons = false;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key().startsWith("icons/")) { hasIcons = true; break; }
    }
    if (formatVersion >= 2 && meta.contains("parts")) {
        const QJsonObject po = meta["parts"].toObject();
        p.profiles = po["profiles"].toBool() && files.contains("database");
        p.routes = po["routes"].toBool() && files.contains("database");
        p.settings = po["settings"].toBool() && files.contains("database");
        p.otp = po["otp"].toBool() && files.contains("database");
        p.icons = po["icons"].toBool() && hasIcons;
    } else {
        p.profiles = p.routes = p.settings = files.contains("database");
        p.icons = hasIcons;
    }
    return p;
}

void DialogBasicSettings::downloadXrayGeoAsset(const QString &url, const QString &fileName) {
    const QString effectiveUrl = url.trimmed();
    if (effectiveUrl.isEmpty()) {
        QMessageBox::warning(this, tr("Download geo asset"),
            tr("Please enter a URL for %1 first.").arg(fileName));
        return;
    }
    MW_show_log(tr("Downloading Xray geo asset: %1").arg(fileName));
    // DownloadAsset drives a blocking event loop; don't capture the dialog, it may close first.
    runOnNewThread([effectiveUrl, fileName] {
        const bool proxyAvailable = Configs::dataManager->settingsRepo->started_id >= 0;
        const auto err = NetworkRequestHelper::DownloadAsset(effectiveUrl, fileName, proxyAvailable);
        runOnUiThread([err, fileName] {
            if (err.isEmpty()) {
                MW_show_log(QObject::tr("Downloaded Xray geo asset: %1").arg(fileName));
                QMessageBox::information(GetMainWindow(), QObject::tr("Download geo asset"),
                    QObject::tr("%1 was downloaded successfully.").arg(fileName));
            } else {
                MessageBoxWarning(QObject::tr("Download geo asset"),
                    QObject::tr("Failed to download %1:\n%2").arg(fileName, err));
            }
        });
    });
}

void DialogBasicSettings::on_xray_geoip_download_clicked() {
    QString url = ui->xray_geoip_url->text().trimmed();
    if (url.isEmpty()) url = ui->xray_geoip_url->placeholderText();
    downloadXrayGeoAsset(url, "geoip.dat");
}

void DialogBasicSettings::on_xray_geosite_download_clicked() {
    QString url = ui->xray_geosite_url->text().trimmed();
    if (url.isEmpty()) url = ui->xray_geosite_url->placeholderText();
    downloadXrayGeoAsset(url, "geosite.dat");
}

void DialogBasicSettings::on_backup_create_clicked() {
    Configs::BackupParts parts;
    parts.profiles = ui->backup_inc_profiles->isChecked();
    parts.routes = ui->backup_inc_routes->isChecked();
    parts.settings = ui->backup_inc_settings->isChecked();
    parts.otp = ui->backup_inc_otp->isChecked();
    parts.icons = ui->backup_inc_icons->isChecked();

    if (!parts.any()) {
        QMessageBox::warning(this, tr("Create Backup"),
            tr("Select at least one part to include in the backup."));
        return;
    }

    if (parts.settings) Configs::dataManager->settingsRepo->Save();

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Create Backup"),
        QDir::homePath() + "/Throned-backup.thrbackup",
        tr("Throned Backup (*.thrbackup)")
    );
    if (filePath.isEmpty()) return;

    QMap<QString, QByteArray> files;

    if (parts.anyDb()) {
        QString tempDbPath = QDir::temp().filePath("Thr_backup_tmp.db");
        QFile::remove(tempDbPath);

        try {
            Configs::dataManager->getDatabase().backupSelective(tempDbPath.toStdString(), parts);
        } catch (std::exception& e) {
            QFile::remove(tempDbPath);
            QMessageBox::critical(this, tr("Backup Failed"),
                tr("Failed to create database snapshot: %1").arg(e.what()));
            return;
        }

        QFile tempDbFile(tempDbPath);
        if (!tempDbFile.open(QIODevice::ReadOnly)) {
            QFile::remove(tempDbPath);
            QMessageBox::critical(this, tr("Backup Failed"), tr("Failed to read database snapshot."));
            return;
        }
        files["database"] = tempDbFile.readAll();
        tempDbFile.close();
        QFile::remove(tempDbPath);
    }

    if (parts.icons) {
        QDir iconsDir("icons");
        if (iconsDir.exists()) {
            for (const QFileInfo& entry : iconsDir.entryInfoList(QDir::Files)) {
                QFile iconFile(entry.absoluteFilePath());
                if (iconFile.open(QIODevice::ReadOnly)) {
                    files["icons/" + entry.fileName()] = iconFile.readAll();
                }
            }
        }
    }

    QFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Backup Failed"),
            tr("Cannot write to: %1").arg(filePath));
        return;
    }

    QDataStream stream(&outFile);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);

    stream.writeRawData("THRN", 4);
    stream << BACKUP_FORMAT_VERSION;

    QJsonObject partsObj;
    partsObj["profiles"] = parts.profiles;
    partsObj["routes"] = parts.routes;
    partsObj["settings"] = parts.settings;
    partsObj["otp"] = parts.otp;
    partsObj["icons"] = parts.icons;

    QJsonObject meta;
    meta["backup_version"] = BACKUP_CONTENT_VERSION;
    meta["created_at"] = QDateTime::currentDateTime().toString(Qt::TextDate);
    meta["platform"] = QSysInfo::kernelType();
    meta["parts"] = partsObj;
    stream << QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact));

    stream << files;
    outFile.close();

    QStringList included;
    if (parts.profiles) included << tr("Profiles");
    if (parts.routes) included << tr("Routing profiles");
    if (parts.settings) included << tr("Settings");
    if (parts.otp) included << tr("OTP profiles");
    if (parts.icons) included << tr("Custom icons");

    QMessageBox::information(this, tr("Backup Created"),
        tr("Backup created successfully:\n%1\n\nIncluded: %2")
            .arg(filePath, included.join(", ")));
}

void DialogBasicSettings::on_backup_restore_clicked() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Restore Backup"),
        QDir::homePath(),
        tr("Throned Backup (*.thrbackup)")
    );
    if (filePath.isEmpty()) return;

    QFile inFile(filePath);
    if (!inFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("Cannot open backup file: %1").arg(filePath));
        return;
    }

    QDataStream stream(&inFile);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);

    char magic[4];
    if (stream.readRawData(magic, 4) != 4 || strncmp(magic, "THRN", 4) != 0) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("Not a valid Throned backup file."));
        return;
    }

    quint32 formatVersion;
    stream >> formatVersion;
    if (formatVersion < 1 || formatVersion > BACKUP_FORMAT_VERSION) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("Unsupported backup format version: %1.\nThis backup may have been created with a newer version of the application.")
                .arg(formatVersion));
        return;
    }

    QString metaJson;
    stream >> metaJson;
    QJsonObject meta = QJsonDocument::fromJson(metaJson.toUtf8()).object();
    QString createdAt = meta["created_at"].toString();

    QMap<QString, QByteArray> files;
    stream >> files;
    inFile.close();

    Configs::BackupParts avail = BackupPartsFromMeta(formatVersion, meta, files);
    if (!avail.any()) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("This backup file does not contain any restorable data."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Restore Backup"));
    auto* layout = new QVBoxLayout(&dlg);
    auto* header = new QLabel(
        tr("Backup created on %1.\nSelect which parts to restore:")
            .arg(createdAt.isEmpty() ? tr("unknown date") : createdAt), &dlg);
    header->setWordWrap(true);
    layout->addWidget(header);

    auto* cbProfiles = new QCheckBox(tr("Profiles (groups and proxies)"), &dlg);
    auto* cbRoutes = new QCheckBox(tr("Routing profiles"), &dlg);
    auto* cbSettings = new QCheckBox(tr("Settings"), &dlg);
    auto* cbOtp = new QCheckBox(tr("OTP profiles"), &dlg);
    auto* cbIcons = new QCheckBox(tr("Custom icons"), &dlg);
    for (auto* cb : {cbProfiles, cbRoutes, cbSettings, cbOtp, cbIcons}) cb->setChecked(true);
    cbProfiles->setEnabled(avail.profiles);
    cbProfiles->setChecked(avail.profiles);
    cbRoutes->setEnabled(avail.routes);
    cbRoutes->setChecked(avail.routes);
    cbSettings->setEnabled(avail.settings);
    cbSettings->setChecked(avail.settings);
    cbOtp->setEnabled(avail.otp);
    cbOtp->setChecked(avail.otp);
    cbIcons->setEnabled(avail.icons);
    cbIcons->setChecked(avail.icons);
    layout->addWidget(cbProfiles);
    layout->addWidget(cbRoutes);
    layout->addWidget(cbSettings);
    layout->addWidget(cbOtp);
    layout->addWidget(cbIcons);

    auto* warn = new QLabel(
        tr("Each selected part replaces the current data. This cannot be undone.\n"
           "Throned will restart to complete the restore."), &dlg);
    warn->setWordWrap(true);
    layout->addWidget(warn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Restore"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    Configs::BackupParts chosen;
    chosen.profiles = avail.profiles && cbProfiles->isChecked();
    chosen.routes = avail.routes && cbRoutes->isChecked();
    chosen.settings = avail.settings && cbSettings->isChecked();
    chosen.otp = avail.otp && cbOtp->isChecked();
    chosen.icons = avail.icons && cbIcons->isChecked();

    if (!chosen.any()) {
        QMessageBox::warning(this, tr("Restore Backup"),
            tr("Select at least one part to restore."));
        return;
    }

    if (chosen.anyDb()) {
        QString tempDbPath = QDir::temp().filePath("Thr_restore_tmp.db");
        QFile::remove(tempDbPath);
        QFile tempDbFile(tempDbPath);
        if (!tempDbFile.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, tr("Restore Failed"),
                tr("Failed to create temporary file for restore."));
            return;
        }
        tempDbFile.write(files["database"]);
        tempDbFile.close();

        try {
            Configs::dataManager->getDatabase().restoreSelective(tempDbPath.toStdString(), chosen);
        } catch (std::exception& e) {
            QFile::remove(tempDbPath);
            QMessageBox::critical(this, tr("Restore Failed"),
                tr("Failed to restore database: %1").arg(e.what()));
            return;
        }
        QFile::remove(tempDbPath);
    }

    if (chosen.icons) {
        QDir iconsDir("icons");
        iconsDir.mkpath(".");
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            if (it.key().startsWith("icons/")) {
                QString iconName = it.key().mid(6);
                if (iconName.isEmpty()) continue;
                QFile iconFile(iconsDir.filePath(iconName));
                if (iconFile.open(QIODevice::WriteOnly)) {
                    iconFile.write(it.value());
                }
            }
        }
    }

    // The exit path's settingsRepo->Save() would write the stale in-memory values back over the restore.
    if (chosen.settings) Configs::dataManager->settingsRepo->noSave = true;

    QMessageBox::information(this, tr("Restore Complete"),
        tr("Backup restored successfully. Throned will now restart for the changes to take effect."));
    MW_dialog_message(MwMessage::RestartProgram, {});
    QDialog::reject();
}
