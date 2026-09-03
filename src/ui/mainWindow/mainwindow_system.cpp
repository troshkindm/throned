#include "include/ui/mainwindow.h"
#include "NkrVersion.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScreen>
#include <QScopeGuard>
#include <QSet>
#include <QTextBrowser>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.hpp"
#include "include/api/RPC.h"
#include "include/database/RoutesRepo.h"
#include "include/configs/generate.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/Logger.hpp"
#include "include/sys/Process.hpp"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/widget/UpdateStatusWidget.h"

#include "include/ui/group/dialog_manage_groups.h"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/ui/setting/dialog_hotkey.h"
#include "include/ui/setting/dialog_manage_routes.h"
#include "include/ui/setting/dialog_otp_manager.h"
#include "include/ui/setting/dialog_dpi_bypass.h"
#include "include/ui/setting/dialog_preset_settings.h"
#include "include/ui/setting/dialog_vpn_settings.h"

#ifdef Q_OS_WIN
#include "3rdparty/WinCommander.hpp"
#include "include/sys/windows/WinVersion.h"
#endif
#ifdef Q_OS_LINUX
#include "include/sys/linux/LinuxCap.h"
#endif
#ifdef Q_OS_MACOS
#include "include/sys/macos/MacOS.h"
#endif

qint64 MainWindow::GetCorePid() {
    QMutexLocker lock(&coreProcessMutex);
    return core_process ? core_process->processId() : 0;
}

QString MainWindow::GetRunningConfigName() {
    auto ent = running;
    if (ent == nullptr || ent->outbound == nullptr) return {};
    return ent->outbound->DisplayTypeAndName();
}

void MainWindow::on_menu_basic_settings_triggered() {
    USE_DIALOG(DialogBasicSettings)
}

void MainWindow::openLogSettings() {
    if (dialog_is_using) return;
    dialog_is_using = true;
    auto *dialog = new DialogBasicSettings(this);
    dialog->showLoggingPage();
    connect(dialog, &QDialog::finished, this, [=, this] {
        dialog->deleteLater();
        dialog_is_using = false;
    });
    dialog->show();
}

void MainWindow::on_menu_manage_groups_triggered() {
    USE_DIALOG(DialogManageGroups)
}

void MainWindow::on_menu_routing_settings_triggered() {
    if (dialog_is_using) return;
    dialog_is_using = true;
    auto dialog = new DialogManageRoutes(this);
    connect(dialog, &QDialog::finished, this, [=,this] {
        dialog->deleteLater();
        dialog_is_using = false;
    });
    dialog->show();
}

void MainWindow::on_menu_vpn_settings_triggered() {
    USE_DIALOG(DialogVPNSettings)
}

void MainWindow::on_menu_dpi_bypass_triggered() {
    USE_DIALOG(DialogDpiBypass)
}

void MainWindow::on_menu_preset_settings_triggered() {
    USE_DIALOG(DialogPresetSettings)
}

void MainWindow::on_menu_otp_manager_triggered() {
    USE_DIALOG(DialogOtpManager)
}

void MainWindow::on_menu_hotkey_settings_triggered() {
    if (dialog_is_using) return;
    dialog_is_using = true;
    auto dialog = new DialogHotkey(this, getActionsForShortcut());
    connect(dialog, &QDialog::finished, this, [=,this]
    {
        dialog->deleteLater();
        dialog_is_using = false;
    });
    dialog->show();
}

void MainWindow::on_commitDataRequest() {
    qDebug() << "Start of data save";

    auto* settings = Configs::dataManager->settingsRepo.get();

    settings->mainWindowGeometry = this->saveGeometry().toBase64(QByteArray::Base64Encoding);
    if (!isMaximized()) {
        auto news = QString("%1x%2").arg(size().width()).arg(size().height());
        if (settings->mw_size != news) settings->mw_size = news;
    }
    settings->splitter_state = ui->splitter->saveState().toBase64();

    // Backstop only: this runs on a graceful exit, so it must never be the sole write.
    if (settings->remember_enable && settings->started_id >= 0) settings->remember_id = settings->started_id;
    settings->remember_system_proxy = settings->spmode_system_proxy;
    settings->remember_tun = settings->spmode_vpn;

    settings->Save();
    qDebug() << "End of data save";
}

