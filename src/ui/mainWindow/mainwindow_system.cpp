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
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScreen>
#include <QScopeGuard>
#include <QSet>
#include <QTextBrowser>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <QPushButton>
#include <QThread>

#include "include/api/RPC.h"
#include "include/database/RoutesRepo.h"
#include "include/api/remote/Server.hpp"
#include "include/configs/generate.h"
#include "include/engine/EngineHost.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/app/ExitController.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/engine/services/Dashboard.h"
#include "include/engine/supervisor/CoreSupervisor.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/LocalNetwork.hpp"
#include "include/global/Logger.hpp"
#include "include/scanner/ScanManager.h"
#include "include/sys/HostPresence.hpp"
#include "include/sys/KillSwitch.hpp"
#include "include/sys/SystemProxy.hpp"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/widget/UpdateStatusWidget.h"

#include "include/ui/group/dialog_manage_groups.h"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/ui/setting/dialog_integration.h"
#include "include/ui/setting/dialog_manage_routes.h"
#include "include/ui/setting/dialog_otp_manager.h"
#include "include/ui/setting/dialog_dpi_bypass.h"
#include "include/ui/setting/dialog_preset_settings.h"
#include "include/ui/setting/dialog_vpn_settings.h"
#include "include/ui/setting/RawRouteItem.h"
#include "include/ui/setting/RouteItem.h"
#include "include/database/RoutesRepo.h"

#ifdef Q_OS_WIN
#include <WinCommander.hpp>
#include "include/sys/windows/WinVersion.h"
#endif

void MainWindow::on_menu_basic_settings_triggered() {
    USE_DIALOG(DialogBasicSettings)
}

void MainWindow::openLogSettings() {
    if (dialog_is_using) return;
    setDialogInUse(true);
    auto *dialog = new DialogBasicSettings(this);
    dialog->showLoggingPage();
    connect(dialog, &QDialog::finished, this, [=, this] {
        dialog->deleteLater();
        setDialogInUse(false);
    });
    dialog->show();
}

void MainWindow::on_menu_manage_groups_triggered() {
    USE_DIALOG(DialogManageGroups)
}

void MainWindow::on_menu_routing_settings_triggered() {
    if (dialog_is_using) return;
    setDialogInUse(true);
    auto dialog = new DialogManageRoutes(this);
    connect(dialog, &QDialog::finished, this, [=, this] {
        dialog->deleteLater();
        setDialogInUse(false);
    });
    dialog->show();
}

