#include "include/ui/mainwindow.h"
#include "include/ui/mainWindow/MainWindowInternal.h"

#include <QStackedWidget>

#include <utility>

#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/entities/AppRoutes.h"
#include "include/ui/setting/ApplicationPickerDialog.h"
#include "include/ui/utils/AppRoutesController.h"
#include "include/ui/widget/RoutingQuickMenu.hpp"
#include "include/ui/widget/SimpleRoutesPage.h"
#include "include/ui/widget/StackTransition.h"

void MainWindow::setupSimpleRoutes() {
    simpleRoutesPage = new SimpleRoutesPage(simplePages);
    simpleRoutesPage->setAnimated(!AppRoutesController::Preview());
    simplePages->addWidget(simpleRoutesPage);
    simpleRoutesController = new AppRoutesController(simpleRoutesPage);
    simplePageTransition = new StackTransition(simplePages);
    simplePageTransition->setAnimated(!AppRoutesController::Preview());
    connect(simpleRoutesPage, &SimpleRoutesPage::back, this, [this] { simplePageTransition->switchTo(simplePages->widget(0), false); });
    connect(simpleRoutesPage, &SimpleRoutesPage::openAdvanced, this, &MainWindow::openCurrentRouteEditor);
    connect(simpleRoutesPage, &SimpleRoutesPage::wholeComputerRequested, this, &MainWindow::openSimpleModeSheet);
    connect(simpleRoutesPage, &SimpleRoutesPage::pickApplication, this, [this] {
        ApplicationPickerDialog picker(this);
        if (picker.exec() == QDialog::Accepted) simpleRoutesPage->addEntries(picker.selectedRules());
    });
    connect(simpleRoutesPage, &SimpleRoutesPage::applyRequested, this, [this](const QList<Configs::AppRoutes::Route> &routes, int rest) {
        // A profile naming a list that is not on disk would make the core fetch it while connecting, and fail there.
        const int profileId = simpleRoutesProfileId;
        simpleRoutesController->downloadRuleSets(simpleRoutesPage->neededRuleSets(), [this, profileId, routes, rest](bool ready) {
            if (ready) applySimpleRoutes(profileId, routes, rest);
        });
    });
}

void MainWindow::applySimpleRoutes(int profileId, const QList<Configs::AppRoutes::Route> &routes, int rest) {
    {
        auto profile = Configs::dataManager->routesRepo->GetRouteProfile(profileId);
        if (!profile || profile->isRaw) return;
        Configs::AppRoutes::Write(profile->Rules, routes);
        profile->defaultOutboundID = rest;
        profile->applyProfileRules = true;
        Configs::dataManager->routesRepo->Save(profile);
        const auto applied = std::make_pair(routes, rest);
        if (simpleRouteDrafts.value(profileId) == applied) simpleRouteDrafts.remove(profileId);
        if (simpleRoutesProfileId == profileId) {
            simpleRoutesPage->markApplied(routes, rest);
            if (simpleRoutesPage->isDirty())
                simpleRouteDrafts.insert(profileId, {simpleRoutesPage->stagedRoutes(), simpleRoutesPage->stagedDefault()});
            else
                simpleRouteDrafts.remove(profileId);
        }
        if (Configs::dataManager->settingsRepo->current_route_id == profileId) {
            if (!AppRoutesController::Preview())
                applyRoutingChange();
            else
                refreshRoutingStatus();
        }
    }
}

void MainWindow::openSimpleRoutes() {
    // Staged edits survive a trip back to the main page; only a clean page follows the saved profile.
    if (simpleRoutesProfileId != Configs::dataManager->settingsRepo->current_route_id || !simpleRoutesPage->isDirty()) reloadSimpleRoutes();
    simpleRoutesController->scanApps();
    simpleRoutesController->downloadRuleSets(simpleRoutesPage->neededRuleSets());
    simpleRoutesController->refreshStaleRuleSets();
    simplePageTransition->switchTo(simpleRoutesPage, true);
    simpleRoutesPage->scrollArea()->setFocus();
}

void MainWindow::reloadSimpleRoutes() {
    if (simpleRoutesPage == nullptr) return;
    const int profileId = Configs::dataManager->settingsRepo->current_route_id;
    const bool changedProfile = simpleRoutesProfileId != profileId;
    if (simpleRoutesProfileId >= 0 && simpleRoutesProfileId != profileId && simpleRoutesPage->isDirty())
        simpleRouteDrafts.insert(simpleRoutesProfileId, {simpleRoutesPage->stagedRoutes(), simpleRoutesPage->stagedDefault()});
    simpleRoutesProfileId = profileId;
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(profileId);
    if (profile)
        simpleRoutesPage->load(Configs::AppRoutes::Read(profile->Rules), profile->defaultOutboundID,
                               Configs::AppRoutes::ForeignRuleCount(profile->Rules), !profile->isRaw, profile->applyProfileRules, changedProfile);
    else
        simpleRoutesPage->load({}, Configs::directID, 0, false, true, changedProfile);

    if (const auto draft = simpleRouteDrafts.constFind(profileId); draft != simpleRouteDrafts.cend())
        simpleRoutesPage->setDraft(draft->first, draft->second);
    const auto running = runningSnapshot();
    simpleRoutesController->setTargets(running != nullptr ? running : Configs::dataManager->profilesRepo->GetProfile(simpleProfileId));
    simpleRoutesPage->setWholeComputer(Configs::dataManager->settingsRepo->spmode_vpn);
    simpleRoutesPage->setConnected(running != nullptr);
}

QString MainWindow::simpleRoutesSummary() const {
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
    if (!profile || profile->isRaw) return RoutingQuickMenu::statusSummary();
    if (!profile->applyProfileRules) return tr("Profile rules are disabled");
    const int apps = Configs::AppRoutes::Read(profile->Rules).size();
    QString rest;
    if (profile->defaultOutboundID == Configs::directID)
        rest = tr("everything else directly");
    else if (profile->defaultOutboundID == Configs::proxyID)
        rest = tr("everything else through the VPN");
    else if (profile->defaultOutboundID == Configs::blockID)
        rest = tr("everything else blocked");
    else if (profile->defaultOutboundID == Configs::warpBypassID)
        rest = tr("everything else through WARP bypass");
    else if (const auto target = Configs::dataManager->profilesRepo->GetProfile(profile->defaultOutboundID); target && target->outbound)
        rest = tr("everything else through %1").arg(target->outbound->DisplayName());
    else
        rest = tr("Default server is missing");
    return apps == 0 ? rest.left(1).toUpper() + rest.mid(1) : tr("%n app(s)", nullptr, apps) + QStringLiteral(" · ") + rest;
}
