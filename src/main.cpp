#include <csignal>
#include <algorithm>
#include <cstdio>
#include <memory>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMessageBox>
#include <QLocalSocket>
#include <QLocalServer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"
#include "include/global/Notice.hpp"

#include "include/control/ControlClient.h"
#include "include/engine/EngineHost.h"
#include "include/engine/app/Bootstrap.h"
#include "include/engine/app/ConfigDump.h"
#include "include/engine/app/DataDirLock.h"
#include "include/engine/app/ExitCodes.h"
#include "include/engine/app/InstanceServer.h"
#include "include/engine/headless/HeadlessMain.h"
#include "include/sys/Process.hpp"
#include "include/sys/ServiceManager.hpp"
#include "include/ui/app/GuiStartup.h"
#include "include/ui/mainwindow.h"
#include "include/ui/preview/UiPreview.h"
#include "include/api/RPC.h"

#ifdef Q_OS_WIN
#include "include/sys/windows/MiniDump.h"
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <include/sys/linux/coreDump.h>
#endif

void signal_handler(int signum) {
    Q_UNUSED(signum)
    // The running teardown quits on its own; re-entering it from one of its nested loops would deadlock on mu_exit.
    if (Engine::Bootstrap::ExitInProgress()) return;
    if (auto *h = Engine::host()) h->shutdown();
    QCoreApplication::quit();
}