void MainWindow::openRouteProfileEditor(int routeId) {
    // Also keeps the routing dialog from saving its stale copy over this edit.
    if (dialog_is_using) return;
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(routeId);
    if (profile == nullptr) return;
    setDialogInUse(true);

    const auto onSaved = [this](const std::shared_ptr<Configs::RouteProfile> &chain) {
        if (!Configs::dataManager->routesRepo->Save(chain)) return;
        if (chain->id == Configs::dataManager->settingsRepo->current_route_id)
            connection()->noteRestartNeeded(Engine::RestartReason::Routing);
    };
    QDialog *editor;
    if (profile->isRaw) {
        auto *raw = new RawRouteItem(this, profile);
        connect(raw, &RawRouteItem::settingsChanged, this, onSaved);
        editor = raw;
    } else {
        auto *item = new RouteItem(this, profile);
        connect(item, &RouteItem::settingsChanged, this, onSaved);
        editor = item;
    }
    connect(editor, &QDialog::finished, this, [=, this] {
        editor->deleteLater();
        setDialogInUse(false);
    });
    editor->setWindowModality(Qt::ApplicationModal);
    editor->show();
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

void MainWindow::on_menu_integration_settings_triggered() {
    if (dialog_is_using) return;
    setDialogInUse(true);
    auto dialog = new DialogIntegration(this, getActionsForShortcut());
    connect(dialog, &QDialog::finished, this, [=, this] {
        dialog->deleteLater();
        setDialogInUse(false);
    });
    dialog->show();
}

void MainWindow::saveWindowState() {
    auto *settings = Configs::dataManager->settingsRepo.get();

    if (simpleModeActive)
        rememberSimpleGeometry();
    else
        settings->mainWindowGeometry = saveGeometry().toBase64();
    if (!simpleModeActive && !isMaximized()) {
        auto news = QString("%1x%2").arg(size().width()).arg(size().height());
        if (settings->mw_size != news) settings->mw_size = news;
    }
    settings->splitter_state = ui->splitter->saveState().toBase64();
}

void MainWindow::on_commitDataRequest() {
    saveWindowState();
    exitController()->persistSessionState();
}

void MainWindow::on_menu_exit_triggered() {
    // A teardown already running quits on its own; a second request would cut its nested waits short.
    if (Configs::dataManager->settingsRepo->prepare_exit || exitController()->exitInProgress()) return;
    exitController()->setExitInProgress(true);
    // A request made during the teardown below may overwrite exit_reason.
    const auto reason = exit_reason;
    const bool unattended = m_exitUnattended;
    const bool restart = reason == ExitReason::Restart || reason == ExitReason::RestartWithTun ||
                         reason == ExitReason::RestartElevated;
    const auto program = QApplication::applicationFilePath();
    QStringList arguments;
    if (restart) {
        arguments = Configs::dataManager->settingsRepo->argv;
        if (arguments.length() > 0) {
            arguments.removeFirst();
            arguments.removeAll("-tray");
            arguments.removeAll("-flag_restart_tun_on");
        }
        if (reason == ExitReason::RestartWithTun) arguments << "-flag_restart_tun_on";
        if (unattended && m_exitKeepTray) arguments << "-tray";
    }

    bool relaunched = false;
#ifdef Q_OS_WIN
    if (reason == ExitReason::RestartWithTun || reason == ExitReason::RestartElevated) {
        // Asked before the teardown so a declined UAC prompt leaves this instance running; the new one waits for this pid to exit.
        const auto elevatedArguments = arguments + QStringList{QString("-wait_pid=%1").arg(QCoreApplication::applicationPid())};
        const bool launched = WinCommander::runProcessElevated(program, elevatedArguments, QApplication::applicationDirPath(), 1, false) == 0;
        if (!launched) {
            exitController()->setExitInProgress(false);
            exit_reason = ExitReason::None;
            MW_show_log(tr("Restart as administrator was cancelled, Throne keeps running"));
            return;
        }
        relaunched = true;
    }
#endif

    QString updaterProgram;
    QString updaterWorkingDirectory;
    QStringList updaterArguments;
    if (reason == ExitReason::RunUpdater) {
        updaterWorkingDirectory = QApplication::applicationDirPath();
        const QDir applicationDir(updaterWorkingDirectory);
        QString updaterLanguage = QStringLiteral("en");
        switch (Configs::dataManager->settingsRepo->language) {
            case 2:
                updaterLanguage = QStringLiteral("zh");
                break;
            case 3:
                updaterLanguage = QStringLiteral("fa");
                break;
            case 4:
                updaterLanguage = QStringLiteral("ru");
                break;
            case 0:
                updaterLanguage = QLocale::system().name().section(QChar('_'), 0, 0);
                break;
            default:
                break;
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
            exitController()->setExitInProgress(false);
            exit_reason = ExitReason::None;
            return;
        }
#else
        updaterProgram = applicationDir.absoluteFilePath(QStringLiteral("updater"));
#endif
    }

    Engine::host()->shutdown(!unattended);
    if (reason == ExitReason::RunUpdater) {
        if (!QProcess::startDetached(updaterProgram, updaterArguments, updaterWorkingDirectory)) {
            LOG_ERROR(QStringLiteral("Could not start updater: %1").arg(updaterProgram));
            MessageBoxWarning(tr("Update"), tr("Could not start the updater. Start Throned again manually."));
        }
    } else if (restart && !relaunched) {
        QDir::setCurrent(QApplication::applicationDirPath());
        // Until this process exits it holds the data dir lock and still answers on the instance socket.
        arguments << QString("-wait_pid=%1").arg(QCoreApplication::applicationPid());
        QProcess::startDetached(program, arguments);
    }
    QCoreApplication::quit();
}

void MainWindow::request_unattended_exit(ExitReason reason) {
    if (Configs::dataManager->settingsRepo->prepare_exit || exitController()->exitInProgress()) return;
    m_exitUnattended = true;
    m_exitKeepTray = isHidden();
    exit_reason = reason;
    on_menu_exit_triggered();
}

void MainWindow::toggle_system_proxy() {
    auto currentState = Configs::dataManager->settingsRepo->spmode_system_proxy;
    if (currentState) {
        connection()->setSystemProxy(false);
    } else {
        connection()->setSystemProxy(true);
    }
}

void MainWindow::toggle_tun() {
    const auto state = connection()->connectionState();
    if (state == ConnectionState::Connecting || state == ConnectionState::Stopping) return;
    connection()->setTun(!Configs::dataManager->settingsRepo->spmode_vpn);
}

bool MainWindow::StopVPNProcess() {
    supervisor()->killForPrivilegeChange();
    return true;
}

void MainWindow::show_startstop_menu() {
    if (!Configs::dataManager->settingsRepo->kill_switch) return;
    QMenu menu(this);
    connect(menu.addAction(tr("Disable Kill Switch")), &QAction::triggered, this, [this] { confirm_disable_kill_switch(); });
    auto *button = ui->toolButton_startstop;
    menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void MainWindow::confirm_disable_kill_switch() {
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Disable kill switch"),
                                tr("Turn off the kill switch? Traffic that does not go through Throne will no longer be blocked."),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto *disable = box->addButton(tr("Disable Kill Switch"), QMessageBox::DestructiveRole);
    auto *cancel = box->addButton(QMessageBox::Cancel);
    // Cancel is the default, so a stray Enter cannot complete the two-step disable.
    box->setDefaultButton(cancel);
    box->setEscapeButton(cancel);
    connect(box, &QMessageBox::finished, this, [this, box, disable] {
        if (box->clickedButton() == disable) connection()->disableKillSwitch();
    });
    box->open();
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
    if (spl[1].contains("beta") || spl[1].contains("alpha") || spl[1].contains("rc")) version += "." + spl[1];
    auto parts = version.split("."); // [1,2,3,beta,13]
    auto currentParts = QString(NKR_VERSION).replace("-", ".").split('.');
    if (parts.size() < 3 || currentParts.size() < 3) {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }
    std::vector<int> verNums;
    std::vector<int> currNums;
    verNums.push_back(parts[0].toInt());
    verNums.push_back(parts[1].toInt());
    verNums.push_back(parts[2].toInt());
    if (parts.size() > 3) {
        if (parts[3] == "alpha") verNums.push_back(1);
        if (parts[3] == "beta") verNums.push_back(2);
        if (parts[3] == "rc") verNums.push_back(3);
        if (parts.size() > 4) verNums.push_back(parts[4].toInt());
    }

    currNums.push_back(currentParts[0].toInt());
    currNums.push_back(currentParts[1].toInt());
    currNums.push_back(currentParts[2].toInt());
    if (currentParts.size() > 3) {
        if (currentParts[3] == "alpha") currNums.push_back(1);
        if (currentParts[3] == "beta") currNums.push_back(2);
        if (currentParts[3] == "rc") currNums.push_back(3);
        if (currentParts.size() > 4) currNums.push_back(currentParts[4].toInt());
    }

    if (verNums.size() < 3 || currNums.size() < 3) {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }

    for (int i = 0; i < 3; i++) {
        if (verNums[i] > currNums[i]) return true;
        if (verNums[i] < currNums[i]) return false;
    }

    if (verNums.size() == 5 && currNums.size() == 3) return false;
    if (verNums.size() == 3 && currNums.size() == 5) return true;
    if (verNums.size() == 5 && currNums.size() == 5) {
        for (int i = 3; i < 5; i++) {
            if (verNums[i] > currNums[i]) return true;
            if (verNums[i] < currNums[i]) return false;
        }
    } else {
        MW_show_log("There are no updates. You have the latest version - " + QString(NKR_VERSION));
        return false;
    }
    return false;
}

constexpr auto dashboardDownloadURL = "https://github.com/SagerNet/sing-box-dashboard/archive/a9d068d22a6cff77dbb2f803e7469209f2e76af4.zip";

} // namespace

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
        Engine::Dashboard::SeedApi();
        // Fragment, not query: browsers never send it to the server.
        QUrl url(QString("http://127.0.0.1:%1/dashboard/throne.html").arg(port));
        url.setFragment(QString("secret=%1&url=127.0.0.1:%2")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(Configs::dataManager->settingsRepo->core_box_api_secret)))
                            .arg(port),
                        QUrl::StrictMode);
        QDesktopServices::openUrl(url);
    };

    Engine::Dashboard::SeedApi();
    if (QFile::exists(QDir(Configs::apiDashboardDir).filePath("index.html"))) {
        show();
        return;
    }

    if (QMessageBox::question(this, tr("Web dashboard"),
                              tr("The dashboard is not installed yet. Download it now?")) != QMessageBox::StandardButton::Yes) {
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
#ifdef Q_PROCESSOR_ARM_64
    search = "windows-arm64";
#else
#ifdef Q_OS_WIN64
    if (WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1809))
        search = "windows64";
    else
        search = "windowslegacy64";
