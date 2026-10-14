#include "include/ui/preview/MainWindowCapture.h"

#include "include/configs/generate.h"
#include "include/database/DatabaseManager.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/entities/AppRoutes.h"
#include "include/global/GuiUtils.hpp"
#include "include/ui/mainwindow.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "include/ui/widget/SimpleModeControls.h"
#include "include/ui/widget/SimpleRoutesPage.h"

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>

namespace UiPreview {
void VerifyCompactUx(MainWindow *window, const QString &prefix) {
    auto *settings = Configs::dataManager->settingsRepo.get();
    auto *action = window->findChild<QAction *>(QStringLiteral("simpleModeAction"));
    auto *routesRow = window->findChild<SimpleListRow *>(QStringLiteral("simpleRoutesRow"));
    auto *page = window->findChild<SimpleRoutesPage *>();
    auto *maximize = window->findChild<QToolButton *>(QStringLiteral("titleMaximize"));
    if (!action || !routesRow || !page || !maximize) {
        qWarning("Compact UX: missing production controls");
        qApp->exit(2);
        return;
    }
    auto a = Configs::RoutesRepo::NewRouteProfile();
    auto b = Configs::RoutesRepo::NewRouteProfile();
    a->name = QStringLiteral("Example route A");
    b->name = QStringLiteral("Example route B");
    a->applyProfileRules = false;
    a->defaultOutboundID = window->get_profile_to_start();
    b->defaultOutboundID = Configs::directID;
    auto advanced = std::make_shared<Configs::RouteRule>();
    advanced->type = Configs::custom;
    advanced->name = QStringLiteral("throned-app:advanced");
    advanced->domain_suffix = {QStringLiteral("advanced.example")};
    advanced->port = {QStringLiteral("443")};
    advanced->outboundID = Configs::directID;
    a->Rules.append(advanced);
    if (a->defaultOutboundID < 0 || !Configs::dataManager->routesRepo->AddRouteProfile(a) ||
        !Configs::dataManager->routesRepo->AddRouteProfile(b)) {
        qWarning("Compact UX: could not create isolated routing fixtures");
        qApp->exit(2);
        return;
    }
    window->restoreGeometry(window->saveGeometry());
    const QSize fullSize = window->size();
    settings->current_route_id = a->id;
    action->setChecked(true);
    auto *steps = new QTimer(window);
    steps->setInterval(250);
    QObject::connect(steps, &QTimer::timeout, window,
                     [=, step = 0, compactSize = QSize(), fullGeometry = QString()]() mutable {
                         const auto require = [steps](bool ok, const char *reason) {
                             if (ok) return true;
                             qWarning("Compact UX: %s", reason);
                             steps->stop();
                             qApp->exit(2);
                             return false;
                         };
                         auto *apply = page->findChild<QPushButton *>(QStringLiteral("simpleRouteApply"));
                         if (step == 0) {
                             if (!require(window->width() < 600 && maximize->isHidden(), "compact mode must keep a portrait window")) return;
                             window->resize(430, 690);
                         } else if (step == 1) {
                             compactSize = window->size();
                             fullGeometry = settings->mainWindowGeometry;
                             window->hide();
                             window->resize(fullSize);
                             window->on_commitDataRequest();
                             window->show();
                         } else if (step == 2) {
                             if (!require(window->size() == compactSize && settings->mainWindowGeometry == fullGeometry,
                                          "tray restore must recover the user's compact size without replacing full geometry")) return;
                             window->resize(450, 640);
                         } else if (step == 3) {
                             compactSize = window->size();
                             window->showMinimized();
                         } else if (step == 4) {
                             ActivateWindow(window);
                         } else if (step == 5) {
                             if (!require(window->size() == compactSize && !window->isMinimized(), "minimize and restore must retain compact geometry")) return;
                             window->showMaximized();
                         } else if (step == 6) {
                             if (!require(!window->isMaximized() && window->size() == compactSize,
                                          "a window-manager maximize must not stretch the compact interface")) return;
                             action->setChecked(false);
                         } else if (step == 7) {
                             if (!require(window->size() == fullSize && !maximize->isHidden(), "full mode must restore its independent window geometry")) return;
                             settings->simple_window_geometry = settings->mainWindowGeometry;
                             action->setChecked(true);
                         } else if (step == 8) {
                             if (!require(window->width() <= 560, "legacy wide compact geometry must be bounded")) return;
                             window->resize(380, 680);
                             routesRow->click();
                         } else if (step == 9) {
                             if (!require(page->isVisible() && page->isDirty(), "disabled profile rules must offer Apply")) return;
                             page->addEntries({QStringLiteral("a.example"), QStringLiteral("192.0.2.0/24"),
                                               QStringLiteral("processName:example-client"), QStringLiteral("processPath:/opt/example/client")});
                             page->back();
                             settings->current_route_id = b->id;
                             routesRow->click();
                         } else if (step == 10) {
                             if (!require(page->stagedRoutes().isEmpty() && !page->isDirty(), "switching routing profiles must not copy another profile's draft")) return;
                             page->addEntries({QStringLiteral("b.example")});
                             apply->click();
                             Configs::RoutesRepo persisted(Configs::dataManager->getDatabase());
                             const auto savedA = persisted.GetRouteProfile(a->id);
                             const auto savedB = persisted.GetRouteProfile(b->id);
                             if (!require(savedA && savedB && !savedA->applyProfileRules && savedA->Rules.size() == 1 &&
                                              Configs::AppRoutes::Read(savedB->Rules).first().domains == QStringList{QStringLiteral("b.example")},
                                          "Apply must persist only the routing profile currently being edited")) return;
                             page->back();
                             settings->current_route_id = a->id;
                             routesRow->click();
                         } else if (step == 11) {
                             if (!require(page->isDirty() && page->stagedRoutes().size() == 1 &&
                                              page->stagedRoutes().first().domains == QStringList{QStringLiteral("a.example")},
                                          "returning to a routing profile must recover its own staged edits")) return;
                             const auto routes = page->stagedRoutes();
                             if (!require(routes.first().processPaths == QStringList{QStringLiteral("/opt/example/client")},
                                          "choosing an executable path must retain its full scope")) return;
                             apply->click();
                             Configs::RoutesRepo persisted(Configs::dataManager->getDatabase());
                             const auto saved = persisted.GetRouteProfile(a->id);
                             if (!require(saved && saved->applyProfileRules && saved->defaultOutboundID == a->defaultOutboundID && !page->isDirty(),
                                          "Apply must enable profile rules and preserve a named default server")) return;
                             bool preserved = false;
                             for (const auto &rule: saved->Rules)
                                 preserved = preserved || (rule->name == advanced->name && rule->port == advanced->port && rule->domain_suffix == advanced->domain_suffix);
                             if (!require(preserved, "compact Apply must preserve rules extended in the advanced editor")) return;
                             auto *area = qobject_cast<QScrollArea *>(page->scrollArea());
                             if (!require(area->widget()->width() <= area->viewport()->width(), "route content must fit the default compact width")) return;
                             window->grab().save(prefix + QStringLiteral("-applied.png"));
                         } else if (step == 12) {
                             const auto selected = Configs::dataManager->profilesRepo->GetProfile(window->get_profile_to_start());
                             const auto built = Configs::BuildSingBoxConfig(selected, Configs::ConfigBuildPurpose::Preview);
                             if (!require(built && built->error.isEmpty(), "saved compact routes must generate a valid connection configuration")) return;
                             bool domain = false, cidr = false, process = false, path = false, preserved = false;
                             for (const auto &value: built->coreConfig.value(QStringLiteral("route")).toObject().value(QStringLiteral("rules")).toArray()) {
                                 const auto rule = value.toObject();
                                 if (rule.value(QStringLiteral("domain_suffix")).toArray().contains(QStringLiteral("a.example"))) {
                                     domain = true;
                                     if (!require(rule.value(QStringLiteral("process_name")).toArray().isEmpty() && rule.value(QStringLiteral("process_path")).toArray().isEmpty(),
                                                  "domain rules must not require a process match")) return;
                                 }
                                 cidr = cidr || rule.value(QStringLiteral("ip_cidr")).toArray().contains(QStringLiteral("192.0.2.0/24"));
                                 process = process || rule.value(QStringLiteral("process_name")).toArray().contains(QStringLiteral("example-client"));
                                 path = path || rule.value(QStringLiteral("process_path")).toArray().contains(QStringLiteral("/opt/example/client"));
                                 preserved = preserved || (rule.value(QStringLiteral("domain_suffix")).toArray().contains(QStringLiteral("advanced.example")) &&
                                                           rule.value(QStringLiteral("port")).toArray().contains(443));
                             }
                             if (!require(domain && cidr && process && path && preserved, "generated configuration must contain each saved route and advanced restriction")) return;
                             ProfilesTableModel model;
                             model.setRowStyle(ProfilesTableModel::RowStyle::Comfortable);
                             if (!require(model.columnOfSort(GroupSortMethod::ByName) == ProfilesTableModel::ColcServer &&
                                              model.sortOfColumn(ProfilesTableModel::ColcTraffic) == GroupSortMethod::ByTraffic &&
                                              model.sortOfColumn(ProfilesTableModel::ColcPing) == GroupSortMethod::ByLatency,
                                          "persistent upstream sorting must match the fork's comfortable columns")) return;
                             model.setRowStyle(ProfilesTableModel::RowStyle::Compact);
                             if (!require(model.columnOfSort(GroupSortMethod::ByName) == ProfilesTableModel::ColName,
                                          "compact table columns must retain their original sorting mapping")) return;
                             action->setChecked(false);
                             window->showMaximized();
                         } else if (step == 13) {
                             if (!require(window->isMaximized(), "full mode must still support maximizing")) return;
                             action->setChecked(true);
                         } else if (step == 14) {
                             window->resize(440, 650);
                         } else if (step == 15) {
                             compactSize = window->size();
                             window->hide();
                             window->show();
                         } else {
                             if (!require(window->size() == compactSize, "entering from a maximized full window must still remember subsequent compact resizes")) return;
                             action->setChecked(false);
                             if (!require(window->isMaximized(), "returning to full mode must retain its maximized state")) return;
                             qInfo("Compact UX: geometry, profile drafts, persistence and generated routing passed");
                             steps->stop();
                             qApp->exit(0);
                         }
                         ++step;
                     });
    steps->start();
}
} // namespace UiPreview