namespace {
// The standalone screenshot modes; -1 when the arguments ask for none of them.
int RunStandalonePreview(QApplication &app) {
    const QStringList arguments = app.arguments();
    const auto previewLocale = [&arguments](const QMap<QString, QString> &locales) {
        if (const int langAt = arguments.indexOf(QStringLiteral("-lang")); langAt >= 0 && langAt + 1 < arguments.size()) {
            if (const QString locale = locales.value(arguments.at(langAt + 1).toLower()); !locale.isEmpty())
                GuiStartup::LoadTranslation(locale);
        }
    };

    if (arguments.contains(QStringLiteral("--update-prompt-preview"))) {
        previewLocale({{"en", "en_US"}, {"zh", "zh_CN"}, {"fa", "fa_IR"}, {"ru", "ru_RU"}});
        UiPreview::ApplyTheme(app);
        QString note;
        if (const int at = arguments.indexOf(QStringLiteral("--notes")); at >= 0 && at + 1 < arguments.size())
            note = ReadFileText(arguments.at(at + 1));
        if (note.isEmpty()) {
            note = QStringLiteral(
                "<!-- throned:lang=en -->\n"
                "## What's new\n\n"
                "- A redesigned main window with clearer profile states and a collapsible activity panel.\n"
                "- Quick Add accepts a subscription or profile link and also opens manual creation.\n"
                "- Update downloads now show unobtrusive progress.\n\n"
                "## Fixed\n\n"
                "- Existing groups survive an empty or invalid subscription response.\n"
                "<!-- throned:lang=end -->\n\n"
                "<details>\n<summary>Русский</summary>\n\n"
                "<!-- throned:lang=ru -->\n"
                "## Что нового\n\n"
                "- Обновлённое главное окно с понятным состоянием профилей и сворачиваемой нижней панелью.\n"
                "- Быстрое добавление принимает ссылку подписки или профиля и открывает ручное создание.\n"
                "- Загрузка обновления теперь показывает ненавязчивый прогресс.\n\n"
                "## Исправлено\n\n"
                "- Пустой или некорректный ответ подписки больше не удаляет существующую группу.\n"
                "<!-- throned:lang=end -->\n"
                "</details>");
        }
        bool previewSaved = true;
        if (const int outputAt = arguments.indexOf(QStringLiteral("--output")); outputAt >= 0 && outputAt + 1 < arguments.size()) {
            const QString output = arguments.at(outputAt + 1);
            previewSaved = false;
            QTimer::singleShot(500, &app, [&app, &previewSaved, output] {
                QWidget *prompt = QApplication::activeModalWidget();
                previewSaved = prompt != nullptr && prompt->grab().save(output, "PNG");
                if (prompt != nullptr)
                    prompt->close();
                else
                    app.exit(2);
            });
        }
        ShowUpdatePrompt(nullptr, QObject::tr("Update"), QStringLiteral("Throned-preview-windows64.zip"), note, true);
        return previewSaved ? 0 : 2;
    }

    if (arguments.contains(QStringLiteral("--route-editor-preview"))) {
        previewLocale({{"zh", "zh_CN"}, {"fa", "fa_IR"}, {"ru", "ru_RU"}});
        return UiPreview::RunRouteEditor(app);
    }
    return -1;
}

// Screenshot mode must never inherit or copy a real profile, so -appdata is pointed at a private, empty one.
bool UsePreviewWorkdir(QStringList &arguments, std::unique_ptr<QTemporaryDir> &workdir) {
    workdir = std::make_unique<QTemporaryDir>();
    if (!workdir->isValid()) return false;
    const QDir previewDir(workdir->path());
    if (!previewDir.mkpath(QStringLiteral("config"))) return false;
    QFile emptyDb(previewDir.absoluteFilePath(QStringLiteral("config/throne.db")));
    if (!emptyDb.open(QIODevice::WriteOnly)) return false;
    emptyDb.close();
    if (const int at = arguments.indexOf(QStringLiteral("-appdata")); at >= 0) {
        arguments.removeAt(at);
        if (at < arguments.size() && !arguments.at(at).startsWith('-')) arguments.removeAt(at);
    }
    arguments << QStringLiteral("-appdata") << workdir->path();
    return true;
}

// Before the data dir is resolved: that step creates directories and copies a legacy profile, which a query must never do.
int RunControlClient(const QStringList &arguments, int cliAt) {
    QString appdataDir;
    bool useAppdata = false;
    if (const int at = arguments.indexOf(QStringLiteral("-appdata")); at >= 0) {
        useAppdata = true;
        if (at + 1 < arguments.size() && !arguments.at(at + 1).startsWith('-')) appdataDir = arguments.at(at + 1);
    }
#ifdef NKR_CPP_USE_APPDATA
    useAppdata = true;
#endif
    const QDir installWd(QCoreApplication::applicationDirPath());
    const QDir appdataWd(appdataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : appdataDir);
    // Which of the two an instance picked depends on whether the install folder was writable, so both are tried.
    QStringList candidates{Engine::Bootstrap::InstanceServerName(useAppdata ? appdataWd : installWd)};
    if (!useAppdata) candidates << Engine::Bootstrap::InstanceServerName(appdataWd);
    return ThronedControl::RunClient(candidates, arguments.mid(cliAt + 1));
}
} // namespace