#else
    search = "windows32";
#endif
#endif
#endif
#ifdef Q_OS_LINUX
#ifdef Q_PROCESSOR_X86_64
    search = "linux-amd64";
#else
    search = "linux-arm64";
#endif
#endif
#ifdef Q_OS_MACOS
#ifdef Q_PROCESSOR_X86_64
    search = "macos-amd64";
#else
    search = "macos-arm64";
#endif
#endif
    if (search.isEmpty()) {
        if (!silent) runOnUiThread([=, this] {
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

    // Releases carry no checksum or signature, so TLS is all that vouches for the download URL and the archive.
    HttpGetOptions options;
    options.strictTls = true;
    options.useProxy = requestUsedProfile;
    auto resp = NetworkRequestHelper::HttpGet("https://api.github.com/repos/troshkindm/throned/releases", options);
    if (!resp.error.isEmpty()) {
        rememberDirectFailure();
        if (!silent) runOnUiThread([=, this] {
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
    for (const QJsonValue value: array) {
        QJsonObject release = value.toObject();
        if (release["prerelease"].toBool() && !Configs::dataManager->settingsRepo->allow_beta_update) continue;
        for (const QJsonValue asset: release["assets"].toArray()) {
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
        if (!silent) runOnUiThread([=, this] {
            MessageBoxInfo(QObject::tr("Update"), QObject::tr("No update"));
        });
        return;
    }

    const auto showUpdatePrompt = [=, this] {
        // The updater replaces files a background instance from this installation still holds open.
        const auto headless = Sys::HostPresence::OtherHeadless();
        const bool headlessSameInstall = headless && Sys::HostPresence::SameInstall(*headless, QCoreApplication::applicationDirPath());
        auto allow_updater = !Configs::dataManager->settingsRepo->flag_use_appdata && !headlessSameInstall;
        const QString notes = headlessSameInstall
                                  ? tr("Update is unavailable while a background Throned (PID %1) runs from this installation; stop it first.")
                                            .arg(headless->pid) +
                                        QStringLiteral("\n\n") + release_note
                                  : release_note;
        const auto choice = ShowUpdatePrompt(this, QObject::tr("Update") + note_pre_release,
                                             assets_name, notes, allow_updater);
        //
        if (choice == UpdatePromptChoice::Update) {
            startUpdateDownload(release_download_url, assets_name);
        } else if (choice == UpdatePromptChoice::OpenInBrowser) {
            QDesktopServices::openUrl(QUrl(release_url));
        }
    };

    runOnUiThread([=, this] {
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
                if (complete)
                    updateStatusWidget->showPreparing(assetName);
                else
                    updateStatusWidget->showDownloading(assetName, received, total);
            });
        };

        const bool proxyAvailable = Configs::dataManager->settingsRepo->started_id >= 0;
        const QString error = NetworkRequestHelper::DownloadAsset(
            url, QStringLiteral("Throned.zip"), proxyAvailable, progress, true);
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
        QStringLiteral("access_token"),
        QStringLiteral("api-key"),
        QStringLiteral("api_key"),
        QStringLiteral("apikey"),
        QStringLiteral("auth"),
        QStringLiteral("authorization"),
        QStringLiteral("key"),
        QStringLiteral("password"),
        QStringLiteral("secret"),
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
    for (const auto &[key, value]: query.queryItems(QUrl::FullyDecoded)) {
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
    for (const int groupID: Configs::dataManager->groupsRepo->GetAllGroupIds()) {
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
    for (const auto &secret: secrets) {
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
        QStringLiteral("access_token"),
        QStringLiteral("api-key"),
        QStringLiteral("api_key"),
        QStringLiteral("apikey"),
        QStringLiteral("auth"),
        QStringLiteral("authorization"),
        QStringLiteral("client_key"),
        QStringLiteral("cookie"),
        QStringLiteral("key"),
        QStringLiteral("password"),
        QStringLiteral("private_key"),
        QStringLiteral("proxy-authorization"),
        QStringLiteral("secret"),
        QStringLiteral("set-cookie"),
        QStringLiteral("token"),
    };
    return keys.contains(key.toLower());
}

QJsonValue redactJsonForDiagnostics(const QJsonValue &value, const bool redactValues = false) {
    if (redactValues && !value.isArray() && !value.isObject()) return QStringLiteral("[redacted]");
    if (value.isArray()) {
        QJsonArray result;
        for (const auto &item: value.toArray()) result.append(redactJsonForDiagnostics(item, redactValues));
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
} // namespace

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

    out << QString("Tun: %1 | System proxy: %2")
               .arg(onOff(settings.spmode_vpn), onOff(settings.spmode_system_proxy));
    if (const auto route = Configs::dataManager->routesRepo->GetRouteProfile(settings.current_route_id)) {
        out << QString("Route profile: %1").arg(route->name);
    }
    const auto running = runningSnapshot();
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
    out << QString("  removed Hijack DNS setting: %1").arg(onOff(settings.enable_dns_server));
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