void MainWindow::prepare_exit()
{
    qDebug() << "prepare for exit...";
    mu_exit.lock();
    if (Configs::dataManager->settingsRepo->prepare_exit)
    {
        qDebug() << "prepare exit had already succeeded, ignoring...";
        mu_exit.unlock();
        return;
    }
    Configs::dataManager->settingsRepo->prepare_exit = true;
    LOG_INFO("prepare_exit started, tearing down proxy/tun/core");
    if (Configs::dataManager->settingsRepo->spmode_system_proxy) set_system_proxy(false);
    if (Configs::dataManager->settingsRepo->system_dns_set) set_system_dns(false, false);
    RegisterHiddenMenuShortcuts(true);
    RegisterHotkey(true);
    on_commitDataRequest();
    Configs::dataManager->settingsRepo->noSave = true; // don't change Configs::dataManager->settingsRepo after this line
    profile_stop(false, true);

    runOnThread([=, this]()
    {
        core_process->Kill();
    }, DS_cores, true);
    HideWindow(this);
    tray->hide();

    mu_exit.unlock();
    qDebug() << "prepare exit done!";
}

void MainWindow::on_menu_exit_triggered() {
    QString updaterProgram;
    QString updaterWorkingDirectory;
    QStringList updaterArguments;
    if (exit_reason == ExitReason::RunUpdater) {
        updaterWorkingDirectory = QApplication::applicationDirPath();
        const QDir applicationDir(updaterWorkingDirectory);
        QString updaterLanguage = QStringLiteral("en");
        switch (Configs::dataManager->settingsRepo->language) {
        case 2: updaterLanguage = QStringLiteral("zh"); break;
        case 3: updaterLanguage = QStringLiteral("fa"); break;
        case 4: updaterLanguage = QStringLiteral("ru"); break;
        case 0: updaterLanguage = QLocale::system().name().section(QChar('_'), 0, 0); break;
        default: break;
        }
        updaterArguments = {QStringLiteral("--lang"), updaterLanguage,
                            QStringLiteral("--parent-pid"), QString::number(QCoreApplication::applicationPid()),
                            QStringLiteral("--executable"), QApplication::applicationFilePath()};
        if (Configs::dataManager->settingsRepo->flag_tray)
            updaterArguments << QStringLiteral("--launch-tray");
#ifdef Q_OS_WIN
        const QString updaterSource = applicationDir.absoluteFilePath(QStringLiteral("updater.exe"));
        updaterProgram = applicationDir.absoluteFilePath(QStringLiteral("updater.old"));
        if ((QFile::exists(updaterProgram) && !QFile::remove(updaterProgram)) ||
            !QFile::copy(updaterSource, updaterProgram)) {
            LOG_ERROR(QStringLiteral("Could not prepare updater copy: %1 -> %2").arg(updaterSource, updaterProgram));
            MessageBoxWarning(tr("Update"), tr("Could not prepare the updater. Throned will keep running."));
            exit_reason = ExitReason::None;
            return;
        }
#else
        updaterProgram = applicationDir.absoluteFilePath(QStringLiteral("updater"));
#endif
    }

    prepare_exit();
    if (exit_reason == ExitReason::RunUpdater) {
        if (!QProcess::startDetached(updaterProgram, updaterArguments, updaterWorkingDirectory)) {
            LOG_ERROR(QStringLiteral("Could not start updater: %1").arg(updaterProgram));
            MessageBoxWarning(tr("Update"), tr("Could not start the updater. Start Throned again manually."));
        }
    } else if (exit_reason == ExitReason::Restart || exit_reason == ExitReason::RestartWithTun || exit_reason == ExitReason::RestartWithDns) {
        QDir::setCurrent(QApplication::applicationDirPath());

        auto arguments = Configs::dataManager->settingsRepo->argv;
        if (arguments.length() > 0) {
            arguments.removeFirst();
            arguments.removeAll("-tray");
            arguments.removeAll("-flag_restart_tun_on");
            arguments.removeAll("-flag_restart_dns_set");
        }
        auto program = QApplication::applicationFilePath();

        if (exit_reason == ExitReason::RestartWithTun || exit_reason == ExitReason::RestartWithDns) {
            if (exit_reason == ExitReason::RestartWithTun) arguments << "-flag_restart_tun_on";
            if (exit_reason == ExitReason::RestartWithDns) arguments << "-flag_restart_dns_set";
#ifdef Q_OS_WIN
            WinCommander::runProcessElevated(program, arguments, "", 1, false);
#else
            QProcess::startDetached(program, arguments);
#endif
        } else {
            QProcess::startDetached(program, arguments);
        }
    }
    QCoreApplication::quit();
}

void MainWindow::toggle_system_proxy() {
    auto currentState = Configs::dataManager->settingsRepo->spmode_system_proxy;
    if (currentState) {
        set_spmode_system_proxy(false);
    } else {
        set_spmode_system_proxy(true);
    }
}

