#include "include/sys/AutoRun.hpp"

#include <QCryptographicHash>
#include <QDir>
#include "include/global/Configs.hpp"
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>

#include <WinCommander.hpp>

QString taskNameFor(const QString &exePath, const QString &brand) {
    const QString cleanPath = QDir::cleanPath(exePath).toLower();
    QByteArray hash = QCryptographicHash::hash(cleanPath.toUtf8(), QCryptographicHash::Md5).toHex();

    QString shortHash = QString(hash).left(8);

    return QString("%1 AutoRun %2").arg(brand, shortHash);
}

QString GetTaskName() {
    return taskNameFor(QCoreApplication::applicationFilePath(), "Throned");
}

QString getCurrentUser() {
    QString domain = qEnvironmentVariable("USERDOMAIN");
    QString user = qEnvironmentVariable("USERNAME");
    return domain + "\\" + user;
}

void enable_autorun() {
    QString taskName = GetTaskName();
    QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QString userId = getCurrentUser();

    QString runLevel = (Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin) ? "HighestAvailable" : "LeastPrivilege";

    QString xmlContent = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        "  <RegistrationInfo>\n"
        "    <Author>%1</Author>\n"
        "  </RegistrationInfo>\n"
        "  <Triggers>\n"
        "    <LogonTrigger>\n"
        "      <Enabled>true</Enabled>\n"
        "    </LogonTrigger>\n"
        "  </Triggers>\n"
        "  <Principals>\n"
        "    <Principal id=\"Author\">\n"
        "      <GroupId>S-1-5-32-545</GroupId>\n"
        "      <RunLevel>%2</RunLevel>\n"
        "    </Principal>\n"
        "  </Principals>\n"
        "  <Settings>\n"
        "    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
        "    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
        "    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
        "    <AllowHardTerminate>false</AllowHardTerminate>\n"
        "    <StartWhenAvailable>false</StartWhenAvailable>\n"
        "    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\n"
        "    <IdleSettings>\n"
        "      <StopOnIdleEnd>true</StopOnIdleEnd>\n"
        "      <RestartOnIdle>false</RestartOnIdle>\n"
        "    </IdleSettings>\n"
        "    <AllowStartOnDemand>true</AllowStartOnDemand>\n"
        "    <Enabled>true</Enabled>\n"
        "    <Hidden>false</Hidden>\n"
        "    <RunOnlyIfIdle>false</RunOnlyIfIdle>\n"
        "    <WakeToRun>false</WakeToRun>\n"
        "    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
        // Task Scheduler's default of 7 is BELOW_NORMAL_PRIORITY_CLASS, which the core process inherits from us; 4-6 are the interactive band.
        "    <Priority>5</Priority>\n"
        "  </Settings>\n"
        "  <Actions Context=\"Author\">\n"
        "    <Exec>\n"
        "      <Command>\"%3\"</Command>\n"
        "    </Exec>\n"
        "  </Actions>\n"
        "</Task>"
    ).arg(userId, runLevel, exePath);

    QString xmlFilePath = QDir::toNativeSeparators(QDir::tempPath() + "\\Throned_Task.xml");
    QFile xmlFile(xmlFilePath);
    if (xmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&xmlFile);
        out.setEncoding(QStringConverter::Utf16);
        out.setGenerateByteOrderMark(true);
        out << xmlContent;
        xmlFile.close();
    }

    QStringList args;
    args << "/create" << "/tn" << taskName << "/xml" << "\"" + xmlFilePath + "\"" << "/f";

    QProcess process;
    process.start("schtasks.exe", args);
    process.waitForFinished();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        WinCommander::runProcessElevated("schtasks.exe", args, "", 0, true);
    }
}

void disable_autorun() {
    QString taskName = GetTaskName();

    QStringList args;
    args << "/delete" << "/tn" << taskName << "/f";

    QProcess process;
    process.start("schtasks.exe", args);
    process.waitForFinished();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        WinCommander::runProcessElevated("schtasks.exe", args, "", 0, true);
    }
}

