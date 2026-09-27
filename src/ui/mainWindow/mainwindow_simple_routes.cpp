#include "include/ui/mainwindow.h"
#include "include/ui/mainWindow/MainWindowInternal.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QPointer>
#include <QStackedWidget>
#include <QThreadPool>
#include <QTimer>

#include <utility>

#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/configs/RuleSetCache.h"
#include "include/database/entities/AppRoutes.h"
#include "include/ui/setting/ApplicationPickerDialog.h"
#include "include/ui/widget/RoutingQuickMenu.hpp"
#include "include/ui/widget/SimpleRoutesPage.h"

namespace {
QString latencyLabel(const std::shared_ptr<Configs::Profile> &profile) {
    if (profile->latency == Configs::kLatencyConnectOnly) return MainWindow::tr("Connect OK");
    if (profile->latency < 0) return MainWindow::tr("Unavailable");
    return profile->latency > 0 ? MainWindow::tr("%1 ms").arg(profile->latency) : QString();
}

QList<Configs::AppRoutes::Program> programsOf(const QList<InstalledApplications::Entry> &programs) {
    QList<Configs::AppRoutes::Program> list;
    for (const auto &program: programs) list.append({program.name, program.executable, program.path, program.package, program.running});
    return list;
}

QHash<QString, bool> matchCatalog(const QList<InstalledApplications::Entry> &programs) {
    return Configs::AppRoutes::Detect(Configs::AppRoutes::Catalog(), programsOf(programs));
}
} // namespace

void MainWindow::setupSimpleRoutes() {
    simpleRoutesPage = new SimpleRoutesPage(simplePages);
    simplePages->addWidget(simpleRoutesPage);
    connect(simpleRoutesPage, &SimpleRoutesPage::back, this, [this] { simplePages->setCurrentIndex(0); });
    connect(simpleRoutesPage, &SimpleRoutesPage::openAdvanced, this, &MainWindow::openCurrentRouteEditor);
    connect(simpleRoutesPage, &SimpleRoutesPage::wholeComputerRequested, this, &MainWindow::openSimpleModeSheet);
    connect(simpleRoutesPage, &SimpleRoutesPage::pickApplication, this, [this] {
        ApplicationPickerDialog picker(this);
        if (picker.exec() == QDialog::Accepted) simpleRoutesPage->addEntries(picker.selectedRules());
    });
    connect(simpleRoutesPage, &SimpleRoutesPage::ruleSetsNeeded, this, [this](const QStringList &names) { downloadSimpleRuleSets(names, {}); });
    connect(simpleRoutesPage, &SimpleRoutesPage::retryDownload, this, [this] { downloadSimpleRuleSets(simpleRoutesPage->neededRuleSets(), {}); });
    connect(simpleRoutesPage, &SimpleRoutesPage::applyRequested, this, [this](const QList<Configs::AppRoutes::Route> &routes, int rest) {
        // A profile naming a list that is not on disk would make the core fetch it while connecting, and fail there.
        downloadSimpleRuleSets(simpleRoutesPage->neededRuleSets(), [this, routes, rest](bool ready) {
            if (ready) applySimpleRoutes(routes, rest);
        });
    });
}

void MainWindow::applySimpleRoutes(const QList<Configs::AppRoutes::Route> &routes, int rest) {
    {
        auto profile = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
        if (!profile || profile->isRaw) return;
        Configs::AppRoutes::Write(profile->Rules, routes);
        profile->defaultOutboundID = rest;
        Configs::dataManager->routesRepo->Save(profile);
        if (!Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview")))
            applyRoutingChange();
        else
            refreshRoutingStatus();
        reloadSimpleRoutes();
    }
}

void MainWindow::downloadSimpleRuleSets(const QStringList &names, const std::function<void(bool)> &then) {
    const bool preview = Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview"));
    const QStringList missing = preview ? QStringList() : Configs::RuleSetCache::Missing(names);
    if (missing.isEmpty()) {
        if (!simpleRuleSetBusy) simpleRoutesPage->setDownload(0, 0, {});
        if (then) then(true);
        return;
    }
    if (simpleRuleSetBusy) {
        // One download at a time; the latest request runs once the current one ends.
        simpleRuleSetNext = names;
        simpleRuleSetNextThen = then;
        return;
    }
    simpleRuleSetBusy = true;
    simpleRoutesPage->setDownload(0, int(missing.size()), {});
    const bool viaProxy = Configs::dataManager->settingsRepo->started_id >= 0;
    const QPointer<MainWindow> guard(this);
    runOnNewThread([guard, missing, viaProxy, then] {
        QString error;
        int done = 0;
        for (const QString &name: missing) {
            error = Configs::RuleSetCache::Download(name, viaProxy);
            if (!error.isEmpty()) break;
            ++done;
            if (!guard) return;
            QMetaObject::invokeMethod(guard.data(), [guard, done, total = int(missing.size())] {
                if (guard) guard->simpleRoutesPage->setDownload(done, total, {}); }, Qt::QueuedConnection);
        }
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, error, done, total = int(missing.size()), then] {
            if (!guard) return;
            guard->simpleRuleSetBusy = false;
            guard->simpleRoutesPage->setDownload(done, total, error);
            if (error.isEmpty())
                QTimer::singleShot(1500, guard.data(), [guard] {
                    if (guard && !guard->simpleRuleSetBusy) guard->simpleRoutesPage->setDownload(0, 0, {});
                });
            if (then) then(error.isEmpty());
            if (!guard->simpleRuleSetNext.isEmpty()) {
                const QStringList next = std::exchange(guard->simpleRuleSetNext, {});
                guard->downloadSimpleRuleSets(next, std::exchange(guard->simpleRuleSetNextThen, {}));
            } }, Qt::QueuedConnection);
    });
}