bool MainWindow::get_elevated_permissions(ExitReason reason) {
    if (Configs::dataManager->settingsRepo->disable_privilege_req)
    {
        MW_show_log(tr("User opted for no privilege req, some features may not work"));
        return true;
    }
    if (Configs::IsAdmin()) return true;
#ifdef Q_OS_LINUX
    if (!Linux_HavePkexec()) {
        MessageBoxWarning(software_name, "Please install \"pkexec\" first.");
        return false;
    }
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please give the core root privileges"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        runOnNewThread([=,this]
        {
            auto chownArgs = QString("root:root " + Configs::FindCoreRealPath());
            auto ret = Linux_Run_Command("chown", chownArgs);
            if (ret != 0) {
                MW_show_log(QString("Failed to run chown %1 code is %2").arg(chownArgs).arg(ret));
            }
            auto chmodArgs = QString("u+s " + Configs::FindCoreRealPath());
            ret = Linux_Run_Command("chmod", chmodArgs);
            if (ret == 0) {
                StopVPNProcess();
            } else {
                MW_show_log(QString("Failed to run chmod %1").arg(chmodArgs));
            }
        });
        return false;
    }
#endif
#ifdef Q_OS_WIN
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please run Throned as admin"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        this->exit_reason = reason;
        on_menu_exit_triggered();
    }
#endif

#ifdef Q_OS_MACOS
    if (Configs::isSetuidSet(Configs::FindCoreRealPath().toStdString()))
    {
        StopVPNProcess();
        return true;
    }
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please give the core root privileges"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes)
    {
        auto Command = QString("sudo chown root:wheel '%1' && sudo chmod u+s '%1'").arg(Configs::FindCoreRealPath());
        auto ret = Mac_Run_Command(Command);
        if (ret == 0) {
            MessageBoxInfo(tr("Requesting permission"), tr("Please Enter your password in the opened terminal, then try again"));
            return false;
        } else {
            MW_show_log(QString("Failed to run %1 with %2").arg(Command).arg(ret));
            return false;
        }
    }
#endif
    return false;
}

void MainWindow::set_system_proxy(bool enable) {
    if (enable) {
        auto socks_port = Configs::dataManager->settingsRepo->inbound_socks_port;
        SetSystemProxy(socks_port, socks_port, Configs::dataManager->settingsRepo->proxy_scheme);
    } else {
        ClearSystemProxy();
    }
}

void MainWindow::set_spmode_system_proxy(bool enable, bool save) {
    if (enable && Configs::dataManager->settingsRepo->disable_mixed_inbound) {
        runOnUiThread([=, this] {
           MessageBoxWarning("Invalid Operation", "Cannot set system proxy when mixed inbound is disabled.");
        });
        ui->checkBox_SystemProxy->setChecked(false);
        return;
    }
    Configs::dataManager->settingsRepo->spmode_system_proxy = enable;
    if (running) {
        set_system_proxy(enable);
        if (!enable && Configs::dataManager->settingsRepo->reset_proxy_on_disable_sp) {
            profile_start(running->id);
        }
    }

    if (save) {
        Configs::dataManager->settingsRepo->remember_system_proxy = enable;
        Configs::dataManager->settingsRepo->Save();
    }

    refresh_status();
}

void MainWindow::set_spmode_vpn(bool enable, bool save) {
    if (enable == Configs::dataManager->settingsRepo->spmode_vpn) return;

    if (enable) {
        bool requestPermission = !Configs::IsAdmin();
        if (requestPermission) {
            if (!get_elevated_permissions(ExitReason::RestartWithTun)) {
                refresh_status();
                return;
            }
        }
    }

    if (save) {
        // Written here, after the elevation check, so a failed enable is not remembered.
        Configs::dataManager->settingsRepo->remember_tun = enable;
        Configs::dataManager->settingsRepo->Save();
    }

    Configs::dataManager->settingsRepo->spmode_vpn = enable;
    refresh_status();

    if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
}

bool MainWindow::StopVPNProcess() {
    runOnThread([=, this]
    {
        core_process->Kill();
    }, DS_cores, true);

    return true;
}

void MainWindow::RestartCore() {
    runOnThread([=, this]
    {
        profile_stop(true, true, true);
        core_process->Kill();
    }, DS_cores);
}