void AutoRun_SetEnabled(bool enable) {
    if (enable) {
        enable_autorun();
    } else {
        disable_autorun();
    }
}

bool AutoRun_IsEnabled() {
    QString taskName = GetTaskName();

    QProcess process;
    process.start("schtasks.exe", QStringList() << "/query" << "/tn" << taskName << "/xml");
    process.waitForFinished();

    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
        if (!output.contains("xml")) return false;
        if (output.contains("<Enabled>false</Enabled>", Qt::CaseInsensitive)) return false;
        return true;
    }

    return false;
}

// Older tasks carry priority 7, and a missing element means the same; a deliberate bump to a higher priority is left alone.
bool autoRun_priorityIsStale(const QString &xml) {
    static const QRegularExpression re("<Priority>\\s*(\\d+)\\s*</Priority>");
    auto match = re.match(xml);
    if (!match.hasMatch()) return true;
    return match.captured(1).toInt() > 6;
}

void AutoRun_FixTaskIfNeeded() {
    QString taskName = GetTaskName();
    QString runLevel = (Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin) ? "HighestAvailable" : "LeastPrivilege";

    QProcess process;
    process.start("schtasks.exe", QStringList() << "/query" << "/tn" << taskName << "/xml");
    process.waitForFinished();

    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
        if (!output.contains("xml")) return;
        bool privilegeIsStale = (runLevel == "HighestAvailable" && !output.contains("HighestAvailable")) || (runLevel == "LeastPrivilege" && output.contains("HighestAvailable"));
        if (privilegeIsStale || autoRun_priorityIsStale(output)) AutoRun_SetEnabled(true);
    }
}

QString windows_GenAutoRunString() {
    auto appPath = QApplication::applicationFilePath();
    appPath = "\"" + QDir::toNativeSeparators(appPath) + "\"";
    appPath += " -tray";
    return appPath;
}

bool old_autoRun_isEnabled() {
    QSettings settings("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);

    auto appPath = QApplication::applicationFilePath();
    QFileInfo fInfo(appPath);
    QString name = fInfo.baseName();

    return settings.value(name).toString() == windows_GenAutoRunString();
}

void AutoRun_MigrateIfNeeded() {
    auto appPath = QApplication::applicationFilePath();
    QFileInfo fInfo(appPath);
    QString name = fInfo.baseName();
    QSettings settings("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);

    bool migrate = old_autoRun_isEnabled();
    if (settings.contains("Throne")) {
        settings.remove("Throne");
        migrate = true;
    }
    if (migrate) settings.remove(name);

    QStringList legacyExecutables{
        QDir(QApplication::applicationDirPath()).absoluteFilePath("Throne.exe"),
    };
    const QFileInfo currentDir(QApplication::applicationDirPath());
    if (currentDir.fileName().compare("Throned", Qt::CaseInsensitive) == 0) {
        legacyExecutables << currentDir.dir().absoluteFilePath("Throne/Throne.exe");
    }
    legacyExecutables.removeDuplicates();

    for (const auto &legacyExe : legacyExecutables) {
        const QString legacyTask = taskNameFor(legacyExe, "Throne");
        QProcess query;
        query.start("schtasks.exe", QStringList() << "/query" << "/tn" << legacyTask);
        query.waitForFinished();
        if (query.exitStatus() != QProcess::NormalExit || query.exitCode() != 0) continue;

        migrate = true;
        QStringList deleteArgs{"/delete", "/tn", legacyTask, "/f"};
        QProcess remove;
        remove.start("schtasks.exe", deleteArgs);
        remove.waitForFinished();
        if (remove.exitStatus() != QProcess::NormalExit || remove.exitCode() != 0) {
            WinCommander::runProcessElevated("schtasks.exe", deleteArgs, "", 0, true);
        }
    }

    if (migrate) AutoRun_SetEnabled(true);
}
