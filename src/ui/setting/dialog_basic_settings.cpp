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
#include "include/ui/utils/GuiUtils.hpp"
#include "include/global/Configs.hpp"
#include "include/global/DeviceDetailsHelper.hpp"
#include "include/global/ExitLookup.hpp"
#include "include/global/Validation.hpp"
#include "include/database/entities/Group.h"
#include "include/engine/services/AssetUpdates.h"
#include "include/engine/services/BackupArchive.h"
#include "include/ui/utils/UiMode.h"
#include "include/ui/utils/ValidationForm.h"

#include <QStyleFactory>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <qfontdatabase.h>
#include <QJsonObject>
#include <QDir>
#include <QUuid>
#include <QCheckBox>
#include <QScreen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QSslSocket>
#include <QStandardItemModel>
#include <QFile>
#include <QFormLayout>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QProgressDialog>
#include <QScrollBar>
#include <QSysInfo>
#include <QTreeWidget>
#include <QUrl>

#include <algorithm>
#include <limits>

#include "include/sys/UrlScheme.hpp"
#include "include/ui/mainwindow.h"
#include "include/ui/setting/DiagnosticsTab.h"
#include "include/ui/utils/NativeFileDialog.h"

namespace {
constexpr int kBasicSettingsMinAutoUpdate = 30;
constexpr int kBasicSettingsMaxAutoUpdate = 525600;

// The test runners read a bare integer; anything else, "3s" included, becomes 0.
QString basicSettingsMillisError(const QString &text, int example, bool zeroIsDefault) {
    if (const auto ms = Validation::ToInt(text); ms && *ms >= (zeroIsDefault ? 0 : 1) && *ms <= std::numeric_limits<int>::max()) return {};
    if (zeroIsDefault)
        return DialogBasicSettings::tr("Enter a whole number of milliseconds, for example %1 (without a unit), or 0 for the default.").arg(example);
    return DialogBasicSettings::tr("Enter a whole number of milliseconds, for example %1 (without a unit).").arg(example);
}

// GroupUpdater sends only values below 1000 characters without line breaks, and silently falls back otherwise.
QString basicSettingsHeaderValueError(const QString &text) {
    if (text.size() < 1000 && !text.contains(QLatin1Char('\n')) && !text.contains(QLatin1Char('\r'))) return {};
    return DialogBasicSettings::tr("Must be shorter than 1000 characters, on a single line.");
}

// The keys GroupUpdater's applyCustomHwidParams reads; it skips anything else without a word.
QString basicSettingsHwidParamsError(const QString &text) {
    static const QStringList keys = {QStringLiteral("hwid"), QStringLiteral("os"), QStringLiteral("osversion"), QStringLiteral("model")};
    for (const auto &item: Validation::Items(text, Validation::Split::Comma)) {
        const auto eq = item.indexOf(QLatin1Char('='));
        const auto value = eq > 0 ? item.mid(eq + 1).trimmed() : QString();
        if (eq > 0 && keys.contains(item.left(eq).trimmed().toLower()) && !value.isEmpty() && basicSettingsHeaderValueError(value).isEmpty())
            continue;
        return DialogBasicSettings::tr("Unknown or malformed parameter \"%1\". Use hwid=…, os=…, osVersion=…, model=… separated by commas.")
            .arg(item.size() > 40 ? item.left(39) + QChar(0x2026) : item);
    }
    return {};
}

// Same per-line wording as Validation::Form::bindLines, which takes only a QPlainTextEdit.
QString basicSettingsRegexLinesError(const QString &text) {
    const auto lines = text.split(QLatin1Char('\n'));
    QString first;
    int bad = 0;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        const auto problem = Validation::Regex(line);
        if (problem.isEmpty()) continue;
        if (bad++ == 0) {
            first = QCoreApplication::translate("Validation", "Line %1 (\"%2\"): %3")
                        .arg(QString::number(i + 1), line.size() > 40 ? line.left(39) + QChar(0x2026) : line, problem);
        }
    }
    if (bad > 1) first += QLatin1Char(' ') + QCoreApplication::translate("Validation", "%n more line(s) have problems.", nullptr, bad - 1);
    return first;
}

// Underlines the lines that do not compile; extra selections stay off the undo stack.
void basicSettingsMarkRegexLines(QTextEdit *edit) {
    QList<QTextEdit::ExtraSelection> selections;
    QTextCharFormat format;
    format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    format.setUnderlineColor(themeManager()->Colors().danger.isValid() ? themeManager()->Colors().danger : QColor(Qt::red));
    for (QTextBlock block = edit->document()->begin(); block.isValid(); block = block.next()) {
        if (Validation::Regex(block.text()).isEmpty()) continue;
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(block);
        selection.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        selection.format = format;
        selections.append(selection);
    }
    edit->setExtraSelections(selections);
}

// BackupArchive answers in English with these texts; the dialog shows their translations.
const char *const kBasicSettingsBackupErrors[] = {
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Failed to create database snapshot: %1"),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Failed to read database snapshot."),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Cannot write to: %1"),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Cannot open backup file: %1"),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Not a valid Throned backup file."),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Unsupported backup format version: %1.\nThis backup may have been created with a newer version of the application."),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "The backup file is damaged or incomplete."),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "This backup file does not contain any restorable data."),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Failed to create temporary file for restore."),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "The backup's database is damaged: %1"),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "Failed to restore database: %1"),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "There is nothing to back up"),
    QT_TRANSLATE_NOOP("DialogBasicSettings", "A subscription update is running; try again when it finishes."),
};

QString basicSettingsBackupError(const QString &error) {
    for (const char *source: kBasicSettingsBackupErrors) {
        const auto text = QString::fromUtf8(source);
        const auto at = text.indexOf(QLatin1String("%1"));
        if (at < 0) {
            if (error == text) return DialogBasicSettings::tr(source);
            continue;
        }
        const auto prefix = text.left(at);
        const auto suffix = text.mid(at + 2);
        if (error.size() >= prefix.size() + suffix.size() && error.startsWith(prefix) && error.endsWith(suffix))
            return DialogBasicSettings::tr(source).arg(error.mid(prefix.size(), error.size() - prefix.size() - suffix.size()));
    }
    return error;
}

Configs::BackupParts basicSettingsCheckedParts(const Ui::DialogBasicSettings *ui) {
    Configs::BackupParts parts;
    parts.profiles = ui->backup_inc_profiles->isChecked();
    parts.routes = ui->backup_inc_routes->isChecked();
    parts.settings = ui->backup_inc_settings->isChecked();
    parts.otp = ui->backup_inc_otp->isChecked();
    parts.ipLists = ui->backup_inc_ip_lists->isChecked();
    parts.icons = ui->backup_inc_icons->isChecked();
    return parts;
}

// Text a WebDAV server or another device wrote, for plain-text display only.
QString basicSettingsRemoteText(const QString &text) {
    return text.simplified().left(200);
}

QString basicSettingsPlatformName(const QString &platform) {
    if (platform == QLatin1String("winnt")) return QStringLiteral("Windows");
    if (platform == QLatin1String("linux")) return QStringLiteral("Linux");
    if (platform == QLatin1String("darwin")) return QStringLiteral("macOS");
    if (platform == QLatin1String("android")) return QStringLiteral("Android");
    return basicSettingsRemoteText(platform);
}

QString basicSettingsPartsText(const QStringList &parts) {
    QStringList shown;
    for (const auto &part: parts) {
        QString label;
        if (part == QLatin1String("profiles"))
            label = DialogBasicSettings::tr("Profiles");
        else if (part == QLatin1String("routes"))
            label = DialogBasicSettings::tr("Routing profiles");
        else if (part == QLatin1String("settings"))
            label = DialogBasicSettings::tr("Settings");
        else if (part == QLatin1String("otp"))
            label = DialogBasicSettings::tr("OTP profiles");
        else if (part == QLatin1String("ip_lists"))
            label = DialogBasicSettings::tr("IP lists and scans");
        else if (part == QLatin1String("icons"))
            label = DialogBasicSettings::tr("Custom icons");
        if (!label.isEmpty() && !shown.contains(label)) shown << label;
    }
    return shown.join(QStringLiteral(", "));
}