namespace {

bool isNewer(QString assetName) {
    if (QString(NKR_VERSION).isEmpty()) return false;
    const auto firstDash = assetName.indexOf('-');
    if (firstDash < 0) return false;
    assetName = assetName.mid(firstDash + 1); // take out Throned- (or legacy Throne-)
    QString version;
    auto spl = assetName.split('-');
    version += spl[0];
    if (spl[1].contains("beta") || spl[1].contains("alpha") || spl[1].contains("rc")) version += "."+spl[1];
    auto parts = version.split("."); // [1,2,3,beta,13]
    auto currentParts = QString(NKR_VERSION).replace("-", ".").split('.');
    if (parts.size() < 3 || currentParts.size() < 3)
    {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }
    std::vector<int> verNums;
    std::vector<int> currNums;
    verNums.push_back(parts[0].toInt());
    verNums.push_back(parts[1].toInt());
    verNums.push_back(parts[2].toInt());
    if (parts.size() > 3)
    {
        if (parts[3] == "alpha") verNums.push_back(1);
        if (parts[3] == "beta") verNums.push_back(2);
        if (parts[3] == "rc") verNums.push_back(3);
        if (parts.size() > 4) verNums.push_back(parts[4].toInt());
    }

    currNums.push_back(currentParts[0].toInt());
    currNums.push_back(currentParts[1].toInt());
    currNums.push_back(currentParts[2].toInt());
    if (currentParts.size() > 3)
    {
        if (currentParts[3] == "alpha") currNums.push_back(1);
        if (currentParts[3] == "beta") currNums.push_back(2);
        if (currentParts[3] == "rc") currNums.push_back(3);
        if (currentParts.size() > 4) currNums.push_back(currentParts[4].toInt());
    }

    if (verNums.size() < 3 || currNums.size() < 3)
    {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }

    for (int i=0;i<3;i++)
    {
        if (verNums[i] > currNums[i]) return true;
        if (verNums[i] < currNums[i]) return false;
    }

    if (verNums.size() == 5 && currNums.size() == 3) return false;
    if (verNums.size() == 3 && currNums.size() == 5) return true;
    if (verNums.size() == 5 && currNums.size() == 5)
    {
        for (int i=3;i<5;i++)
        {
            if (verNums[i] > currNums[i]) return true;
            if (verNums[i] < currNums[i]) return false;
        }
    } else
    {
		MW_show_log("There are no updates. You have the latest version - " + QString(NKR_VERSION));
        return false;
    }
    return false;
}

constexpr auto dashboardDownloadURL = "https://github.com/SagerNet/sing-box-dashboard/archive/a9d068d22a6cff77dbb2f803e7469209f2e76af4.zip";

bool copyOut(const QString &from, const QString &to) {
    QFile::remove(to);
    if (!QFile::copy(from, to)) return false;
    // Resource files are read-only, and QFile::copy carries that onto the copy.
    return QFile::setPermissions(to, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                     QFileDevice::ReadGroup | QFileDevice::ReadOther);
}

bool unpackBundledDashboard(const QDir &dest) {
    if (!QFile::exists(":/dashboard/index.html")) return false;
    const QDir bundle(":/dashboard");
    QDirIterator it(bundle.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto source = it.next();
        const auto target = dest.filePath(bundle.relativeFilePath(source));
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !copyOut(source, target)) return false;
    }
    return true;
}

} // namespace

void MainWindow::SeedDashboard() {
    QDir dashDir(Configs::apiDashboardDir);
    if (!dashDir.exists() && !QDir().mkpath(Configs::apiDashboardDir)) return;
    if (!QFile::exists(dashDir.filePath("index.html"))) unpackBundledDashboard(dashDir);
    // Reinstalling replaces the whole directory, so this cannot be a one-time copy.
    auto src = QFile(":/Throned/dashboard-bootstrap.html");
    if (!src.open(QIODevice::ReadOnly)) {
        MW_show_log(tr("Dashboard bootstrap resource is missing from the build; the dashboard page will not load."));
        return;
    }
    const auto data = src.readAll();
    src.close();
    if (auto dest = QFile(dashDir.filePath("throne.html")); dest.open(QIODevice::Truncate | QIODevice::WriteOnly)) {
        dest.write(data);
        dest.close();
    } else {
        MW_show_log(tr("Could not write the dashboard page to %1").arg(dashDir.filePath("throne.html")));
    }
}

