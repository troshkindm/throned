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
#include "include/ui/scanner/dialog_scanner.h"
#include "include/ui/scanner/dialog_new_scan.h"
#include "include/ui/scanner/dialog_scan_instance.h"
#include "include/ui/setting/dialog_integration.h"

#include <QApplication>
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
    const bool disabled = Subscription::ResolveAutoUpdate(*group).interval == 0;
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

void CaptureUpstreamFeatures(QWidget *parent, const QString &prefix) {
    if (!verifyDatabaseMigration() || !verifyDirectDns()) {
        qCritical() << "Upstream database migration, subscription scheduling or Direct DNS generation failed";
        qApp->exit(2);
        return;
    }
    auto *scanner = new DialogScanner(parent);
    scanner->show();
    QTimer::singleShot(300, scanner, [scanner, parent, prefix] {
        const bool ok = captureUpstream(scanner, prefix + "-scanner.png");
        scanner->close();
        if (!ok) {
            qApp->exit(2);
            return;
        }
        auto *newScan = new DialogNewScan(parent);
        newScan->show();
        QTimer::singleShot(300, newScan, [newScan, parent, prefix] {
            const bool ok = captureUpstream(newScan, prefix + "-new-scan.png");
            newScan->accept();
            if (!ok || newScan->CreatedScanId() < 0) {
                qApp->exit(2);
                return;
            }
            DialogScanInstance::Open(newScan->CreatedScanId());
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
                QTimer::singleShot(300, integration, [integration, prefix] {
                    const bool ok = captureUpstream(integration, prefix + "-integration.png");
                    integration->hide();
                    integration->deleteLater();
                    qApp->exit(ok ? 0 : 2);
                });
            });
        });
    });
}
} // namespace UiPreview
