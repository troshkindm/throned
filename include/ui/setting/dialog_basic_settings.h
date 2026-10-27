#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QTimer>
#include "include/ui/setting/SettingsBindings.h"
#include <memory>
#include "ui_dialog_basic_settings.h"
#include "include/engine/EditLocks.h"
#include "include/engine/services/WebDavBackup.h"

namespace Ui {
class DialogBasicSettings;
}

class QButtonGroup;
class QStackedWidget;
namespace Validation {
class Form;
}

class DialogBasicSettings : public QDialog {
    Q_OBJECT

public:
    explicit DialogBasicSettings(QWidget *parent = nullptr);

    ~DialogBasicSettings();

    void showLoggingPage();
    // Widgets this platform or installation does not offer: the dialog hides them and the settings search skips them.
    static QList<QWidget *> UnavailableWidgets(const Ui::DialogBasicSettings *ui);

public slots:

    void accept();

private:
    // Pages migrated off the .ui build their controls where they are used and bind
    // each one to its setting, so nothing has to be moved between parents and nothing
    // has to be remembered in accept(). Pages still on the .ui are unaffected.
    SettingsBindings bindings_;
    QCheckBox *alwaysStandardUser_ = nullptr;
    QTimer *themeApplyDebounce_ = nullptr;
    class QPlainTextEdit *siteTargetsEdit_ = nullptr;
    class QSpinBox *siteTimeoutSpin_ = nullptr;
    QStackedWidget *settingsStack_ = nullptr;
    QButtonGroup *settingsNavGroup_ = nullptr;

    Ui::DialogBasicSettings *ui;

    void applySelectedTheme();
    Validation::Form *form = nullptr;

    Engine::EditLock settingsLock{Engine::EditTarget::Settings};

    void bindValidation();

    void applyRegexHighlighting();

    void refreshUrlSchemeStatus();

    void downloadXrayGeoAsset(const QString &url, const QString &fileName);

    void rememberGeoAssetUrl(QComboBox *combo, const QString &url, QStringList &history);

    // Inspect, choose the parts, apply and restart; fallbackDeviceName names the creator when the archive does not.
    void restoreBackupFile(const QString &filePath, const QString &fallbackDeviceName);

    struct WebDavChooser;

    // A WebDAV worker is running; the backup buttons refuse to start anything else meanwhile.
    bool webDavBusy = false;

    bool webDavReady(const QString &title);

    void showWebDavSettings();

    // A null chooser opens one with the list; otherwise the open one is refilled and notice joins its note.
    void loadWebDavBackups(const std::shared_ptr<WebDavChooser> &chooser, const QString &notice = {});

    void showWebDavBackups(const QList<Engine::WebDavBackup::Entry> &entries, const QString &indexWarning);

    void fillWebDavChooser(WebDavChooser &chooser, const QList<Engine::WebDavBackup::Entry> &entries, const QString &note);

    void restoreWebDavBackup(const std::shared_ptr<WebDavChooser> &chooser);

    void deleteWebDavBackup(const std::shared_ptr<WebDavChooser> &chooser);

    struct {
        QString custom_inbound;
        bool needRestart = false;
        bool updateDisableTray = false;
        bool updateTrayIcon = false;
        bool updateMaxLogLines = false;
        // What the log font boxes showed on open; left untouched, an unset family/size keeps following its default.
        QString shownLogFontFamily;
        int shownLogFontSize = 0;
        bool updateDisableAdmin = false;
    } CACHE;

private slots:
    void on_backup_create_clicked();
    void on_backup_restore_clicked();
    void on_webdav_settings_clicked();
    void on_webdav_upload_clicked();
    void on_webdav_restore_clicked();
    void on_xray_geoip_download_clicked();
    void on_xray_geosite_download_clicked();
};