void MainWindow::OpenDashboard() {
    const auto &settings = *Configs::dataManager->settingsRepo;
    const auto port = settings.core_box_api_port;
    if (port <= 0) {
        MessageBoxWarning(software_name, tr("The sing-box API is disabled. Set a listen port in Preferences > Basic Settings > Core."));
        return;
    }
    if (settings.started_id < 0) {
        MessageBoxWarning(software_name, tr("Start a profile first; the dashboard is served by the running core."));
        return;
    }

    const auto show = [this, port] {
        SeedDashboard();
        // Fragment, not query: browsers never send it to the server.
        QUrl url(QString("http://127.0.0.1:%1/dashboard/throne.html").arg(port));
        url.setFragment(QString("secret=%1&url=127.0.0.1:%2")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(Configs::dataManager->settingsRepo->core_box_api_secret)))
                            .arg(port),
                        QUrl::StrictMode);
        QDesktopServices::openUrl(url);
    };

    SeedDashboard();
    if (QFile::exists(QDir(Configs::apiDashboardDir).filePath("index.html"))) {
        show();
        return;
    }

    if (QMessageBox::question(this, tr("Web dashboard"),
                              tr("The dashboard is not installed yet. Download it now?"))
        != QMessageBox::StandardButton::Yes) {
        return;
    }

    runOnNewThread([=, this] {
        if (!mu_download_dashboard.tryLock()) {
            runOnUiThread([=, this] {
                MessageBoxWarning(tr("Cannot start"), tr("A dashboard download is already running"));
            });
            return;
        }
        const auto archive = QString("throne-dashboard.zip");
        auto error = NetworkRequestHelper::DownloadAsset(dashboardDownloadURL, archive, true);
        if (error.isEmpty()) {
            bool ok = false;
            error = API::defaultClient->InstallDashboard(&ok, Configs::GetBasePath() + "/" + archive,
                                                         QDir(Configs::apiDashboardDir).absolutePath());
            if (!ok && error.isEmpty()) error = tr("The core did not answer.");
        }
        QFile::remove(Configs::GetBasePath() + "/" + archive);
        mu_download_dashboard.unlock();

        runOnUiThread([=, this] {
            if (!error.isEmpty()) {
                MessageBoxWarning(tr("Failed to install the dashboard"), error);
                return;
            }
            show();
        });
    });
}

void MainWindow::CheckUpdate(bool silent) {
    if (updateCheckInProgress_.exchange(true)) return;
    const auto updateCheckGuard = qScopeGuard([this] { updateCheckInProgress_.store(false); });

    QString search;
#ifdef Q_OS_WIN
#  ifdef Q_PROCESSOR_ARM_64
    search = "windows-arm64";
#  else
#    ifdef Q_OS_WIN64
        if (WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1809))
            search = "windows64";
        else
	        search = "windowslegacy64";
#    else
	    search = "windows32";
#    endif
#  endif
#endif
#ifdef Q_OS_LINUX
#  ifdef Q_PROCESSOR_X86_64
    search = "linux-amd64";
#  else
    search = "linux-arm64";
#  endif
#endif
#ifdef Q_OS_MACOS
#  ifdef Q_PROCESSOR_X86_64
	search = "macos-amd64";
#  else
	search = "macos-arm64";