void MainWindow::refreshStaleRuleSets() {
    if (Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview"))) return;
    const QStringList stale = Configs::RuleSetCache::Stale(simpleRoutesPage->neededRuleSets());
    if (stale.isEmpty()) return;
    const bool viaProxy = Configs::dataManager->settingsRepo->started_id >= 0;
    // Quietly: a working list stays in place when its refresh fails.
    runOnNewThread([stale, viaProxy] {
        for (const QString &name: stale) Configs::RuleSetCache::Download(name, viaProxy);
    });
}

void MainWindow::openSimpleRoutes() {
    // Staged edits survive a trip back to the main page; only a clean page follows the saved profile.
    if (!simpleRoutesPage->isDirty()) reloadSimpleRoutes();
    scanSimpleApps();
    downloadSimpleRuleSets(simpleRoutesPage->neededRuleSets(), {});
    refreshStaleRuleSets();
    simplePages->setCurrentWidget(simpleRoutesPage);
    simpleRoutesPage->scrollArea()->setFocus();
}

void MainWindow::reloadSimpleRoutes() {
    if (simpleRoutesPage == nullptr) return;
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
    if (profile)
        simpleRoutesPage->load(Configs::AppRoutes::Read(profile->Rules), profile->defaultOutboundID,
                               Configs::AppRoutes::ForeignRuleCount(profile->Rules), !profile->isRaw);
    else
        simpleRoutesPage->load({}, Configs::directID, 0, false);

    const auto main = running != nullptr ? running : Configs::dataManager->profilesRepo->GetProfile(simpleProfileId);
    SimpleRouteTarget mainTarget;
    QList<SimpleRouteTarget> others;
    if (main != nullptr) {
        mainTarget = {Configs::proxyID, main->outbound->DisplayName(), latencyLabel(main), main->latency};
        if (const auto group = Configs::dataManager->groupsRepo->GetGroup(main->gid)) {
            for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
                if (profile == nullptr || profile->id == main->id || profile->outbound == nullptr) continue;
                others.append({profile->id, profile->outbound->DisplayName(), latencyLabel(profile), profile->latency});
            }
        }
    }
    simpleRoutesPage->setTargets(mainTarget, others);
    simpleRoutesPage->setWholeComputer(Configs::dataManager->settingsRepo->spmode_vpn);
    simpleRoutesPage->setConnected(running != nullptr);
}

void MainWindow::scanSimpleApps() {
    if (simpleAppScanStarted) return;
    simpleAppScanStarted = true;
    // Previews must not read the machine they run on, so they get a fixed set of programs.
    if (Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview"))) {
        simpleDetectedApps = matchCatalog({{.name = QStringLiteral("Telegram Desktop"), .executable = QStringLiteral("Telegram.exe"), .running = true},
                                           {.name = QStringLiteral("Discord"), .executable = QStringLiteral("Discord.exe"), .running = true},
                                           {.name = QStringLiteral("Steam"), .executable = QStringLiteral("steam.exe")},
                                           {.name = QStringLiteral("Spotify"), .executable = QStringLiteral("Spotify.exe")}});
        simpleRoutesPage->setDetected(simpleDetectedApps);
        return;
    }
    const QPointer<MainWindow> guard(this);
    QThreadPool::globalInstance()->start([guard] {
        const auto programs = programsOf(InstalledApplications::Scan());
        const auto found = Configs::AppRoutes::Detect(Configs::AppRoutes::Catalog(), programs);
        const auto icons = Configs::AppRoutes::IconPaths(Configs::AppRoutes::Catalog(), programs);
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, found, icons] {
            if (!guard) return;
            guard->simpleDetectedApps = found;
            guard->simpleRoutesPage->setDetected(found);
            guard->loadSimpleIcons(icons); }, Qt::QueuedConnection);
    });
}

void MainWindow::loadSimpleIcons(QHash<QString, QString> paths) {
    if (paths.isEmpty() || simpleRoutesPage == nullptr) return;
    // The shell call behind an icon can stall, so one icon per event-loop turn keeps the screen responsive.
    const QString id = paths.begin().key();
    const QString path = paths.take(id);
    const QIcon icon = QFileIconProvider().icon(QFileInfo(path));
    if (!icon.isNull()) simpleRoutesPage->setIcon(id, icon);
    QTimer::singleShot(0, this, [this, paths] { loadSimpleIcons(paths); });
}

QString MainWindow::simpleRoutesSummary() const {
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
    if (!profile || profile->isRaw) return RoutingQuickMenu::statusSummary();
    const int apps = Configs::AppRoutes::Read(profile->Rules).size();
    const QString rest = profile->defaultOutboundID == Configs::directID ? tr("everything else directly") : tr("everything else through the VPN");
    return apps == 0 ? rest.left(1).toUpper() + rest.mid(1) : tr("%n app(s)", nullptr, apps) + QStringLiteral(" · ") + rest;
}