int main(int argc, char *argv[]) {
    // The screenshot harness reads a failed preview assertion off stderr, and its log lives in a temporary dir that is gone by then.
    const bool uiPreviewRun = std::any_of(argv, argv + argc, [](const char *a) { return qstrcmp(a, "-ui-preview") == 0; });
    if (!uiPreviewRun) Logging::InstallQtMessageHandler();

    // Ahead of the crash handler: its MessageBoxW would hang a displayless run.
    auto launch = Engine::Bootstrap::PreScan(argc, argv);
#ifdef Q_OS_WIN
    if (launch.service) {
        if (!Sys::ServiceManager::StartServiceDispatcher()) {
            Engine::Bootstrap::AttachParentConsole();
            if (_fileno(stderr) >= 0) {
                std::fputs(
                    "--service: Throned was not started by the Windows Service Control Manager; install the service "
                    "first, or run without --service\n",
                    stderr);
                std::fflush(stderr);
            }
            return Engine::ExitCode::Usage;
        }
        // Session 0 shows no window: --service alone runs headless too.
        if (launch.mode == Engine::Bootstrap::Mode::Gui) launch.mode = Engine::Bootstrap::Mode::Headless;
    }
#endif
    if (launch.mode == Engine::Bootstrap::Mode::DumpConfigs) return Engine::RunConfigDump(argc, argv);
    if (launch.mode == Engine::Bootstrap::Mode::Headless || launch.mode == Engine::Bootstrap::Mode::HeadlessClient) {
        const int code = Engine::RunHeadless(argc, argv, launch);
        // Last: once the SCM sees SERVICE_STOPPED it may kill this process and start the recovery instance.
        Sys::ServiceManager::NotifyStopped(code);
        return code;
    }

#ifdef Q_OS_WIN
    Windows_SetCrashHandler(Windows_WindowStationVisible());
#endif
#ifdef Q_OS_LINUX
    enable_core_dumps();
#endif

    // No AA_DontUseNativeDialogs: it costs every file picker the platform dialog and buys nothing here.
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication a(argc, argv);

    if (const int code = RunStandalonePreview(a); code >= 0) return code;
    if (const int cliAt = a.arguments().indexOf(QStringLiteral("--cli")); cliAt >= 0) {
        QCoreApplication::setApplicationName("Throned");
        return RunControlClient(a.arguments(), cliAt);
    }

    SetNoticeSink([](const Notice &n) { ShowPassiveWarning(n.title, n.text); });
    Engine::Bootstrap::SetSoftwareNames();

    GuiStartup::InstallMacOpenEventFilter(a);
    GuiStartup::LoadEmojiFont();

    QStringList arguments = QApplication::arguments();
    std::unique_ptr<QTemporaryDir> uiPreviewWorkdir;
    if (uiPreviewRun && !UsePreviewWorkdir(arguments, uiPreviewWorkdir)) return 2;

    const Engine::Bootstrap::RelaunchParent relaunchParent(arguments);
    // Must run before the working directory moves below: argument paths may be relative to it.
    const QString launchDeeplink = Deeplink_ExtractFromArgs(arguments);
    const QStringList launchFiles = LaunchFiles_ExtractFromArgs(arguments, QDir::current());

    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) {
        QFile::remove("updater.old");
    }

    const auto dataDir = Engine::Bootstrap::ResolveDataDir(arguments, QDir(QCoreApplication::applicationDirPath()));
    const QDir &wd = dataDir.wd;
    Configs::SetBasePath(wd.absolutePath());

    const auto serverName = Engine::Bootstrap::InstanceServerName(wd);
    qDebug() << "server name: " << serverName;
    QStringList launchPayload;
    if (!launchDeeplink.isEmpty()) launchPayload << launchDeeplink;
    for (const auto &file: launchFiles) launchPayload << QUrl::fromLocalFile(file).toString();

    // Before the temp wipe and initDB: the owner of the lock owns the data dir.
    Engine::DataDirLock dataDirLock;
    const auto claim = Engine::Bootstrap::ClaimDataDir(dataDirLock, dataDir, serverName, "gui", launchPayload, relaunchParent);
    if (claim.outcome == Engine::Bootstrap::ClaimOutcome::HandedOff) return 0;
    if (claim.outcome == Engine::Bootstrap::ClaimOutcome::HeldByHeadless) {
        const auto pid = claim.ownerPid;
        const QString text = pid > 0 ? QObject::tr("Throned is running in the background (PID %1). Stop the service first.").arg(pid)
                                     : QObject::tr("Throned is running in the background. Stop the service first.");
        std::fputs(qPrintable(QString("%1 Data directory: %2\n").arg(text, QDir::toNativeSeparators(wd.absolutePath()))), stderr);
        if (!arguments.contains("-tray")) QMessageBox::information(nullptr, "Throned", text);
        return 0;
    }
    if (claim.outcome == Engine::Bootstrap::ClaimOutcome::HeldByOther) {
        const auto pid = claim.ownerPid;
        // An autostart must not leave a box behind.
        if (!arguments.contains("-tray")) {
            const QString who = pid > 0 ? QObject::tr("Another Throned instance (PID %1)").arg(pid) : QObject::tr("Another Throned instance");
            QMessageBox::warning(nullptr, "Throned", QObject::tr("%1 is using this data directory:\n%2").arg(who, QDir::toNativeSeparators(wd.absolutePath())));
        }
        return 0;
    }

    // Listening before initDB lets a second launch hand off while migrations run; its payload waits in the pending buffers.
    // This user only: the socket accepts control commands that rewrite routing.
    Engine::InstanceServer server(serverName, QLocalServer::UserAccessOption);
    if (!server.listen()) {
        // Logging::Init has not run yet, so the file log never sees this.
        const QString error = QString("Failed to start the instance server %1: %2").arg(serverName, server.errorString());
        std::fputs(qPrintable(error + '\n'), stderr);
        Engine::Bootstrap::WarnDroppedPayload(launchPayload);
        QMessageBox::critical(nullptr, "Throned", error);
        return 1;
    }
    // Not before this point: until the data dir is ours there is nothing to tear down, and the default action ends the waits above.