#  endif
#endif
    if (search.isEmpty()) {
        if (!silent) runOnUiThread([=,this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Not official support platform"));
        });
        return;
    }

    const bool requestUsedProfile = Configs::dataManager->settingsRepo->started_id >= 0;
    const auto rememberDirectFailure = [this, requestUsedProfile] {
        if (requestUsedProfile) return;
        updateCheckRetryAfterConnect_.store(true);
        // Covers the race where the profile came up while the direct request was
        // still timing out: there may be no later profile-start event to wake us.
        runOnUiThread([this] {
            if (Configs::dataManager->settingsRepo->started_id >= 0)
                retryPendingUpdateCheck();
        });
    };

    auto resp = NetworkRequestHelper::HttpGet(
        "https://api.github.com/repos/troshkindm/throned/releases", false, requestUsedProfile);
    if (!resp.error.isEmpty()) {
        rememberDirectFailure();
        if (!silent) runOnUiThread([=,this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Requesting update error: %1").arg(resp.error + "\n" + resp.data));
        });
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument releasesDocument = QJsonDocument::fromJson(resp.data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !releasesDocument.isArray()) {
        rememberDirectFailure();
        const QString error = parseError.error == QJsonParseError::NoError
            ? tr("GitHub returned an unexpected response.")
            : tr("Could not read GitHub's response: %1").arg(parseError.errorString());
        if (!silent) runOnUiThread([=, this] {
            MessageBoxWarning(tr("Update"), tr("Requesting update error: %1").arg(error));
        });
        return;
    }
    updateCheckRetryAfterConnect_.store(false);

    QString assets_name, release_download_url, release_url, release_note, note_pre_release;
    bool exitFlag = false;
    const QJsonArray array = releasesDocument.array();
    for (const QJsonValue value : array) {
        QJsonObject release = value.toObject();
        if (release["prerelease"].toBool() && !Configs::dataManager->settingsRepo->allow_beta_update) continue;
        for (const QJsonValue asset : release["assets"].toArray()) {
            const QString assetName = asset["name"].toString();
            if (assetName.startsWith("Throned-") && assetName.contains(search) &&
                assetName.section('.', -1) == QString("zip")) {
                note_pre_release = release["prerelease"].toBool() ? " (Pre-release)" : "";
                release_url = release["html_url"].toString();
                release_note = release["body"].toString();
                assets_name = assetName;
                release_download_url = asset["browser_download_url"].toString();
                exitFlag = true;
                break;
            }
        }
        if (exitFlag) break;
    }

    if (release_download_url.isEmpty() || !isNewer(assets_name)) {
        if (!silent) runOnUiThread([=,this] {
            MessageBoxInfo(QObject::tr("Update"), QObject::tr("No update"));
        });
        return;
    }

    const auto showUpdatePrompt = [=,this] {
        auto allow_updater = !Configs::dataManager->settingsRepo->flag_use_appdata;
        const auto choice = ShowUpdatePrompt(this, QObject::tr("Update") + note_pre_release,
                                             assets_name, release_note, allow_updater);
        //
        if (choice == UpdatePromptChoice::Update) {
            startUpdateDownload(release_download_url, assets_name);
        } else if (choice == UpdatePromptChoice::OpenInBrowser) {
            QDesktopServices::openUrl(QUrl(release_url));
        }
    };

    runOnUiThread([=,this] {
        if (!silent) {
            showUpdatePrompt();
            return;
        }
        // The background check must not steal focus, so it only leaves a tray
        // notification. Nothing is downloaded until the user opens the prompt.
        pendingUpdatePrompt = showUpdatePrompt;
        tray->showMessage(QObject::tr("Throned update available"),
                          QObject::tr("%1 is ready to download. Click to see the release notes.").arg(assets_name),
                          QSystemTrayIcon::Information, 10000);
    });
}

void MainWindow::startUpdateDownload(const QString &url, const QString &assetName) {
    if (url.isEmpty() || assetName.isEmpty() || updateStatusWidget == nullptr) return;

    pendingUpdateDownloadUrl = url;
    pendingUpdateAssetName = assetName;
    lastUpdateProgressMs_.store(0);
    updateStatusWidget->showDownloading(assetName, 0, -1);

    runOnNewThread([this, url, assetName] {
        if (!mu_download_update.tryLock()) return;
        const auto unlock = qScopeGuard([this] { mu_download_update.unlock(); });

        const auto progress = [this, assetName](qint64 received, qint64 total) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const bool complete = total > 0 && received >= total;
            const qint64 previous = lastUpdateProgressMs_.load();
            if (!complete && now - previous < 80) return;
            lastUpdateProgressMs_.store(now);
            runOnUiThread([this, assetName, received, total, complete] {
                if (updateStatusWidget == nullptr) return;
                if (complete) updateStatusWidget->showPreparing(assetName);
                else updateStatusWidget->showDownloading(assetName, received, total);
            });
        };

        const bool proxyAvailable = Configs::dataManager->settingsRepo->started_id >= 0;
        const QString error = NetworkRequestHelper::DownloadAsset(
            url, QStringLiteral("Throned.zip"), proxyAvailable, progress);
        runOnUiThread([this, assetName, error] {
            if (updateStatusWidget == nullptr) return;
            if (error.isEmpty()) {
                updateStatusWidget->showReady(assetName);
            } else {
                MW_show_log(tr("Update download failed: %1").arg(error));
                updateStatusWidget->showError(error);
            }
        });
    });
}

void MainWindow::retryPendingUpdateCheck() {
    if (!updateCheckRetryAfterConnect_.exchange(false)) return;
    QTimer::singleShot(1200, this, [this] {
        if (Configs::dataManager->settingsRepo->started_id < 0) {
            updateCheckRetryAfterConnect_.store(true);
            return;
        }
        if (updateCheckInProgress_.load()) {
            updateCheckRetryAfterConnect_.store(true);
            QTimer::singleShot(1500, this, [this] { retryPendingUpdateCheck(); });
            return;
        }
        runOnNewThread([this] { CheckUpdate(true); });
    });
}

namespace {
    bool isSensitiveQueryKey(const QString &key) {
        static const QSet<QString> keys{
            QStringLiteral("access_token"), QStringLiteral("api-key"), QStringLiteral("api_key"),
            QStringLiteral("apikey"), QStringLiteral("auth"), QStringLiteral("authorization"),
            QStringLiteral("key"), QStringLiteral("password"), QStringLiteral("secret"),
            QStringLiteral("token"),
        };
        return keys.contains(key.toLower());
    }

