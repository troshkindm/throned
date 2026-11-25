#include "include/ui/preview/UpstreamPreview.h"
#include "include/ui/preview/GeometryReport.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/database/DatabaseManager.h"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/generate.h"
#include "include/configs/common/Outbound.h"
#include "include/configs/common/DialFields.h"
#include "include/database/IpScansRepo.h"
#include "include/ui/scanner/dialog_scan_instance.h"
#include "include/ui/setting/dialog_integration.h"
#include "include/ui/setting/dialog_manage_routes.h"
#include "include/ui/setting/dialog_rule_tester.h"
#include "include/ui/setting/ProcessPicker.h"
#include "include/ui/group/dialog_edit_group_advanced.h"
#include "include/ui/profile/dialog_edit_profile.h"
#include "include/engine/EngineHost.h"

#include <QApplication>
#include <QMenu>
#include <functional>
#include <memory>
#include <QJsonArray>
#include <QDebug>
#include <QTemporaryDir>
#include <QTimer>
#include <QPixmap>
#include <QTabWidget>

namespace UiPreview {
namespace {
bool verifyDatabaseMigration() {
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    const auto path = (directory.path() + "/migration.db").toStdString();
    int groupId = -1;
    {
        Configs::Database db(path);
        Configs::GroupsRepo groups(db);
        Configs::ProfilesRepo profiles(db);
        auto group = Configs::GroupsRepo::NewGroup();
        group->name = "Legacy subscription";
        group->url = "https://subscription.example/config";
        group->info = "upload=100; download=200; total=1000";
        group->provider.announce = "Example announcement";
        group->provider.announceSeen = "dismissed";
        group->provider.updateIntervalMinutes = 180;
        group->provider.notifyExpiry = false;
        group->id = 1;
        if (!groups.Save(group)) return false;
        groupId = group->id;
        auto profile = Configs::ProfilesRepo::NewProfile("direct");
        profile->id = 71;
        profile->gid = groupId;
        profile->favorite = true;
        profile->traffic_downlink = 12345;
        // Save only updates an existing row and leaves the favourite mark alone, so the legacy row is inserted first.
        Configs::ProfileInsertRow row{};
        row.id = profile->id;
        row.type = "direct";
        row.gid = groupId;
        row.outbound_json = "{}";
        row.favorite = profile->favorite;
        db.execBatchInsertProfiles({row});
        if (!profiles.Save(profile)) return false;
        for (const auto *column: {"endpoint_json", "sub_options_json", "sub_metadata_json", "sort_method", "sort_descending"})
            db.execThrow(std::string("ALTER TABLE groups DROP COLUMN ") + column);
        db.execThrow("ALTER TABLE profiles DROP COLUMN endpoint_json");
    }
    const Configs::EndpointSource endpoint{Configs::EndpointSource::Mode::Address, "192.0.2.1"};
    {
        Configs::Database db(path);
        Configs::GroupsRepo groups(db);
        Configs::ProfilesRepo profiles(db);
        const auto group = groups.GetGroup(groupId);
        const auto profile = profiles.GetProfile(71);
        if (!group || !profile || !profile->favorite || profile->traffic_downlink != 12345 ||
            group->provider.announceSeen != "dismissed" || group->provider.notifyExpiry ||
            !group->sub_info.valid || group->sub_info.used() != 300) return false;
        if (group->sort != GroupSortAction{}) return false;
        group->sort = {GroupSortMethod::ByLatency, true};
        group->endpoint = endpoint;
        group->sub_options.update_interval = 90;
        group->sub_options.respect_server_interval = false;
        group->sub_info.server_interval = 4;
        profile->endpoint = endpoint;
        if (!groups.Save(group) || !profiles.Save(profile)) return false;
        std::vector<Configs::ProfileInsertRow> rows;
        for (int i = 0; i < 160; ++i) {
            Configs::ProfileInsertRow row{};
            row.id = 100 + i;
            row.type = "direct";
            row.gid = groupId;
            row.favorite = (i % 2 == 0);
            row.outbound_json = "{}";
            row.endpoint_json = R"({"mode":"address","address":"192.0.2.1"})";
            rows.push_back(row);
        }
        db.execBatchInsertProfiles(rows);
        rows.back().favorite = true;
        db.execBatchReplaceProfiles({rows.back()});
        auto query = db.queryThrow("SELECT COUNT(*) FROM profiles WHERE id >= 100 AND favorite = 1 AND endpoint_json LIKE '%192.0.2.1%'");
        if (!query->executeStep() || query->getColumn(0).getInt() != 81) return false;
    }
    Configs::Database db(path);
    Configs::GroupsRepo groups(db);
    Configs::ProfilesRepo profiles(db);
    const auto group = groups.GetGroup(groupId);
    const auto profile = profiles.GetProfile(71);
    if (!group || !profile || !profile->favorite || profile->endpoint != endpoint ||
        group->endpoint != endpoint || group->provider.updateIntervalMinutes != 180 ||
        group->sub_options.update_interval != 90 || group->sub_info.server_interval != 4 ||
        group->sort != GroupSortAction{GroupSortMethod::ByLatency, true}) return false;
    auto &settings = *Configs::dataManager->settingsRepo;
    const auto previous = settings.sub_auto_update;
    settings.sub_auto_update = 0;
    // A group's own interval stands alone; one that only inherits follows the global switch off.
    const bool ownStands = Subscription::ResolveAutoUpdate(*group).interval == 90 * 60;
    group->sub_options.update_interval = 0;
    group->provider.updateIntervalMinutes = 0;
    const bool disabled = ownStands && Subscription::ResolveAutoUpdate(*group).interval == 0;
    group->sub_options.update_interval = 90;
    group->provider.updateIntervalMinutes = 180;
    settings.sub_auto_update = 60;
    const bool overridden = Subscription::ResolveAutoUpdate(*group).interval == 90 * 60;
    group->sub_options.respect_server_interval = true;
    const bool fromServer = Subscription::ResolveAutoUpdate(*group).interval == 4 * 3600;
    settings.sub_auto_update = previous;
    return disabled && overridden && fromServer;
}

bool captureUpstream(QWidget *widget, const QString &path) {
    SaveGeometryReport(widget, path);
    return widget->grab().save(path, "PNG");
}

bool verifyDirectDns() {
    auto profile = Configs::ProfilesRepo::NewProfile("direct");
    auto group = Configs::GroupsRepo::NewGroup();
    group->name = "Direct DNS verification";
    if (!Configs::dataManager->groupsRepo->AddGroup(group) ||
        !Configs::dataManager->profilesRepo->AddProfile(profile, group->id)) return false;
    const auto plain = Configs::BuildSingBoxConfig(profile);
    if (!plain->error.isEmpty()) return false;
    const auto remoteServer = [](const QJsonObject &config) {
        for (const auto &value: config.value("dns").toObject().value("servers").toArray()) {
            const auto server = value.toObject();
            if (server.value("tag").toString() == "dns-remote") return server;
        }
        return QJsonObject{};
    };
    const auto plainDns = remoteServer(plain->coreConfig);
    if (plainDns.isEmpty() || plainDns.contains("detour")) return false;
    profile->outbound->dialFields->inet4_bind_address = "192.0.2.1";
    const auto bound = Configs::BuildSingBoxConfig(profile);
    return bound->error.isEmpty() && remoteServer(bound->coreConfig).value("detour").toString() == "proxy";
}
} // namespace

// Screens the November 2026 upstream merge brought in; each is shown, captured and closed in turn.
static void captureNovemberScreens(QWidget *parent, const QString &prefix) {
    struct Step {
        QString name;
        std::function<QWidget *()> open;
    };
    auto steps = std::make_shared<QList<Step>>();
    // The merge folded the Tor page away; the exit choice it fed now lives on the Exit page.
    *steps << Step{QStringLiteral("routing-exit"), [parent]() -> QWidget * {
                       auto *routing = new DialogManageRoutes(parent);
                       auto *tabs = routing->findChild<QTabWidget *>(QStringLiteral("routeSettingsPages"));
                       auto *exit = routing->findChild<QWidget *>(QStringLiteral("tab_5"));
                       if (!tabs || !exit) {
                           delete routing;
                           return nullptr;
                       }
                       tabs->setCurrentWidget(exit);
                       return routing;
                   }};
    *steps << Step{QStringLiteral("rule-tester"), [parent]() -> QWidget * {
                       auto *host = Engine::host();
                       return host ? new DialogRuleTester(host->connection(), -1, parent) : nullptr;
                   }};
    *steps << Step{QStringLiteral("process-picker"), [parent]() -> QWidget * {
                       return new ProcessPicker(parent, {ProcessPicker::Match::Name, ProcessPicker::Match::Path});
                   }};
    *steps << Step{QStringLiteral("group-filters"), [parent]() -> QWidget * {
                       return new DialogEditGroupAdvanced(Configs::SubscriptionOptions{}, 0, Configs::EndpointSource{}, true, 12, parent);
                   }};
    for (const auto *menuName: {"menuRouting_Menu", "menu_server", "menuTesting"}) {
        *steps << Step{QString::fromLatin1(menuName), [parent, menuName]() -> QWidget * {
                           auto *menu = parent->findChild<QMenu *>(QString::fromLatin1(menuName));
                           if (menu == nullptr) return nullptr;
                           // aboutToShow is where the menus refresh their entries' state.
                           emit menu->aboutToShow();
                           menu->popup(parent->mapToGlobal(QPoint(40, 40)));
                           return menu;
                       }};
    }
    for (const auto *type: {"dnstt", "masterdns"}) {
        *steps << Step{QString::fromLatin1(type), [parent, type]() -> QWidget * {
                           const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
                           if (!group) return nullptr;
                           auto *editor = new DialogEditProfile(QString::fromLatin1(type), group->id, parent);
                           editor->setInitialCommonFields(QStringLiteral("Example tunnel"), QStringLiteral("t.example.com"), QStringLiteral("53"));
                           return editor;
                       }};
    }
    auto next = std::make_shared<std::function<void(int)>>();
    *next = [steps, next, prefix](int index) {
        if (index >= steps->size()) {
            qApp->exit(0);
            return;
        }
        const auto &step = steps->at(index);
        QWidget *widget = step.open();
        if (widget == nullptr) {
            qWarning() << "Upstream preview: could not open" << step.name;
            qApp->exit(2);
            return;
        }
        widget->show();
        const QString name = step.name;
        QTimer::singleShot(400, widget, [widget, next, prefix, name, index] {
            const bool ok = captureUpstream(widget, prefix + "-" + name + ".png");
            widget->hide();
            if (!qobject_cast<QMenu *>(widget)) widget->deleteLater();
            if (!ok) {
                qApp->exit(2);
                return;
            }
            (*next)(index + 1);
        });
    };
    (*next)(0);
}

void CaptureUpstreamFeatures(QWidget *parent, const QString &prefix) {
    if (!verifyDatabaseMigration() || !verifyDirectDns()) {
        qCritical() << "Upstream database migration, subscription scheduling or Direct DNS generation failed";
        qApp->exit(2);
        return;
    }
    auto scan = Configs::IpScansRepo::NewIpScan(Configs::IpScan::Kind::Resolver);
    scan->name = QStringLiteral("Preview resolvers");
    if (!Configs::dataManager->ipScansRepo->AddIpScan(scan)) {
        qApp->exit(2);
        return;
    }
    QTimer::singleShot(0, parent, [parent, prefix, scanId = scan->id] {
        DialogScanInstance::Open(scanId);
        DialogScanInstance *scanWindow = nullptr;
        for (auto *widget: QApplication::topLevelWidgets())
            if (auto *window = qobject_cast<DialogScanInstance *>(widget)) scanWindow = window;
        if (!scanWindow) {
            qApp->exit(2);
            return;
        }
        QTimer::singleShot(300, scanWindow, [scanWindow, parent, prefix] {
            if (!captureUpstream(scanWindow, prefix + "-scan-editor.png")) {
                qApp->exit(2);
                return;
            }
            scanWindow->close();
            auto *integration = new DialogIntegration(parent);
            auto *tabs = integration->findChild<QTabWidget *>("tabWidget");
            if (tabs) tabs->setCurrentIndex(tabs->count() - 1);
            integration->show();
            QTimer::singleShot(300, integration, [integration, parent, prefix] {
                const bool ok = captureUpstream(integration, prefix + "-integration.png");
                integration->hide();
                integration->deleteLater();
                if (!ok) {
                    qApp->exit(2);
                    return;
                }
                captureNovemberScreens(parent, prefix);
            });
        });
    });
}
} // namespace UiPreview