#ifdef Q_OS_UNIX
    Engine::Bootstrap::InstallTerminationHandlers(signal_handler);
#endif
    server.setHandlers(GuiStartup::HandleLaunchLine, GuiStartup::HandleLaunchConnected);
    server.installQuitCleanup();

    Engine::Bootstrap::OpenDataDir(dataDir, arguments);
    Engine::Bootstrap::PrepareRuntime();

    LogThread = new QThread;
    LogThread->start();

    GuiStartup::SetupIconTheme();

#ifdef Q_OS_WIN
    if (!uiPreviewRun && GuiStartup::RelaunchElevatedIfNeeded(server, arguments, launchDeeplink, launchFiles)) return 0;
#endif

    if (Configs::dataManager->settingsRepo->start_minimal) Configs::dataManager->settingsRepo->flag_tray = true;

    GuiStartup::ApplyAppearanceArguments(arguments);
    GuiStartup::LoadTranslator();

    // Must follow the single-instance check: opening the log earlier truncates the running instance's file and fakes a crash marker.
    Engine::Bootstrap::StartLogging(dataDir);
    GuiStartup::InstallSessionEndHooks(a, signal_handler);

    API::defaultClient = new API::Client();

    // Before any redesigned widget or stylesheet exists; appearance changes use the same path at runtime.
    GuiStartup::SetupApplicationFont(a);

    Configs_sys::SetIsolatedChildren(true);
    Engine::EngineHost::create();
    auto *window = UI_InitMainWindow();
    if (uiPreviewRun) {
        // No core, no API, no kill switch: a preview only renders the window.
        if (const int previewAt = arguments.indexOf(QStringLiteral("-ui-preview")); previewAt >= 0 && previewAt + 1 < arguments.size()) {
            const QString prefix = arguments.at(previewAt + 1);
            QTimer::singleShot(1200, qApp, [prefix] { UiPreview::RunMainWindow(prefix); });
        }
    } else {
        std::optional<Engine::EngineHost::BootStart> bootStart;
        if (Configs::dataManager->settingsRepo->remember_enable) bootStart = Engine::EngineHost::BootStart{UI_BootStartProfileId()};
        Engine::host()->init(window, window, bootStart);
        GuiStartup::WarnAboutHeadless();
        Configs::dataManager->RunDeferredMaintenance();
    }

    if (Logging::PreviousSessionCrashed()) {
        MW_show_log(QObject::tr("[Warn] Throned did not shut down cleanly last time. "
                                "Diagnostics were saved to: %1")
                        .arg(Logging::LogDir()));
    }

    // The Flush calls replay whatever arrived before the window existed (e.g. a macOS FileOpen event).
    if (!launchDeeplink.isEmpty()) Deeplink_Submit(launchDeeplink);
    Deeplink_FlushPending();
    LaunchFiles_Submit(launchFiles);
    LaunchFiles_FlushPending();

#ifdef Q_OS_UNIX
    // Only right before exec(): a nested loop ahead of it would consume the signal while quit() is still a no-op.
    Engine::Bootstrap::ArmTerminationHandlers([] { signal_handler(0); });
#endif
    return QApplication::exec();
}