    void appendUrlSecrets(const QString &text, QStringList &secrets) {
        const auto url = QUrl::fromUserInput(text);
        if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) return;
        if (!url.userInfo().isEmpty()) secrets << url.userInfo();
        if (!url.password().isEmpty()) secrets << url.password();
        const QUrlQuery query(url);
        for (const auto &[key, value] : query.queryItems(QUrl::FullyDecoded)) {
            if (isSensitiveQueryKey(key) && !value.isEmpty()) secrets << value;
        }

        // Personal DNS products commonly put the account id in the path instead
        // of a query. Keep the provider visible while hiding that final segment.
        const auto host = url.host().toLower();
        const auto parts = url.path().split('/', Qt::SkipEmptyParts);
        if (!parts.isEmpty() &&
            (host == QStringLiteral("dns.nextdns.io") || host == QStringLiteral("d.adguard-dns.com") ||
             (parts.size() > 1 && parts.at(parts.size() - 2) == QStringLiteral("dns-query")))) {
            secrets << parts.last();
        }
    }

    // Values that must never leave the machine in a paste. Redaction is by literal
    // match rather than pattern: a token only has to be recognised once, here.
    QStringList collectSecrets() {
        const auto &settings = *Configs::dataManager->settingsRepo;
        QStringList secrets{
            settings.core_box_api_secret,
            settings.core_box_clash_api_secret,
            settings.inbound_user,
            settings.inbound_pass,
            settings.internal_proxy_auth,
            settings.warp_private_key,
        };
        for (const int groupID : Configs::dataManager->groupsRepo->GetAllGroupIds()) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(groupID);
            if (group != nullptr && !group->url.isEmpty()) secrets << group->url;
        }
        appendUrlSecrets(settings.remote_dns, secrets);
        appendUrlSecrets(settings.direct_dns, secrets);
        appendUrlSecrets(settings.core_box_underlying_dns, secrets);
        static const QRegularExpression urlInJson(QStringLiteral(R"((?:https?|h3)://[^\s\"']+)"),
                                                   QRegularExpression::CaseInsensitiveOption);
        auto urls = urlInJson.globalMatch(settings.dns_object);
        while (urls.hasNext()) appendUrlSecrets(urls.next().captured(), secrets);
        secrets.removeAll("");
        // Longest first, so a token that contains another is masked whole.
        std::sort(secrets.begin(), secrets.end(),
                  [](const QString &a, const QString &b) { return a.size() > b.size(); });
        return secrets;
    }

    QString redact(QString text, const QStringList &secrets) {
        for (const auto &secret : secrets) {
            // A short value is more likely an ordinary word than a token, and blanket
            // replacing one would shred the log it is supposed to make readable.
            if (secret.size() < 6) continue;
            text.replace(secret, QStringLiteral("[redacted]"));
        }
        // Share links carry the credentials themselves, and the log holds them verbatim:
        // scanning a QR writes the whole link out. Matching by scheme catches every
        // profile format at once. Plain http(s) is deliberately left alone -- rule-set
        // and update URLs are worth reading, and subscription links with tokens are
        // already covered by the literal pass above.
        static const QRegularExpression shareLink(
            QStringLiteral(R"(\b(?:vless|vmess|trojan|ss|ssr|hysteria2?|hy2|tuic|anytls|socks5?|wireguard|wg|snell|mieru|juicity|naive|shadowtls|ssh|throne)://\S+)"),
            QRegularExpression::CaseInsensitiveOption);
        text.replace(shareLink, QStringLiteral("[link redacted]"));

        // Preserve the resolver host for support while masking credentials and
        // common token-bearing query fields even when they only appear in a
        // generated DNS object rather than in the settings verbatim.
        static const QRegularExpression urlUserInfo(
            QStringLiteral(R"((\b[a-z][a-z0-9+.-]*://)[^/\s@\"']+@)"),
            QRegularExpression::CaseInsensitiveOption);
        text.replace(urlUserInfo, QStringLiteral("\\1[credentials redacted]@"));
        static const QRegularExpression sensitiveQuery(
            QStringLiteral(R"(([?&](?:access_token|api[-_]?key|apikey|auth|authorization|key|password|secret|token)=)[^&#\s\"']+)"),
            QRegularExpression::CaseInsensitiveOption);
        text.replace(sensitiveQuery, QStringLiteral("\\1[redacted]"));
        return text;
    }

    bool isSensitiveJsonKey(const QString &key) {
        static const QSet<QString> keys{
            QStringLiteral("access_token"), QStringLiteral("api-key"), QStringLiteral("api_key"),
            QStringLiteral("apikey"), QStringLiteral("auth"), QStringLiteral("authorization"),
            QStringLiteral("client_key"), QStringLiteral("cookie"), QStringLiteral("key"),
            QStringLiteral("password"), QStringLiteral("private_key"), QStringLiteral("proxy-authorization"),
            QStringLiteral("secret"), QStringLiteral("set-cookie"), QStringLiteral("token"),
        };
        return keys.contains(key.toLower());
    }

    QJsonValue redactJsonForDiagnostics(const QJsonValue &value, const bool redactValues = false) {
        if (redactValues && !value.isArray() && !value.isObject()) return QStringLiteral("[redacted]");
        if (value.isArray()) {
            QJsonArray result;
            for (const auto &item : value.toArray()) result.append(redactJsonForDiagnostics(item, redactValues));
            return result;
        }
        if (value.isObject()) {
            QJsonObject result;
            const auto object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                const auto key = it.key().toLower();
                const bool hideValue = redactValues || key == QStringLiteral("headers") || isSensitiveJsonKey(key);
                result.insert(it.key(), redactJsonForDiagnostics(it.value(), hideValue));
            }
            return result;
        }
        return value;
    }

    QString onOff(const bool value) { return value ? QStringLiteral("on") : QStringLiteral("off"); }
}