// Window-modal busy indicator without a cancel button; the caller closes it when the worker reports back.
QPointer<QProgressDialog> basicSettingsWebDavProgress(QWidget *parent, const QString &text) {
    auto *progress = new QProgressDialog(text, QString(), 0, 0, parent);
    progress->setWindowTitle(QStringLiteral("WebDAV"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->show();
    return progress;
}

// Non-blocking, and plain text because it may carry what a server sent.
void basicSettingsWebDavMessage(QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text) {
    auto *box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
    box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}
} // namespace

struct DialogBasicSettings::WebDavChooser {
    QPointer<QDialog> dialog;
    QTreeWidget *tree = nullptr;
    QLabel *note = nullptr;
    QPushButton *restore = nullptr;
    QPushButton *remove = nullptr;
    QList<Engine::WebDavBackup::Entry> entries;
};

DialogBasicSettings::DialogBasicSettings(QWidget *parent)
    : QDialog(parent), ui(new Ui::DialogBasicSettings) {
    ui->setupUi(this);
    form = new Validation::Form(this);
    form->attachStatus(ui->buttonBox);
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
    D_LOAD_STRING(direct_test_url)
    D_LOAD_BOOL(exit_country_lookup)
    ui->exit_lookup_provider->addItem(tr("Automatic"), Configs_network::ExitLookupAutoId);
    for (const auto &provider: Configs_network::ExitLookupProviders()) ui->exit_lookup_provider->addItem(provider.label, provider.id);
    ui->exit_lookup_provider->setCurrentIndex(
        std::max(0, ui->exit_lookup_provider->findData(Configs::dataManager->settingsRepo->exit_lookup_provider)));
    ui->label_exit_lookup_provider->setEnabled(ui->exit_country_lookup->isChecked());
    ui->exit_lookup_provider->setEnabled(ui->exit_country_lookup->isChecked());
    connect(ui->exit_country_lookup, &QCheckBox::toggled, this, [this](bool enabled) {
        ui->label_exit_lookup_provider->setEnabled(enabled);
        ui->exit_lookup_provider->setEnabled(enabled);
    });
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

    ui->url_scheme_auto_register->setChecked(Configs::dataManager->settingsRepo->url_scheme_auto_register);
    ui->file_assoc_auto_register->setChecked(Configs::dataManager->settingsRepo->file_assoc_auto_register);
    const auto connectAssociation = [this](Association a, QCheckBox *autoRegister, QPushButton *install, QPushButton *uninstall, const QString &failure) {
        connect(install, &QPushButton::clicked, this, [=, this] {
            const bool ok = UrlScheme_Install(a);
            refreshUrlSchemeStatus();
            if (!ok) QMessageBox::warning(this, ui->url_scheme_box->title(), failure);
        });
        connect(uninstall, &QPushButton::clicked, this, [=, this] {
            UrlScheme_Uninstall(a);
            autoRegister->setChecked(false);
            refreshUrlSchemeStatus();
        });
    };
    connectAssociation(Association::Links, ui->url_scheme_auto_register, ui->url_scheme_install, ui->url_scheme_uninstall,
                       tr("Could not register the handler for throne:// links."));
    connectAssociation(Association::ConfigFiles, ui->file_assoc_auto_register, ui->file_assoc_install, ui->file_assoc_uninstall,
                       tr("Could not register the config file associations."));
#ifdef Q_OS_MACOS
    // LaunchServices registers the scheme from the bundle's Info.plist, so there is nothing of ours to add or take back.
    ui->url_scheme_install->hide();
    ui->url_scheme_uninstall->hide();
#endif
    const bool urlSchemeSupported = UrlScheme_IsSupported(Association::Links);
    const bool portableCopy = !Configs::dataManager->settingsRepo->flag_use_appdata;
    ui->url_scheme_auto_register->setEnabled(urlSchemeSupported && !portableCopy);
    ui->file_assoc_auto_register->setEnabled(UrlScheme_IsSupported(Association::ConfigFiles) && !portableCopy);
    if (portableCopy) {
        ui->url_scheme_auto_register->setToolTip(
            tr("A portable copy leaves the system handler alone. Use Install when you want it."));
        ui->file_assoc_auto_register->setToolTip(
            tr("A portable copy leaves config file associations alone. Use Install when you want them."));
    }
    ui->url_scheme_install->setEnabled(urlSchemeSupported);
    ui->url_scheme_uninstall->setEnabled(urlSchemeSupported);
    if (!UrlScheme_IsSupported(Association::ConfigFiles)) {
        ui->file_assoc_auto_register->hide();
        ui->file_assoc_status->hide();
        ui->file_assoc_install->hide();
        ui->file_assoc_uninstall->hide();
    }
    refreshUrlSchemeStatus();

    connect(ui->custom_inbound_edit, &QPushButton::clicked, this, [=, this] {
        C_EDIT_JSON_ALLOW_EMPTY(custom_inbound, JsonEdit::SingBox::Config)
    });
    connect(ui->disable_tray, &QCheckBox::stateChanged, this, [=, this](const bool &) {
        CACHE.updateDisableTray = true;
    });
    connect(ui->random_listen_port, &QCheckBox::stateChanged, this, [=, this](const bool &state) {
        if (state) {
            ui->inbound_socks_port->setDisabled(true);
        } else {
            ui->inbound_socks_port->setDisabled(false);
        }
    });

#ifndef Q_OS_WIN
    ui->proxy_scheme_l->hide();
    ui->proxy_scheme->hide();
#endif

    ui->max_log_line->setText(QString::number(Configs::dataManager->settingsRepo->max_log_line));
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

    ui->connection_statistics->setChecked(Configs::dataManager->settingsRepo->enable_stats);
    ui->disable_traffic_aggregation->setChecked(Configs::dataManager->settingsRepo->disable_traffic_aggregation);
    D_LOAD_BOOL(start_minimal)
    ui->skip_delete_confirm->setChecked(Configs::dataManager->settingsRepo->skip_delete_confirmation);
    D_LOAD_BOOL(show_config_security)
    ui->language->setCurrentIndex(Configs::dataManager->settingsRepo->language);
    ui->ui_mode->setCurrentIndex(Configs::dataManager->settingsRepo->simple_interface ? 0 : 1);
    connect(ui->language, &QComboBox::currentIndexChanged, this, [=, this](int index) {
        CACHE.needRestart = true;
    });
    connect(ui->font, &QComboBox::currentTextChanged, this, [=, this](const QString &fontName) {
        auto font = qApp->font();
        font.setFamily(fontName);
        qApp->setFont(font);
        Configs::dataManager->settingsRepo->font = fontName;
        Configs::dataManager->settingsRepo->Save();
        themeManager()->RefreshRegisteredStyles();
        updateGeometry();
    });
    for (int i = 7; i <= 26; i++) {
        ui->font_size->addItem(Int2String(i));
    }
    ui->font_size->setCurrentText(Int2String(qApp->font().pointSize()));
    connect(ui->font_size, &QComboBox::currentTextChanged, this, [=, this](const QString &sizeStr) {
        auto font = qApp->font();
        font.setPointSize(sizeStr.toInt());
        qApp->setFont(font);
        Configs::dataManager->settingsRepo->font_size = sizeStr.toInt();
        Configs::dataManager->settingsRepo->Save();
        themeManager()->RefreshRegisteredStyles();
        updateGeometry();
    });
    ui->log_font_family->setCurrentFont(QFont(ResolveLogFontFamily(Configs::dataManager->settingsRepo->log_font_family)));
    CACHE.shownLogFontFamily = ui->log_font_family->currentFont().family();
    for (int i = 7; i <= 26; i++) {
        ui->log_font_size->addItem(Int2String(i));
    }
    const int logFontSize = Configs::dataManager->settingsRepo->log_font_size;
    ui->log_font_size->setCurrentText(Int2String(logFontSize > 0 ? logFontSize : qApp->font().pointSize()));
    CACHE.shownLogFontSize = ui->log_font_size->currentText().toInt();
    //
    ui->theme->setIconSize(QSize(64, 22));
    ui->theme->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (const QString &theme: themeManager()->ThronedThemes())
        ui->theme->addItem(themeManager()->PreviewIcon(theme), theme);
    ui->enable_custom_icon->setChecked(Configs::dataManager->settingsRepo->use_custom_icons);
    ui->follow_status_in_taskbar->setChecked(Configs::dataManager->settingsRepo->follow_status_in_taskbar);
    ui->follow_status_in_taskbar->setEnabled(ui->enable_custom_icon->isChecked());
    connect(ui->enable_custom_icon, &QCheckBox::toggled, this, [this](bool enabled) {
        ui->follow_status_in_taskbar->setEnabled(enabled);
    });
    connect(ui->select_custom_icon, &QPushButton::clicked, this, [=, this] {
        auto n = QMessageBox::information(this, "Custom Icon Manual", tr(Configs::Information::CustomIconManual.toStdString().c_str()), QMessageBox::Open | QMessageBox::Cancel);
        if (n == QMessageBox::Open) {
            auto fileNames = NativeFileDialog::getOpenFileNames(this,
                                                                tr("Select png icons"), QDir::homePath(), tr("Image Files (*.png)"));
            QString errors;
            for (const auto &fileName: fileNames) {
                CACHE.updateTrayIcon = true;
                QFileInfo fileInfo(fileName);
                if (auto pixMap = QPixmap(fileName); pixMap.isNull())
                    errors += "Failed to load " + fileName + "\n";
                else if (pixMap.width() != pixMap.height())
                    errors += "Image does not have equal width and height: " + fileName + "\n";
                else if (!Configs::Information::iconNames.contains(fileInfo.fileName()))
                    errors += "Icon name is not valid: " + fileInfo.fileName() + "\n";
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
        selectedTheme = themeManager()->IsDarkTheme(selectedTheme) ? QStringLiteral("Throned Midnight")
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
    ui->sub_tls_version->setCurrentIndex(std::clamp(Configs::dataManager->settingsRepo->sub_tls_version, 0, ui->sub_tls_version->count() - 1));
    ui->sub_http_version->setCurrentIndex(std::clamp(Configs::dataManager->settingsRepo->sub_http_version, 0, ui->sub_http_version->count() - 1));
    if (auto *model = qobject_cast<QStandardItemModel *>(ui->sub_tls_version->model()); model && !QSslSocket::isProtocolSupported(QSsl::TlsV1_3)) {
        model->item(static_cast<int>(Configs::subTlsVersion::tls13))->setEnabled(false);
    }
    D_LOAD_BOOL(net_use_proxy)
    D_LOAD_BOOL(allow_stopping_active_profile)
    D_LOAD_BOOL(sub_clear)
    D_LOAD_BOOL(sub_show_change_popup)
    D_LOAD_BOOL(net_insecure)
    D_LOAD_BOOL(sub_send_hwid)
    D_LOAD_STRING(sub_custom_hwid_params)
    D_LOAD_INT_ENABLE(sub_auto_update, sub_auto_update_enable)
    D_LOAD_BOOL(sub_respect_server_interval)
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
    ui->core_box_clash_listen_addr->setPlaceholderText(tr("127.0.0.1 (default)"));
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
    auto populateGeoAssetCombo = [](QComboBox *combo, bool geoip, const QString &current, const QStringList &history) {
        for (const auto &p: Configs::Xray::XrayGeoAssetProviders) {
            combo->addItem(geoip ? p.geoip : p.geosite);
            combo->setItemData(combo->count() - 1, p.name, Qt::ToolTipRole);
        }
        for (const auto &h: history) {
            if (combo->findText(h) < 0) combo->addItem(h);
        }
        const auto &fallback = Configs::Xray::XrayGeoAssetProviders.first();
        combo->lineEdit()->setPlaceholderText(geoip ? fallback.geoip : fallback.geosite);
        combo->setCurrentText(current);
    };
    populateGeoAssetCombo(ui->xray_geoip_url, true,
                          Configs::dataManager->settingsRepo->xray_geoip_url,
                          Configs::dataManager->settingsRepo->xray_geoip_url_history);
    populateGeoAssetCombo(ui->xray_geosite_url, false,
                          Configs::dataManager->settingsRepo->xray_geosite_url,
                          Configs::dataManager->settingsRepo->xray_geosite_url_history);

    ui->ntp_enable->setChecked(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_server->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_port->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_interval->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_outbound->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_server->setText(Configs::dataManager->settingsRepo->ntp_server_address);
    ui->ntp_port->setText(Int2String(Configs::dataManager->settingsRepo->ntp_server_port));
    ui->ntp_interval->setCurrentText(Configs::dataManager->settingsRepo->ntp_interval);
    ui->ntp_outbound->setCurrentText(Configs::dataManager->settingsRepo->ntp_outbound);
    connect(ui->ntp_enable, &QCheckBox::stateChanged, this, [=, this](const bool &state) {
        ui->ntp_server->setEnabled(state);
        ui->ntp_port->setEnabled(state);
        ui->ntp_interval->setEnabled(state);
        ui->ntp_outbound->setEnabled(state);
    });

    D_LOAD_BOOL(kill_switch)
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

    ui->tabWidget->addTab(new DiagnosticsTab(ui->tabWidget), tr("Diagnostics"));
    if (ui->random_listen_port->isChecked()) UiMode::Unmark(ui->random_listen_port);
    if (ui->disable_mixed_inbound->isChecked()) UiMode::Unmark(ui->disable_mixed_inbound);
    if (ui->enable_custom_icon->isChecked()) {
        for (QWidget *widget: {static_cast<QWidget *>(ui->enable_custom_icon), static_cast<QWidget *>(ui->select_custom_icon),
                               static_cast<QWidget *>(ui->follow_status_in_taskbar)}) {
            UiMode::Unmark(widget);
        }
    }
    UiMode::Apply(this);
    UiMode::Fold(this);
    bindValidation();

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
        // Ignored, like the field labels: a wrapping label otherwise asks for a width no dialog can give it.
        subtitleLabel->setWordWrap(true);
        subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        headingLayout->addWidget(subtitleLabel);
        layout->addWidget(heading);
        return section;
    };

    // Upstream ships diagnostics as a group box of plain buttons; move its controls into this dialog's cards.
    const auto dressDiagnosticsPage = [this, &makeSection, &makeFieldRow, &makeToggle](QWidget *page) {
        auto *root = qobject_cast<QVBoxLayout *>(page->layout());
        auto *group = page->findChild<QGroupBox *>(QStringLiteral("groupProfile"));
        auto *intro = page->findChild<QLabel *>(QStringLiteral("intro"));
        auto *start = page->findChild<QPushButton *>(QStringLiteral("startButton"));
        auto *stop = page->findChild<QPushButton *>(QStringLiteral("stopButton"));
        auto *progress = page->findChild<QProgressBar *>(QStringLiteral("progress"));
        auto *status = page->findChild<QLabel *>(QStringLiteral("status"));
        auto *showInFolder = page->findChild<QPushButton *>(QStringLiteral("showInFolder"));
        auto *openFolder = page->findChild<QPushButton *>(QStringLiteral("openFolder"));
        if (root == nullptr || group == nullptr || intro == nullptr || start == nullptr || stop == nullptr ||
            progress == nullptr || status == nullptr || showInFolder == nullptr || openFolder == nullptr) {
            qWarning() << "Diagnostics page layout changed upstream; leaving it undressed";
            return;
        }

        auto *section = makeSection(group->title(), intro->text());
        auto *layout = qobject_cast<QVBoxLayout *>(section->layout());
        for (const auto &name: {"contention", "trace", "includeLogs"}) {
            auto *box = page->findChild<QCheckBox *>(QLatin1String(name));
            if (box == nullptr) continue;
            // makeToggle reparents and hides the box, so its text and tip are read first.
            const QString title = box->text();
            const QString hint = box->toolTip();
            layout->addWidget(makeFieldRow(title, hint, makeToggle(box)));
        }

        const auto inlineHost = [](QWidget *parent) {
            auto *host = new QWidget(parent);
            host->setObjectName(QStringLiteral("settingsInlineControl"));
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(6);
            return host;
        };

        start->setObjectName(QStringLiteral("settingsPrimaryButton"));
        stop->setObjectName(QStringLiteral("settingsSecondaryButton"));
        auto *captureHost = inlineHost(section);
        auto *captureLayout = qobject_cast<QHBoxLayout *>(captureHost->layout());
        captureLayout->addWidget(progress, 1);
        captureLayout->addWidget(start);
        captureLayout->addWidget(stop);
        layout->addWidget(makeFieldRow(tr("Capture"), tr("The core keeps running while the profile is recorded."), captureHost));

        showInFolder->setObjectName(QStringLiteral("settingsSecondaryButton"));
        openFolder->setObjectName(QStringLiteral("settingsSecondaryButton"));
        auto *resultHost = inlineHost(section);
        auto *resultLayout = qobject_cast<QHBoxLayout *>(resultHost->layout());
        status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        resultLayout->addWidget(status, 1);
        resultLayout->addWidget(showInFolder);
        resultLayout->addWidget(openFolder);
        layout->addWidget(makeFieldRow(tr("Last profile"), QString(), resultHost));

        root->insertWidget(0, section);
        delete group;
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
    testingLayout->addWidget(makeFieldRow(tr("Direct test URL"),
                                          tr("Fetched without any proxy, so the auto selector can tell a dead connection from dead servers; empty uses the operating system's network state"),
                                          ui->direct_test_url));
    testingLayout->addWidget(makeFieldRow(tr("URL test timeout"), tr("Milliseconds before a latency test fails"), ui->url_timeout));
    testingLayout->addWidget(makeFieldRow(tr("UDP test target"),
                                          tr("host:port the UDP test queries; it has to answer DNS over UDP"),
                                          ui->udp_test_target));
    testingLayout->addWidget(makeFieldRow(tr("Concurrent tests"), {}, ui->test_concurrent));
    testingLayout->addWidget(makeFieldRow(tr("Speed test mode"), {}, ui->speedtest_mode));
    testingLayout->addWidget(makeFieldRow(tr("Speed test timeout"), {}, ui->test_timeout));
    testingLayout->addWidget(makeFieldRow(tr("Download test URL"), {}, ui->simple_down_url));
    // One "Name|URL" per line: the list is short and personal, so a text box beats a
    // table nobody wants to click through.
    siteTargetsEdit_ = new QPlainTextEdit(this);
    siteTargetsEdit_->setObjectName(QStringLiteral("siteTargetsEdit"));
    siteTargetsEdit_->setPlainText(Configs::dataManager->settingsRepo->site_test_targets.join('\n'));
    siteTargetsEdit_->setPlaceholderText(QStringLiteral("ChatGPT|https://chatgpt.com/"));
    siteTargetsEdit_->setFixedHeight(96);
    testingLayout->addWidget(makeFieldRow(tr("Sites to check"),
                                          tr("One \"Name|URL\" per line, checked by Site Reachability"),
                                          siteTargetsEdit_));
    siteTimeoutSpin_ = new QSpinBox(this);
    siteTimeoutSpin_->setObjectName(QStringLiteral("siteTimeoutSpin"));
    siteTimeoutSpin_->setRange(1000, 60000);
    siteTimeoutSpin_->setSingleStep(500);
    siteTimeoutSpin_->setSuffix(tr(" ms"));
    siteTimeoutSpin_->setValue(Configs::dataManager->settingsRepo->site_test_timeout_ms);
    testingLayout->addWidget(makeFieldRow(tr("Site check timeout"),
                                          tr("How long one site has to answer"),
                                          siteTimeoutSpin_));
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
                addControlRow(layout, ui->label_log_font->text(), ui->log_font_family);
                addControlRow(layout, ui->label_log_font_size->text(), ui->log_font_size);
                addControlRow(layout, tr("Max log lines"), ui->max_log_line);
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
                addToggleRow(layout, tr("Follow status icon in taskbar"), ui->follow_status_in_taskbar,
                             tr("Turn this off to keep the regular Throned icon while connected."));
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
                addToggleRow(layout, ui->disable_tray->text(), ui->disable_tray);
                addToggleRow(layout, tr("Delete profiles without confirmation"), ui->skip_delete_confirm);
                pageLayout->addWidget(window);
                break;
            }
            case 3: { // Subscription
                auto *updates = makeSection(tr("Automatic updates"), tr("Schedule subscription and routing-profile refreshes."));
                auto *layout = qobject_cast<QVBoxLayout *>(updates->layout());
                addControlRow(layout, tr("User agent"), ui->user_agent);
                addControlRow(layout, tr("TLS version"), ui->sub_tls_version, ui->sub_tls_version_l->toolTip());
                addControlRow(layout, tr("HTTP version"), ui->sub_http_version, ui->sub_http_version_l->toolTip());
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
                addToggleRow(layout, ui->kill_switch->text(), ui->kill_switch);
                pageLayout->addWidget(permissions);

                auto *urlScheme = makeSection(tr("URL scheme"), tr("Handler registration for throne:// links and config files."));
                layout = qobject_cast<QVBoxLayout *>(urlScheme->layout());
                addToggleRow(layout, ui->url_scheme_auto_register->text(), ui->url_scheme_auto_register);
                auto *schemeTools = new QWidget(this);
                auto *schemeLayout = new QHBoxLayout(schemeTools);
                schemeLayout->setContentsMargins(0, 0, 0, 0);
                schemeLayout->setSpacing(6);
                ui->url_scheme_install->setObjectName(QStringLiteral("settingsSecondaryButton"));
                ui->url_scheme_uninstall->setObjectName(QStringLiteral("settingsSecondaryButton"));
                schemeLayout->addWidget(ui->url_scheme_status);
                schemeLayout->addWidget(ui->url_scheme_install);
                schemeLayout->addWidget(ui->url_scheme_uninstall);
                addControlRow(layout, tr("Registration"), schemeTools);

                addToggleRow(layout, ui->file_assoc_auto_register->text(), ui->file_assoc_auto_register);
                auto *assocTools = new QWidget(this);
                auto *assocLayout = new QHBoxLayout(assocTools);
                assocLayout->setContentsMargins(0, 0, 0, 0);
                assocLayout->setSpacing(6);
                ui->file_assoc_install->setObjectName(QStringLiteral("settingsSecondaryButton"));
                ui->file_assoc_uninstall->setObjectName(QStringLiteral("settingsSecondaryButton"));
                assocLayout->addWidget(ui->file_assoc_status);
                assocLayout->addWidget(ui->file_assoc_install);
                assocLayout->addWidget(ui->file_assoc_uninstall);
                addControlRow(layout, tr("Config files"), assocTools);
                pageLayout->addWidget(urlScheme);

                auto *certificates = makeSection(tr("Certificates"), tr("Certificate stores and TLS validation defaults."));
                layout = qobject_cast<QVBoxLayout *>(certificates->layout());
                addToggle(layout, tr("Use Mozilla Certificate Store"), settings.use_mozilla_certs);
                addToggle(layout, tr("Trust System Certificates in Xray"), settings.xray_use_system_ca,
                          tr("Xray verifies servers only against its built-in root certificates. Turn this on to also trust the certificates installed in the system, such as a company or self-signed root. A profile or Xray config that sets useSystemCA itself keeps its own value."));
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
                if (qobject_cast<DiagnosticsTab *>(legacyPages[index]) != nullptr) dressDiagnosticsPage(legacyPages[index]);
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
        MaterialIcon::Glyph::Settings,
        MaterialIcon::Glyph::List,
        MaterialIcon::Glyph::Desktop,
        MaterialIcon::Glyph::Reload,
        MaterialIcon::Glyph::Tools,
        MaterialIcon::Glyph::Apps,
        MaterialIcon::Glyph::Folder,
        MaterialIcon::Glyph::Shield,
        MaterialIcon::Glyph::Pulse,
    };
    for (int index = 0; index < stack->count(); ++index) {
        const QString name = index < pageNames.size() ? pageNames[index] : tr("Settings");
        auto *button = new QPushButton(name, sidebar);
        button->setObjectName(QStringLiteral("settingsNav"));
        button->setCheckable(true);
        const auto glyph = pageIcons.value(index, MaterialIcon::Glyph::Settings);
        const auto refreshIcon = [button, glyph] {
            button->setIcon(MaterialIcon::icon(glyph, themeManager()->Colors().textMuted, 17));
        };
        refreshIcon();
        connect(themeManager(), &ThemeManager::themeChanged, button, refreshIcon);
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
/* Any QComboBox rule in this sheet makes Qt style every combo box here, so the plain
   ones need their own or they render as bare text with a floating arrow. */
QDialog#basicSettingsDialog QComboBox {
    color: #F1F3F5; background: #222529; border: 1px solid #3E454F;
    border-radius: 6px; padding: 5px 26px 5px 9px;
}
QDialog#basicSettingsDialog QComboBox:hover { background: #292D33; border-color: #4A4F57; }
QDialog#basicSettingsDialog QComboBox:focus, QDialog#basicSettingsDialog QComboBox:on { border-color: #237AE9; }
QDialog#basicSettingsDialog QComboBox:disabled { color: #747C86; background: #171B21; }
QDialog#basicSettingsDialog QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 24px; border: none; background: transparent;
}
QDialog#basicSettingsDialog QComboBox::down-arrow { image: url("%CHEVRON_DOWN%"); width: 14px; height: 14px; }
QDialog#basicSettingsDialog QComboBox QAbstractItemView {
    color: #F1F3F5; background: #222529; border: 1px solid #3E454F;
    selection-background-color: #237AE9; outline: none;
}
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
    themeManager()->RegisterStyle(this, settingsStyleTemplate);
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

void DialogBasicSettings::bindValidation() {
    using namespace Validation;
    const QStringList http = {QStringLiteral("http"), QStringLiteral("https")};
    const auto refreshOn = [this](QAbstractButton *toggle) { connect(toggle, &QAbstractButton::toggled, form, [this] { form->refresh(); }); };

    form->bind(ui->inbound_address, Rules::Ip());
    form->bind(ui->inbound_socks_port, Rules::Port());
    // Only the URL test treats 0 as its default; the download/upload test would time out at once.
    form->bind(ui->test_timeout, [](const QString &text) { return basicSettingsMillisError(text, 5000, false); }).label(tr("Speed test timeout"));
    form->bind(ui->simple_down_url, Rules::Url(http, Empty::Required)).label(ui->label_16->text()).when([this] { return ui->speedtest_mode->currentIndex() == 3; });
    connect(ui->speedtest_mode, &QComboBox::currentIndexChanged, form, [this] { form->refresh(); });
    form->bind(ui->test_latency_url, Rules::Url(http, Empty::Required)).label(ui->label_13->text());
    form->bind(ui->direct_test_url, Rules::Url(http)).label(ui->label_direct_test_url->text());
    form->bind(ui->test_concurrent, Rules::Int(0, 499, Empty::Required)).label(ui->label_14->text());
    form->bind(ui->url_timeout, [](const QString &text) { return basicSettingsMillisError(text, 3000, true); }).label(tr("URL test timeout"));

    struct PortUse {
        QLineEdit *edit;
        QString name;
        std::function<bool()> used;
    };
    const QList<PortUse> ports = {
        {ui->inbound_socks_port, tr("Mixed inbound"),
         [this] { return !ui->disable_mixed_inbound->isChecked() && !ui->random_listen_port->isChecked(); }},
        {ui->dns_in_port, tr("DNS server"), [] { return true; }},
        {ui->core_box_clash_api, tr("Clash API"), [this] { return !ui->core_box_clash_api->text().trimmed().isEmpty(); }},
        {ui->core_box_api_port, tr("sing-box API"), [this] { return !ui->core_box_api_port->text().trimmed().isEmpty(); }},
    };
    for (qsizetype i = 0; i < ports.size(); ++i) {
        for (qsizetype j = i + 1; j < ports.size(); ++j) {
            form->addRule({ports[i].edit, ports[j].edit}, [a = ports[i], b = ports[j]] {
                if (!a.used() || !b.used()) return QString();
                const auto port = ToInt(a.edit->text());
                if (!port || *port <= 0 || port != ToInt(b.edit->text())) return QString();
                return tr("Port %1 is already used by: %2. Choose a different port.").arg(QString::number(*port), b.name); }, nullptr, Severity::Error, ports[i].name);
        }
    }
    refreshOn(ui->disable_mixed_inbound);
    refreshOn(ui->random_listen_port);

    form->bind(ui->max_log_line, Rules::Int(1, std::numeric_limits<int>::max(), Empty::Required)).label(ui->label_17->text());
    for (QTextEdit *edit: {static_cast<QTextEdit *>(ui->log_include_regex), static_cast<QTextEdit *>(ui->log_exclude_regex)})
        form->addRule({edit}, [edit] {
            basicSettingsMarkRegexLines(edit);
            return basicSettingsRegexLinesError(edit->toPlainText());
        });

    form->bind(ui->user_agent, [](const QString &text) { return basicSettingsHeaderValueError(text); });
    form->bind(ui->sub_custom_hwid_params, [](const QString &text) { return basicSettingsHwidParamsError(text); });
    form->bind(ui->sub_auto_update, Rules::Int(kBasicSettingsMinAutoUpdate, kBasicSettingsMaxAutoUpdate, Empty::Required))
        .when([this] { return ui->sub_auto_update_enable->isChecked(); });
    form->bind(ui->route_auto_update, Rules::Int(kBasicSettingsMinAutoUpdate, kBasicSettingsMaxAutoUpdate, Empty::Required))
        .when([this] { return ui->route_auto_update_enable->isChecked(); });
    refreshOn(ui->sub_auto_update_enable);
    refreshOn(ui->route_auto_update_enable);

    form->bind(ui->dns_in_port, Rules::Port());
    const auto clashLabel = [this](const QLabel *label) { return QStringLiteral("%1 %2").arg(ui->clash_api_box->title(), label->text()); };
    form->bind(ui->core_box_clash_listen_addr, Rules::Host(Empty::Allowed)).label(clashLabel(ui->core_box_clash_listen_addr_l));
    form->bind(ui->core_box_clash_api, Rules::Port(Empty::Allowed)).label(clashLabel(ui->core_box_clash_api_l));
    form->bind(ui->core_box_api_port, Rules::Port(Empty::Allowed))
        .label(QStringLiteral("%1 %2").arg(ui->api_service_box->title(), ui->core_box_api_port_l->text()));

    form->bind(ui->ntp_server, Rules::Host(Empty::Allowed)).label(tr("NTP server"));
    // The core turns port 0 into 123.
    form->bind(ui->ntp_port, [](const QString &text) { return ToInt(text) == 0 ? QString() : Port(text, Empty::Allowed); }).label(tr("NTP port"));

    form->bind(ui->xray_geoip_url, Rules::Url(http)).label(ui->label_xray_geoip_url->text());
    form->bind(ui->xray_geosite_url, Rules::Url(http)).label(ui->label_xray_geosite_url->text());

    form->markAll();
}

QList<QWidget *> DialogBasicSettings::UnavailableWidgets(const Ui::DialogBasicSettings *ui) {
    QList<QWidget *> widgets;
#ifdef Q_OS_MACOS
    // LaunchServices registers the scheme from the bundle's Info.plist, so there is nothing of ours to add or take back.
    widgets << ui->url_scheme_install << ui->url_scheme_uninstall;
#endif
    if (!UrlScheme_IsSupported(Association::ConfigFiles)) {
        widgets << ui->file_assoc_auto_register << ui->file_assoc_status << ui->file_assoc_install << ui->file_assoc_uninstall;
    }
#ifndef Q_OS_WIN
    widgets << ui->proxy_scheme_l << ui->proxy_scheme;
#endif
    return widgets;
}

void DialogBasicSettings::refreshUrlSchemeStatus() {
    const auto colors = themeManager()->Colors();
    const auto show = [&](QLabel *status, Association a) {
        if (!UrlScheme_IsSupported(a)) {
            status->setText(tr("Not available for this installation"));
            status->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
            return;
        }
        const bool installed = UrlScheme_IsCurrent(a);
        if (!installed && !Configs::dataManager->settingsRepo->flag_use_appdata) {
            status->setText(tr("Not installed — a portable copy does not claim it on its own"));
            status->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
            return;
        }
        status->setText(installed ? tr("Installed") : tr("Not installed"));
        status->setStyleSheet(QStringLiteral("color: %1;").arg((installed ? colors.success : colors.textMuted).name()));
    };
    show(ui->url_scheme_status, Association::Links);
    show(ui->file_assoc_status, Association::ConfigFiles);
}

void DialogBasicSettings::applyRegexHighlighting() {
    basicSettingsMarkRegexLines(ui->log_include_regex);
    basicSettingsMarkRegexLines(ui->log_exclude_regex);
}

void DialogBasicSettings::applySelectedTheme() {
    const auto theme = ui->theme->currentText().trimmed();
    if (theme.isEmpty()) return;
    themeManager()->ApplyTheme(theme);
    Configs::dataManager->settingsRepo->theme = theme;
    Configs::dataManager->settingsRepo->Save();
    refreshUrlSchemeStatus();
}

void DialogBasicSettings::accept() {
    if (!form->validateAll()) return;
    bool needChoosePort = false;
    if (siteTargetsEdit_ != nullptr) {
        Configs::dataManager->settingsRepo->site_test_targets =
            SplitLines(siteTargetsEdit_->toPlainText());
    }
    if (siteTimeoutSpin_ != nullptr)
        Configs::dataManager->settingsRepo->site_test_timeout_ms = siteTimeoutSpin_->value();

    Configs::dataManager->settingsRepo->inbound_address = ui->inbound_address->text().trimmed();
    Configs::dataManager->settingsRepo->custom_inbound = CACHE.custom_inbound;
    D_SAVE_INT(inbound_socks_port)
    if (!Configs::dataManager->settingsRepo->random_inbound_port && ui->random_listen_port->isChecked()) {
        needChoosePort = true;
    }
    Configs::dataManager->settingsRepo->random_inbound_port = ui->random_listen_port->isChecked();
    D_SAVE_INT(test_concurrent)
    Configs::dataManager->settingsRepo->test_latency_url = ui->test_latency_url->text().trimmed();
    Configs::dataManager->settingsRepo->direct_test_url = ui->direct_test_url->text().trimmed();
    D_SAVE_BOOL(exit_country_lookup)
    Configs::dataManager->settingsRepo->exit_lookup_provider = ui->exit_lookup_provider->currentData().toString();
    D_SAVE_BOOL(disable_tray)
    Configs::dataManager->settingsRepo->proxy_scheme = ui->proxy_scheme->currentText().toLower();
    Configs::dataManager->settingsRepo->speed_test_mode = ui->speedtest_mode->currentIndex();
    Configs::dataManager->settingsRepo->simple_dl_url = ui->simple_down_url->text().trimmed();
    Configs::dataManager->settingsRepo->url_test_timeout_ms = ui->url_timeout->text().trimmed().toInt();
    if (const auto target = ui->udp_test_target->text().trimmed(); !target.isEmpty())
        Configs::dataManager->settingsRepo->udp_test_target = target;
    Configs::dataManager->settingsRepo->speed_test_timeout_ms = ui->test_timeout->text().trimmed().toInt();
    Configs::dataManager->settingsRepo->allow_beta_update = ui->allow_beta->isChecked();
    Configs::dataManager->settingsRepo->disable_mixed_inbound = ui->disable_mixed_inbound->isChecked();
    Configs::dataManager->settingsRepo->reset_proxy_on_disable_sp = ui->reset_proxy_on_disable_sp->isChecked();
    D_SAVE_BOOL(inbound_auth)
    D_SAVE_STRING(inbound_user)
    D_SAVE_STRING(inbound_pass)

    const bool registrationTurnedOn =
        (!Configs::dataManager->settingsRepo->url_scheme_auto_register && ui->url_scheme_auto_register->isChecked()) ||
        (!Configs::dataManager->settingsRepo->file_assoc_auto_register && ui->file_assoc_auto_register->isChecked());
    Configs::dataManager->settingsRepo->url_scheme_auto_register = ui->url_scheme_auto_register->isChecked();
    Configs::dataManager->settingsRepo->file_assoc_auto_register = ui->file_assoc_auto_register->isChecked();
    if (registrationTurnedOn) UrlScheme_RegisterIfNeeded();

    auto oldMaxLogLines = Configs::dataManager->settingsRepo->max_log_line;
    Configs::dataManager->settingsRepo->max_log_line = ui->max_log_line->text().trimmed().toInt();
    if (oldMaxLogLines != Configs::dataManager->settingsRepo->max_log_line) CACHE.updateMaxLogLines = true;
    Configs::dataManager->settingsRepo->log_level = ui->log_level->currentText().trimmed();
    Configs::dataManager->settingsRepo->xray_log_level = ui->xray_loglevel->currentText().trimmed();
    Configs::dataManager->settingsRepo->log_enable_include = ui->enable_log_include->isChecked();
    Configs::dataManager->settingsRepo->log_enable_exclude = ui->enable_log_exclude->isChecked();
    Configs::dataManager->settingsRepo->log_include_keyword = SplitAndTrim(ui->log_include_keyword->toPlainText(), "\n", false);
    Configs::dataManager->settingsRepo->log_exclude_keyword = SplitAndTrim(ui->log_exclude_keyword->toPlainText(), "\n", false);

    Configs::dataManager->settingsRepo->log_include_regex = SplitAndTrim(ui->log_include_regex->toPlainText(), "\n", false);
    Configs::dataManager->settingsRepo->log_exclude_regex = SplitAndTrim(ui->log_exclude_regex->toPlainText(), "\n", false);

    Configs::dataManager->settingsRepo->enable_stats = ui->connection_statistics->isChecked();
    Configs::dataManager->settingsRepo->disable_traffic_aggregation = ui->disable_traffic_aggregation->isChecked();
    Configs::dataManager->settingsRepo->language = ui->language->currentIndex();
    const bool simpleMode = ui->ui_mode->currentIndex() == 0;
    const bool uiModeChanged = Configs::dataManager->settingsRepo->simple_interface != simpleMode;
    Configs::dataManager->settingsRepo->simple_interface = simpleMode;
    auto oldUseCustomIcon = Configs::dataManager->settingsRepo->use_custom_icons;
    Configs::dataManager->settingsRepo->use_custom_icons = ui->enable_custom_icon->isChecked();
    if (oldUseCustomIcon != Configs::dataManager->settingsRepo->use_custom_icons) CACHE.updateTrayIcon = true;
    auto oldFollowStatusInTaskbar = Configs::dataManager->settingsRepo->follow_status_in_taskbar;
    Configs::dataManager->settingsRepo->follow_status_in_taskbar = ui->follow_status_in_taskbar->isChecked();
    if (oldFollowStatusInTaskbar != Configs::dataManager->settingsRepo->follow_status_in_taskbar) CACHE.updateTrayIcon = true;
    D_SAVE_BOOL(start_minimal)
    Configs::dataManager->settingsRepo->skip_delete_confirmation = ui->skip_delete_confirm->isChecked();
    bool profileListDisplayChanged =
        Configs::dataManager->settingsRepo->show_config_security != ui->show_config_security->isChecked();
    D_SAVE_BOOL(show_config_security)

    QString logFontFamily = ui->log_font_family->currentFont().family();
    if (Configs::dataManager->settingsRepo->log_font_family.isEmpty() && logFontFamily == CACHE.shownLogFontFamily) logFontFamily.clear();
    int logFontSize = ui->log_font_size->currentText().toInt();
    if (Configs::dataManager->settingsRepo->log_font_size <= 0 && logFontSize == CACHE.shownLogFontSize) logFontSize = 0;
    const bool logFontChanged = logFontFamily != Configs::dataManager->settingsRepo->log_font_family ||
                                logFontSize != Configs::dataManager->settingsRepo->log_font_size;
    Configs::dataManager->settingsRepo->log_font_family = logFontFamily;
    Configs::dataManager->settingsRepo->log_font_size = logFontSize;

    // The PeriodicRunner reads these intervals live; no timer needs restarting.

    Configs::dataManager->settingsRepo->user_agent = ui->user_agent->text().trimmed();
    Configs::dataManager->settingsRepo->sub_tls_version = ui->sub_tls_version->currentIndex();
    Configs::dataManager->settingsRepo->sub_http_version = ui->sub_http_version->currentIndex();
    D_SAVE_BOOL(net_use_proxy)
    D_SAVE_BOOL(allow_stopping_active_profile)
    D_SAVE_BOOL(sub_clear)
    D_SAVE_BOOL(sub_show_change_popup)
    D_SAVE_BOOL(net_insecure)
    D_SAVE_BOOL(sub_send_hwid)
    Configs::dataManager->settingsRepo->sub_custom_hwid_params = ui->sub_custom_hwid_params->text().trimmed();
    D_SAVE_INT_ENABLE(sub_auto_update, sub_auto_update_enable)
    D_SAVE_BOOL(sub_respect_server_interval)
    D_SAVE_INT_ENABLE(route_auto_update, route_auto_update_enable)
    D_SAVE_INT_ENABLE(app_auto_update, app_auto_update_enable)

    Configs::dataManager->settingsRepo->disable_traffic_stats = ui->disable_stats->isChecked();
    Configs::dataManager->settingsRepo->core_dns_in_port = ui->dns_in_port->text().trimmed().toInt();
    Configs::dataManager->settingsRepo->core_box_clash_listen_addr = ui->core_box_clash_listen_addr->text().trimmed();
    Configs::dataManager->settingsRepo->core_box_clash_api = ui->core_box_clash_api->text().trimmed().toInt();
    Configs::dataManager->settingsRepo->core_box_clash_api_secret = ui->core_box_clash_api_secret->text().trimmed();
    Configs::dataManager->settingsRepo->core_box_api_port = ui->core_box_api_port->text().trimmed().toInt();
    // Blank means "no authentication" to sing-box, so never let the field clear it.
    if (const auto secret = ui->core_box_api_secret->text().trimmed(); !secret.isEmpty())
        Configs::dataManager->settingsRepo->core_box_api_secret = secret;

    Configs::dataManager->settingsRepo->xray_vless_preference = static_cast<Configs::Xray::XrayVlessPreference>(ui->vless_xray_pref->currentIndex());
    // An empty field means the provider shown as its placeholder; the download at profile start has no fallback of its own.
    const auto geoAssetUrl = [](const QComboBox *combo) {
        const auto url = combo->currentText().trimmed();
        return url.isEmpty() ? combo->lineEdit()->placeholderText() : url;
    };
    Configs::dataManager->settingsRepo->xray_geoip_url = geoAssetUrl(ui->xray_geoip_url);
    Configs::dataManager->settingsRepo->xray_geosite_url = geoAssetUrl(ui->xray_geosite_url);

    Configs::dataManager->settingsRepo->enable_ntp = ui->ntp_enable->isChecked();
    Configs::dataManager->settingsRepo->ntp_server_address = ui->ntp_server->text().trimmed();
    Configs::dataManager->settingsRepo->ntp_server_port = ui->ntp_port->text().trimmed().toInt();
    Configs::dataManager->settingsRepo->ntp_interval = ui->ntp_interval->currentText().trimmed();
    Configs::dataManager->settingsRepo->ntp_outbound = ui->ntp_outbound->currentText().trimmed();

    // Security

    // Has to be asked before the bindings overwrite the stored value.
    if (alwaysStandardUser_ != nullptr && Configs::dataManager->settingsRepo->disable_run_admin != alwaysStandardUser_->isChecked()) {
        CACHE.updateDisableAdmin = true;
    }
    const bool previousSkipCert = Configs::dataManager->settingsRepo->skip_cert;
    const bool killSwitchChanged = Configs::dataManager->settingsRepo->kill_switch != ui->kill_switch->isChecked();
    D_SAVE_BOOL(kill_switch)
    bindings_.save();
    profileListDisplayChanged |= previousSkipCert != Configs::dataManager->settingsRepo->skip_cert;

    // A quick Save can arrive before the preview debounce fires. Flush it while
    // the dialog and its combo still exist instead of silently losing the choice.
    if (themeApplyDebounce_ != nullptr && themeApplyDebounce_->isActive()) {
        themeApplyDebounce_->stop();
        applySelectedTheme();
    }

    QStringList changes;
    if (CACHE.needRestart) changes << MwArg::NeedRestart;
    if (CACHE.updateDisableTray) changes << MwArg::DisableTray;
    if (CACHE.updateTrayIcon) changes << MwArg::TrayIcon;
    if (CACHE.updateMaxLogLines) changes << MwArg::MaxLogLines;
    if (CACHE.updateDisableAdmin) changes << MwArg::DisableAdmin;
    if (needChoosePort) changes << MwArg::ChoosePort;
    if (profileListDisplayChanged) changes << MwArg::ProfileListDisplay;
    if (killSwitchChanged) changes << MwArg::KillSwitch;
    if (logFontChanged) changes << MwArg::LogFont;
    if (uiModeChanged) changes << MwArg::UiMode;
    MW_dialog_message(MwMessage::UpdateSettings, changes);
    QDialog::accept();
}

void DialogBasicSettings::downloadXrayGeoAsset(const QString &url, const QString &fileName) {
    const QString effectiveUrl = url.trimmed();
    if (effectiveUrl.isEmpty()) {
        QMessageBox::warning(this, tr("Download geo asset"),
                             tr("Please enter a URL for %1 first.").arg(fileName));
        return;
    }

    Engine::AssetUpdates::GeoRequest request;
    if (fileName == QLatin1String("geoip.dat")) {
        request.files = {Engine::AssetUpdates::GeoFile::GeoIp};
        request.geoipUrl = effectiveUrl;
    } else {
        request.files = {Engine::AssetUpdates::GeoFile::GeoSite};
        request.geositeUrl = effectiveUrl;
    }
    // Don't capture the dialog, it may close first.
    const bool started = Engine::AssetUpdates::UpdateGeoAssets(request, [fileName](const QList<Engine::AssetUpdates::GeoFileResult> &results) {
        const QString err = results.isEmpty() ? QString() : results.first().error;
        if (!results.isEmpty() && err.isEmpty()) {
            QMessageBox::information(GetMainWindow(), QObject::tr("Download geo asset"),
                                     QObject::tr("%1 was downloaded successfully.").arg(fileName));
        } else {
            MessageBoxWarning(QObject::tr("Download geo asset"),
                              QObject::tr("Failed to download %1:\n%2").arg(fileName, err));
        }
    });
    // Refused only while this same file is downloading; the other file may download alongside.
    if (!started) {
        QMessageBox::warning(this, tr("Download geo asset"), tr("A geo asset download is already running."));
        return;
    }
    MW_show_log(tr("Downloading Xray geo asset: %1").arg(fileName));
}

void DialogBasicSettings::rememberGeoAssetUrl(QComboBox *combo, const QString &url, QStringList &history) {
    if (url.isEmpty()) return;
    for (const auto &p: Configs::Xray::XrayGeoAssetProviders) {
        if (url == p.geoip || url == p.geosite) return;
    }

    history.removeAll(url);
    history.prepend(url);
    while (history.size() > 5) history.removeLast();

    // Providers occupy the head of the combo, so everything after them is history that may have aged out.
    for (int i = combo->count() - 1; i >= static_cast<int>(Configs::Xray::XrayGeoAssetProviders.size()); --i) {
        if (!history.contains(combo->itemText(i))) combo->removeItem(i);
    }
    if (combo->findText(url) < 0) combo->addItem(url);
    combo->setCurrentText(url);

    Configs::dataManager->settingsRepo->Save();
}

void DialogBasicSettings::on_xray_geoip_download_clicked() {
    QString url = ui->xray_geoip_url->currentText().trimmed();
    if (url.isEmpty()) url = ui->xray_geoip_url->lineEdit()->placeholderText();
    rememberGeoAssetUrl(ui->xray_geoip_url, url, Configs::dataManager->settingsRepo->xray_geoip_url_history);
    downloadXrayGeoAsset(url, "geoip.dat");
}

void DialogBasicSettings::on_xray_geosite_download_clicked() {
    QString url = ui->xray_geosite_url->currentText().trimmed();
    if (url.isEmpty()) url = ui->xray_geosite_url->lineEdit()->placeholderText();
    rememberGeoAssetUrl(ui->xray_geosite_url, url, Configs::dataManager->settingsRepo->xray_geosite_url_history);
    downloadXrayGeoAsset(url, "geosite.dat");
}

void DialogBasicSettings::on_backup_create_clicked() {
    if (webDavBusy) return;
    const Configs::BackupParts parts = basicSettingsCheckedParts(ui);

    if (!parts.any()) {
        QMessageBox::warning(this, tr("Create Backup"),
                             tr("Select at least one part to include in the backup."));
        return;
    }

    Engine::BackupArchive::Prepare(parts);

    QString filePath = NativeFileDialog::getSaveFileName(
        this,
        tr("Create Backup"),
        QDir::homePath() + "/Throned-backup.thrbackup",
        tr("Throned Backup (*.thrbackup)"));
    if (filePath.isEmpty()) return;

    if (const auto error = Engine::BackupArchive::Build(parts, filePath); !error.isEmpty()) {
        QMessageBox::critical(this, tr("Backup Failed"), basicSettingsBackupError(error));
        return;
    }

    QStringList included;
    if (parts.profiles) included << tr("Profiles");
    if (parts.routes) included << tr("Routing profiles");
    if (parts.settings) included << tr("Settings");
    if (parts.otp) included << tr("OTP profiles");
    if (parts.ipLists) included << tr("IP lists and scans");
    if (parts.icons) included << tr("Custom icons");

    QMessageBox::information(this, tr("Backup Created"),
                             tr("Backup created successfully:\n%1\n\nIncluded: %2")
                                 .arg(filePath, included.join(", ")));
}

void DialogBasicSettings::on_backup_restore_clicked() {
    if (webDavBusy) return;
    QString filePath = NativeFileDialog::getOpenFileName(
        this,
        tr("Restore Backup"),
        QDir::homePath(),
        tr("Throned Backup (*.thrbackup)"));
    if (filePath.isEmpty()) return;
    restoreBackupFile(filePath, {});
}

void DialogBasicSettings::restoreBackupFile(const QString &filePath, const QString &fallbackDeviceName) {
    Engine::BackupArchive::Info info;
    if (const auto error = Engine::BackupArchive::Inspect(filePath, &info); !error.isEmpty()) {
        QMessageBox::critical(this, tr("Restore Failed"), basicSettingsBackupError(error));
        return;
    }
    const Configs::BackupParts avail = info.available;
    const QString createdAt = info.createdAt;
    const QString createdBy = basicSettingsRemoteText(info.deviceName.isEmpty() ? fallbackDeviceName : info.deviceName);

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Restore Backup"));
    auto *layout = new QVBoxLayout(&dlg);
    if (!createdBy.isEmpty()) {
        auto *creator = new QLabel(tr("Created by: %1").arg(createdBy), &dlg);
        creator->setTextFormat(Qt::PlainText);
        creator->setWordWrap(true);
        layout->addWidget(creator);
    }
    auto *header = new QLabel(
        tr("Backup created on %1.\nSelect which parts to restore:")
            .arg(createdAt.isEmpty() ? tr("unknown date") : createdAt),
        &dlg);
    header->setTextFormat(Qt::PlainText);
    header->setWordWrap(true);
    layout->addWidget(header);

    auto *cbProfiles = new QCheckBox(tr("Profiles (groups and proxies)"), &dlg);
    auto *cbRoutes = new QCheckBox(tr("Routing profiles"), &dlg);
    auto *cbSettings = new QCheckBox(tr("Settings"), &dlg);
    auto *cbOtp = new QCheckBox(tr("OTP profiles"), &dlg);
    auto *cbIpLists = new QCheckBox(tr("IP lists and scans"), &dlg);
    auto *cbIcons = new QCheckBox(tr("Custom icons"), &dlg);
    for (auto *cb: {cbProfiles, cbRoutes, cbSettings, cbOtp, cbIpLists, cbIcons}) cb->setChecked(true);
    cbProfiles->setEnabled(avail.profiles);
    cbProfiles->setChecked(avail.profiles);
    cbRoutes->setEnabled(avail.routes);
    cbRoutes->setChecked(avail.routes);
    cbSettings->setEnabled(avail.settings);
    cbSettings->setChecked(avail.settings);
    cbOtp->setEnabled(avail.otp);
    cbOtp->setChecked(avail.otp);
    cbIpLists->setEnabled(avail.ipLists);
    cbIpLists->setChecked(avail.ipLists);
    cbIcons->setEnabled(avail.icons);
    cbIcons->setChecked(avail.icons);
    layout->addWidget(cbProfiles);
    layout->addWidget(cbRoutes);
    layout->addWidget(cbSettings);
    layout->addWidget(cbOtp);
    layout->addWidget(cbIpLists);
    layout->addWidget(cbIcons);

    auto *warn = new QLabel(
        tr("Each selected part replaces the current data. This cannot be undone.\n"
           "Throned will restart to complete the restore."),
        &dlg);
    warn->setWordWrap(true);
    layout->addWidget(warn);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
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
    chosen.ipLists = avail.ipLists && cbIpLists->isChecked();
    chosen.icons = avail.icons && cbIcons->isChecked();

    if (!chosen.any()) {
        QMessageBox::warning(this, tr("Restore Backup"),
                             tr("Select at least one part to restore."));
        return;
    }

    // Apply keeps the device-local settings; the window restores every other one, only the API also keeps this host's keys.
    Engine::BackupArchive::ApplyResult result;
    if (const auto error = Engine::BackupArchive::Apply(filePath, chosen, {}, &result); !error.isEmpty()) {
        QMessageBox::critical(this, tr("Restore Failed"), basicSettingsBackupError(error));
        return;
    }
    const int skippedRules = result.skippedRules;

    QString done = tr("Backup restored successfully. Throned will now restart for the changes to take effect.");
    if (skippedRules > 0)
        done += "\n\n" + tr("Skipped %n routing rule(s) that use conditions this version of Throned does not support.", nullptr, skippedRules);
    QMessageBox::information(this, tr("Restore Complete"), done);
    MW_dialog_message(MwMessage::RestartProgram, {});
    QDialog::reject();
}

bool DialogBasicSettings::webDavReady(const QString &title) {
    if (webDavBusy) return false;
    if (!Configs::dataManager->settingsRepo->webdav_server.trimmed().isEmpty()) return true;
    QMessageBox::information(this, title, tr("Enter your WebDAV server in WebDAV Settings first."));
    showWebDavSettings();
    return false;
}

void DialogBasicSettings::on_webdav_settings_clicked() {
    if (webDavBusy) return;
    showWebDavSettings();
}

void DialogBasicSettings::showWebDavSettings() {
    const auto &settings = *Configs::dataManager->settingsRepo;
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("WebDAV Settings"));
    auto *layout = new QVBoxLayout(dlg);
    auto *form = new QFormLayout();
    auto *server = new QLineEdit(settings.webdav_server, dlg);
    server->setPlaceholderText(QStringLiteral("https://cloud.example.com/remote.php/dav/files/me/"));
    server->setMinimumWidth(server->fontMetrics().horizontalAdvance(server->placeholderText()) + 24);
    auto *user = new QLineEdit(settings.webdav_user, dlg);
    auto *pass = new QLineEdit(settings.webdav_pass, dlg);
    pass->setEchoMode(QLineEdit::Password);
    auto *folder = new QLineEdit(settings.webdav_path, dlg);
    folder->setPlaceholderText(QStringLiteral("Throned"));
    auto *device = new QLineEdit(settings.device_name, dlg);
    device->setPlaceholderText(QSysInfo::machineHostName());
    form->addRow(tr("Server URL"), server);
    form->addRow(tr("Username"), user);
    form->addRow(tr("Password"), pass);
    form->addRow(tr("Folder"), folder);
    form->addRow(tr("Device name"), device);
    layout->addLayout(form);
    auto *useProxy = new QCheckBox(tr("Connect through the proxy"), dlg);
    useProxy->setChecked(settings.webdav_use_proxy);
    layout->addWidget(useProxy);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dlg);
    auto *test = buttons->addButton(tr("Test"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    const auto fields = [server, user, pass, folder, useProxy] {
        Engine::WebDavBackup::Config config;
        config.server = server->text().trimmed();
        config.user = user->text().trimmed();
        config.pass = pass->text();
        config.path = folder->text().trimmed();
        config.useProxy = useProxy->isChecked();
        return config;
    };
    connect(test, &QPushButton::clicked, dlg, [this, dlg, test, fields] {
        if (webDavBusy) return;
        webDavBusy = true;
        test->setEnabled(false);
        const QPointer<DialogBasicSettings> self(this);
        const QPointer<QDialog> target(dlg);
        const QPointer<QPushButton> button(test);
        const auto progress = basicSettingsWebDavProgress(dlg, tr("Testing the connection..."));
        runOnNewThread([self, target, button, progress, config = fields()] {
            const QString error = Engine::WebDavBackup::Test(config);
            runOnUiThread([self, target, button, progress, error] {
                if (progress) progress->close();
                if (self) self->webDavBusy = false;
                if (button) button->setEnabled(true);
                if (!target) return;
                if (error.isEmpty()) {
                    basicSettingsWebDavMessage(target, QMessageBox::Information, tr("WebDAV Settings"), tr("Connected. The backup folder is ready."));
                } else {
                    basicSettingsWebDavMessage(target, QMessageBox::Warning, tr("WebDAV Settings"), tr("Connection failed:\n%1").arg(error));
                }
            });
        });
    });
    connect(buttons, &QDialogButtonBox::accepted, dlg, [dlg, fields, device] {
        const auto config = fields();
        const QString scheme = QUrl(config.server).scheme().toLower();
        if (!config.server.isEmpty() && scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
            basicSettingsWebDavMessage(dlg, QMessageBox::Warning, tr("WebDAV Settings"), tr("The server URL must start with http:// or https://."));
            return;
        }
        auto &saved = *Configs::dataManager->settingsRepo;
        saved.webdav_server = config.server;
        saved.webdav_user = config.user;
        saved.webdav_pass = config.pass;
        saved.webdav_path = config.path;
        saved.webdav_use_proxy = config.useProxy;
        saved.device_name = device->text().trimmed();
        saved.Save();
        dlg->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    dlg->open();
}

void DialogBasicSettings::on_webdav_upload_clicked() {
    const QString title = tr("Upload to WebDAV");
    if (!webDavReady(title)) return;
    const Configs::BackupParts parts = basicSettingsCheckedParts(ui);
    if (!parts.any()) {
        QMessageBox::warning(this, title, tr("Select at least one part to include in the backup."));
        return;
    }

    Engine::BackupArchive::Prepare(parts);
    const auto config = Engine::WebDavBackup::FromSettings();
    const QString tempPath = Engine::BackupArchive::TempPath(QStringLiteral("webdav-"));
    webDavBusy = true;
    const QPointer<DialogBasicSettings> self(this);
    const auto progress = basicSettingsWebDavProgress(this, tr("Uploading the backup..."));
    runOnNewThread([self, progress, parts, config, tempPath, title] {
        Configs::BackupParts written;
        QString fileName;
        QString indexWarning;
        QString error = Engine::BackupArchive::Build(parts, tempPath, &written);
        const bool built = error.isEmpty();
        if (built) {
            error = Engine::WebDavBackup::Upload(config, tempPath, Engine::BackupArchive::PartNames(written), &fileName, &indexWarning);
        }
        QFile::remove(tempPath);
        runOnUiThread([self, progress, title, built, error, fileName, indexWarning] {
            if (progress) progress->close();
            if (!self) return;
            self->webDavBusy = false;
            if (!error.isEmpty()) {
                basicSettingsWebDavMessage(self, QMessageBox::Critical, title,
                                           built ? tr("Upload failed:\n%1").arg(error) : basicSettingsBackupError(error));
                return;
            }
            if (indexWarning.isEmpty()) {
                basicSettingsWebDavMessage(self, QMessageBox::Information, title, tr("Backup uploaded:\n%1").arg(fileName));
            } else {
                basicSettingsWebDavMessage(self, QMessageBox::Warning, title,
                                           tr("Backup uploaded, but the backup list could not be updated: %1").arg(indexWarning));
            }
        });
    });
}

void DialogBasicSettings::on_webdav_restore_clicked() {
    if (!webDavReady(tr("Restore from WebDAV"))) return;
    loadWebDavBackups(nullptr);
}

void DialogBasicSettings::loadWebDavBackups(const std::shared_ptr<WebDavChooser> &chooser, const QString &notice) {
    const auto config = Engine::WebDavBackup::FromSettings();
    webDavBusy = true;
    const QPointer<DialogBasicSettings> self(this);
    QWidget *parent = this;
    if (chooser && chooser->dialog) parent = chooser->dialog;
    const auto progress = basicSettingsWebDavProgress(parent, tr("Loading the backup list..."));
    runOnNewThread([self, chooser, progress, config, notice] {
        QList<Engine::WebDavBackup::Entry> entries;
        QString indexWarning;
        const QString error = Engine::WebDavBackup::List(config, &entries, &indexWarning);
        runOnUiThread([self, chooser, progress, notice, entries, indexWarning, error] {
            if (progress) progress->close();
            if (!self) return;
            self->webDavBusy = false;
            if (chooser && !chooser->dialog) return;
            if (!error.isEmpty()) {
                QString text = tr("Could not list the backups:\n%1").arg(error);
                if (!notice.isEmpty()) text += QStringLiteral("\n\n") + notice;
                basicSettingsWebDavMessage(chooser ? static_cast<QWidget *>(chooser->dialog) : self.data(), QMessageBox::Critical,
                                           tr("Restore from WebDAV"), text);
                return;
            }
            if (!chooser) {
                self->showWebDavBackups(entries, indexWarning);
                return;
            }
            QStringList note;
            if (!notice.isEmpty()) note << notice;
            if (!indexWarning.isEmpty()) note << tr("The backup list could not be read: %1").arg(indexWarning);
            self->fillWebDavChooser(*chooser, entries, note.join(QLatin1Char('\n')));
        });
    });
}

void DialogBasicSettings::showWebDavBackups(const QList<Engine::WebDavBackup::Entry> &entries, const QString &indexWarning) {
    const QString title = tr("Restore from WebDAV");
    if (entries.isEmpty()) {
        basicSettingsWebDavMessage(this, QMessageBox::Information, title, tr("There are no backups in the WebDAV folder yet."));
        return;
    }

    auto chooser = std::make_shared<WebDavChooser>();
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(title);
    chooser->dialog = dlg;
    auto *layout = new QVBoxLayout(dlg);
    chooser->tree = new QTreeWidget(dlg);
    chooser->tree->setRootIsDecorated(false);
    chooser->tree->setUniformRowHeights(true);
    chooser->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    chooser->tree->setHeaderLabels({tr("Created"), tr("Device"), tr("Platform"), tr("Version"), tr("Parts"), tr("Size")});
    layout->addWidget(chooser->tree);
    chooser->note = new QLabel(dlg);
    chooser->note->setTextFormat(Qt::PlainText);
    chooser->note->setWordWrap(true);
    layout->addWidget(chooser->note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    chooser->restore = buttons->addButton(tr("Restore"), QDialogButtonBox::ActionRole);
    chooser->remove = buttons->addButton(tr("Delete"), QDialogButtonBox::ActionRole);
    chooser->restore->setDefault(true);
    layout->addWidget(buttons);

    connect(chooser->tree, &QTreeWidget::currentItemChanged, dlg, [chooser](QTreeWidgetItem *current) {
        chooser->restore->setEnabled(current != nullptr);
        chooser->remove->setEnabled(current != nullptr);
    });
    connect(chooser->tree, &QTreeWidget::itemDoubleClicked, dlg, [this, chooser] { restoreWebDavBackup(chooser); });
    connect(chooser->restore, &QPushButton::clicked, dlg, [this, chooser] { restoreWebDavBackup(chooser); });
    connect(chooser->remove, &QPushButton::clicked, dlg, [this, chooser] { deleteWebDavBackup(chooser); });
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    fillWebDavChooser(*chooser, entries, indexWarning.isEmpty() ? QString() : tr("The backup list could not be read: %1").arg(indexWarning));
    int width = 2 * chooser->tree->frameWidth() + chooser->tree->verticalScrollBar()->sizeHint().width() +
                layout->contentsMargins().left() + layout->contentsMargins().right();
    for (int column = 0; column < chooser->tree->columnCount(); ++column) {
        chooser->tree->resizeColumnToContents(column);
        width += chooser->tree->columnWidth(column);
    }
    QSize size = dlg->sizeHint().expandedTo(QSize(width, 0));
    if (const QScreen *scr = screen()) size = size.boundedTo(scr->availableGeometry().size() - QSize(48, 72));
    dlg->resize(size);
    dlg->open();
}

void DialogBasicSettings::fillWebDavChooser(WebDavChooser &chooser, const QList<Engine::WebDavBackup::Entry> &entries, const QString &note) {
    const QString thisDevice = Engine::BackupArchive::ThisDevice().id;
    const QLocale locale;
    chooser.entries = entries;
    chooser.tree->clear();
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        QString device = basicSettingsRemoteText(entry.deviceName);
        if (device.isEmpty()) device = tr("Unknown device");
        if (!entry.deviceId.isEmpty() && entry.deviceId == thisDevice) device = tr("%1 (this device)").arg(device);
        auto *item = new QTreeWidgetItem(chooser.tree);
        item->setText(0, entry.createdAt.isValid() ? locale.toString(entry.createdAt.toLocalTime(), QLocale::ShortFormat) : tr("Unknown"));
        item->setText(1, device);
        item->setText(2, basicSettingsPlatformName(entry.platform));
        item->setText(3, basicSettingsRemoteText(entry.appVersion));
        item->setText(4, basicSettingsPartsText(entry.parts));
        item->setText(5, entry.size >= 0 ? locale.formattedDataSize(entry.size) : QString());
        item->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, static_cast<int>(i));
    }
    chooser.note->setText(note);
    chooser.note->setVisible(!note.isEmpty());
    const bool any = chooser.tree->topLevelItemCount() > 0;
    if (any) chooser.tree->setCurrentItem(chooser.tree->topLevelItem(0));
    chooser.restore->setEnabled(any);
    chooser.remove->setEnabled(any);
}

void DialogBasicSettings::restoreWebDavBackup(const std::shared_ptr<WebDavChooser> &chooser) {
    if (webDavBusy || !chooser->dialog) return;
    const auto *item = chooser->tree->currentItem();
    const int row = item != nullptr ? item->data(0, Qt::UserRole).toInt() : -1;
    if (row < 0 || row >= chooser->entries.size()) return;
    const auto entry = chooser->entries.at(row);

    const auto config = Engine::WebDavBackup::FromSettings();
    const QString tempPath = Engine::BackupArchive::TempPath(QStringLiteral("webdav-"));
    webDavBusy = true;
    const QPointer<DialogBasicSettings> self(this);
    const QPointer<QDialog> dialog(chooser->dialog);
    const auto progress = basicSettingsWebDavProgress(chooser->dialog, tr("Downloading the backup..."));
    runOnNewThread([self, dialog, progress, config, entry, tempPath] {
        const QString error = Engine::WebDavBackup::Download(config, entry, tempPath);
        runOnUiThread([self, dialog, progress, entry, tempPath, error] {
            if (progress) progress->close();
            if (!self) {
                QFile::remove(tempPath);
                return;
            }
            self->webDavBusy = false;
            if (!error.isEmpty()) {
                basicSettingsWebDavMessage(dialog ? static_cast<QWidget *>(dialog) : self.data(), QMessageBox::Critical,
                                           tr("Restore from WebDAV"), tr("Could not download the backup:\n%1").arg(error));
                return;
            }
            if (dialog) dialog->close();
            self->restoreBackupFile(tempPath, entry.indexed ? entry.deviceName : QString());
            QFile::remove(tempPath);
        });
    });
}

void DialogBasicSettings::deleteWebDavBackup(const std::shared_ptr<WebDavChooser> &chooser) {
    if (webDavBusy || !chooser->dialog) return;
    const auto *item = chooser->tree->currentItem();
    const int row = item != nullptr ? item->data(0, Qt::UserRole).toInt() : -1;
    if (row < 0 || row >= chooser->entries.size()) return;
    const auto entry = chooser->entries.at(row);

    auto *confirm = new QMessageBox(QMessageBox::Question, tr("Delete Backup"),
                                    tr("Delete this backup from the WebDAV server? This cannot be undone.\n\n%1").arg(basicSettingsRemoteText(entry.file)),
                                    QMessageBox::Yes | QMessageBox::No, chooser->dialog.data());
    confirm->setTextFormat(Qt::PlainText);
    confirm->setDefaultButton(QMessageBox::No);
    confirm->setAttribute(Qt::WA_DeleteOnClose);
    connect(confirm->button(QMessageBox::Yes), &QAbstractButton::clicked, chooser->dialog.data(), [this, chooser, entry] {
        if (webDavBusy || !chooser->dialog) return;
        const auto config = Engine::WebDavBackup::FromSettings();
        webDavBusy = true;
        const QPointer<DialogBasicSettings> self(this);
        const auto progress = basicSettingsWebDavProgress(chooser->dialog, tr("Deleting the backup..."));
        runOnNewThread([self, chooser, progress, config, entry] {
            QString indexWarning;
            const QString error = Engine::WebDavBackup::Delete(config, entry, &indexWarning);
            runOnUiThread([self, chooser, progress, error, indexWarning] {
                if (progress) progress->close();
                if (!self) return;
                self->webDavBusy = false;
                if (!chooser->dialog) return;
                if (!error.isEmpty()) {
                    basicSettingsWebDavMessage(chooser->dialog, QMessageBox::Critical, tr("Delete Backup"),
                                               tr("Could not delete the backup:\n%1").arg(error));
                    return;
                }
                self->loadWebDavBackups(chooser, indexWarning.isEmpty() ? QString()
                                                                        : tr("Backup deleted, but the backup list could not be updated: %1").arg(indexWarning));
            });
        });
    });
    confirm->open();
}