// Everything a support answer needs, in one paste. Built here rather than asked
// for question by question, because a user who can describe their DNS setup
// accurately is not the user who needs help.
QString MainWindow::collectDiagnostics() {
    const auto &settings = *Configs::dataManager->settingsRepo;
    const auto secrets = collectSecrets();
    QStringList out;

    out << QString("Throned %1").arg(NKR_VERSION);
    out << QString("OS: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    out << QString("Qt: %1").arg(qVersion());
    out << "";

    out << QString("Tun: %1 | System proxy: %2 | System DNS: %3")
               .arg(onOff(settings.spmode_vpn), onOff(settings.spmode_system_proxy), onOff(settings.system_dns_set));
    if (const auto route = Configs::dataManager->routesRepo->GetRouteProfile(settings.current_route_id)) {
        out << QString("Route profile: %1").arg(route->name);
    }
    if (running != nullptr && running->outbound != nullptr) {
        out << QString("Running profile type: %1").arg(running->outbound->DisplayType());
    } else {
        out << "Running profile: none";
    }
    out << "";

    out << "DNS settings:";
    out << QString("  remote: %1 (ipv6 %2)").arg(settings.remote_dns, onOff(!settings.remote_dns_disable_ipv6));
    out << QString("  direct: %1 (ipv6 %2)").arg(settings.direct_dns, onOff(!settings.direct_dns_disable_ipv6));
    out << QString("  final out: %1 | fake ip: %2 | dns routing: %3")
               .arg(settings.dns_final_out, onOff(settings.fake_dns), onOff(settings.enable_dns_routing));
    out << QString("  local override: %1").arg(settings.core_box_underlying_dns.isEmpty() ? "(empty)" : settings.core_box_underlying_dns);
    out << QString("  custom dns object: %1 | apply to full configs: %2")
               .arg(onOff(settings.use_dns_object), onOff(settings.apply_dns_to_full_config));
    out << QString("  hijack dns server: %1").arg(onOff(settings.enable_dns_server));
    out << "";

    if (running != nullptr) {
        if (const auto built = Configs::BuildSingBoxConfig(running); built != nullptr && built->error.isEmpty()) {
            out << "Generated dns section:";
            const auto dns = redactJsonForDiagnostics(built->coreConfig.value("dns")).toObject();
            out << QJsonObject2QString(dns, true);
            out << "";
        }
    }

    if (const auto pings = pingHistoryReport(); !pings.isEmpty()) {
        out << pings;
        out << "";
    }

    out << "Last log lines:";
    const auto logText = ui->masterLogBrowser->toPlainText();
    const auto lines = logText.split('\n');
    out << lines.mid(qMax(0, lines.size() - 80)).join('\n');

    return redact(out.join('\n'), secrets);
}

void MainWindow::copyDiagnostics() {
    const auto report = collectDiagnostics();
    QApplication::clipboard()->setText(report);
    MW_show_log(tr("Diagnostics copied to the clipboard (%1 characters). Secrets and subscription links are masked.")
                    .arg(report.size()));
}
