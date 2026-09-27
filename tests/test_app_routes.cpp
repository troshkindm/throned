#include "include/database/ProfilesRepo.h"
#include "include/database/entities/AppRoutes.h"
#include "include/database/entities/RouteProfile.h"

#include <QFile>
#include <QHostAddress>
#include <QSet>
#include <QTest>

#include <srslist.h>

// Link seam: nothing here renders a rule that points at a profile.
namespace Configs {
std::shared_ptr<Profile> ProfilesRepo::GetProfile(int) const { return nullptr; }
} // namespace Configs

using namespace Configs;
using namespace Configs::AppRoutes;

namespace {
std::shared_ptr<RouteRule> foreign(const QString &name) {
    auto rule = std::make_shared<RouteRule>();
    rule->name = name;
    rule->domain_suffix = {name + QStringLiteral(".example")};
    return rule;
}

QStringList names(const QList<std::shared_ptr<RouteRule>> &rules) {
    QStringList result;
    for (const auto &rule: rules) result.append(rule->name);
    return result;
}
} // namespace

class TestAppRoutes : public QObject {
    Q_OBJECT

private slots:
    void shippedCatalogIsUsable() {
        QFile file(QStringLiteral(APP_CATALOG));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray json = file.readAll();
        QString error;
        const auto windows = ParseCatalog(json, QStringLiteral("windows"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(windows.size() >= 100);
        const QSet<QString> categories{"messaging", "social", "ai", "dev", "video", "music", "gaming", "cloud", "work"};
        // Names every program uses: routing by them would send everything through, which is never what an app entry means.
        const QStringList generic{"launcher.exe", "node.exe", "python.exe", "javaw.exe", "java.exe", "chrome.exe", "msedge.exe",
                                  "firefox.exe", "update.exe", "agent.exe", "msedgewebview2.exe", "explorer.exe", "svchost.exe"};
        QSet<QString> ids;
        QHash<QString, QString> processOwner;
        for (const auto &entry: windows) {
            QVERIFY2(!ids.contains(entry.id), qPrintable(entry.id));
            ids.insert(entry.id);
            QVERIFY2(categories.contains(entry.category), qPrintable(entry.id));
            QVERIFY(QColor(entry.color).isValid());
            QVERIFY2(!entry.processes.isEmpty() || !entry.ruleSets.isEmpty() || !entry.domains.isEmpty(), qPrintable(entry.id));
            for (const QString &cidr: entry.cidrs) QVERIFY2(QHostAddress::parseSubnet(cidr).second > 0, qPrintable(cidr));
            for (const QString &process: entry.processes) {
                QVERIFY(process.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive));
                QVERIFY2(!generic.contains(process.toLower()), qPrintable(entry.id + ": " + process));
                // One program belongs to one entry, or "found on this computer" lists the same app twice.
                QVERIFY2(!processOwner.contains(process.toLower()), qPrintable(process + " in " + entry.id + " and " + processOwner.value(process.toLower())));
                processOwner.insert(process.toLower(), entry.id);
            }
            QVERIFY2(entry.detectExecutables.isEmpty() || !entry.detectPaths.isEmpty(), qPrintable(entry.id));
            for (const QString &name: entry.ruleSets) {
                const QByteArray key = name.toUtf8();
                const bool known = std::any_of(ruleSetList.begin(), ruleSetList.end(),
                                               [&key](const auto &item) { return item.first == std::string_view(key.constData(), key.size()); });
                QVERIFY2(known, qPrintable(entry.id + ": " + name));
            }
        }
        const auto onLinux = ParseCatalog(json, QStringLiteral("linux"));
        const auto telegram = std::find_if(onLinux.begin(), onLinux.end(), [](const CatalogEntry &e) { return e.id == "telegram"; });
        QVERIFY(telegram != onLinux.end());
        QVERIFY(telegram->processes.contains(QStringLiteral("telegram-desktop")));
    }

    void writeKeepsForeignRulesInPlace() {
        QList<std::shared_ptr<RouteRule>> rules{foreign("ads"), foreign("work")};
        const QList<Route> routes{
            {.id = "telegram", .processes = {"Telegram.exe"}, .ruleSets = {"geosite-telegram"}, .domains = {"t.me"}, .cidrs = {"91.108.4.0/22"}, .outbound = proxyID},
            {.id = "custom", .domains = {"example.org"}, .outbound = 7},
        };
        Write(rules, routes);
        QCOMPARE(names(rules), QStringList({"throned-app:telegram", "throned-app:telegram:process", "throned-app:custom", "ads", "work"}));
        QCOMPARE(rules.at(1)->process_name, QStringList{"Telegram.exe"});
        QVERIFY(rules.at(1)->domain_suffix.isEmpty());
        QCOMPARE(rules.at(0)->ip_cidr, QStringList{"91.108.4.0/22"});
        QCOMPARE(rules.at(0)->rule_set, QStringList{"geosite-telegram"});
        QVERIFY(rules.at(1)->rule_set.isEmpty());
        QCOMPARE(rules.at(2)->outboundID, 7);
        QCOMPARE(Read(rules), routes);

        // Moved below "ads" in the full editor: a rewrite stays there.
        rules.move(3, 0);
        Write(rules, {{.id = "youtube", .domains = {"youtube.com"}, .outbound = directID}});
        QCOMPARE(names(rules), QStringList({"ads", "throned-app:youtube", "work"}));

        Write(rules, {});
        QCOMPARE(names(rules), QStringList({"ads", "work"}));
    }

    void detectsWhatIsInstalledWithoutGuessing() {
        const QList<CatalogEntry> catalog{
            {.id = "telegram", .name = "Telegram", .detect = {"Telegram Desktop"}, .packages = {"TelegramMessengerLLP.TelegramDesktop"}, .processes = {"Telegram.exe"}},
            {.id = "box", .name = "Box", .detect = {"Box"}, .processes = {"Box.exe"}},
            {.id = "rockstar", .name = "Rockstar", .detectExecutables = {"Launcher.exe"}, .detectPaths = {"Rockstar Games"}},
        };
        QCOMPARE(Detect(catalog, {{.name = "Boxcryptor", .executable = "Boxcryptor.exe"}}).size(), 0);
        QCOMPARE(Detect(catalog, {{.name = "Box Drive", .executable = "BoxUI.exe"}}).keys(), QStringList{"box"});
        QCOMPARE(Detect(catalog, {{.name = "Game", .executable = "Launcher.exe", .path = R"(C:\Games\Other\Launcher.exe)"}}).size(), 0);
        QCOMPARE(Detect(catalog, {{.name = "Launcher", .executable = "Launcher.exe", .path = R"(C:\Program Files\Rockstar Games\Launcher\Launcher.exe)"}}).keys(),
                 QStringList{"rockstar"});
        const auto store = Detect(catalog, {{.name = "TelegramMessengerLLP.TelegramDesktop", .package = "TelegramMessengerLLP.TelegramDesktop_5.1.0.0_x64__t4vj0pshhgkwm"}});
        QCOMPARE(store.value("telegram", true), false);
        QCOMPARE(Detect(catalog, {{.name = "Telegram", .executable = "telegram.exe", .running = true}}).value("telegram"), true);
    }

    void iconComesFromTheProgramItself() {
        const QList<CatalogEntry> catalog{{.id = "telegram", .name = "Telegram", .detect = {"Telegram Desktop"}, .processes = {"Telegram.exe"}},
                                          {.id = "store", .name = "Store only", .packages = {"Vendor.App"}}};
        const auto paths = IconPaths(catalog, {{.name = "Telegram Desktop", .executable = "Updater.exe", .path = "C:/T/Updater.exe"},
                                               {.name = "Telegram", .executable = "Telegram.exe", .path = "C:/T/Telegram.exe"},
                                               {.name = "Vendor.App", .package = "Vendor.App_1_x64__abc"}});
        QCOMPARE(paths.value("telegram"), QString("C:/T/Telegram.exe"));
        QVERIFY(!paths.contains("store"));
    }

    void plumbingIsNotForeign() {
        QList<std::shared_ptr<RouteRule>> rules{foreign("ads")};
        auto local = foreign(QString::fromLatin1(LocalProxyRuleName));
        rules.append(local);
        auto endpoint = std::make_shared<RouteRule>();
        endpoint->type = endpointPreferredBy;
        rules.append(endpoint);
        Write(rules, {{.id = "x", .domains = {"x.com"}, .outbound = proxyID}});
        QCOMPARE(ForeignRuleCount(rules), 1);
    }

    void classifiesWhatTheScreenCanStore() {
        QCOMPARE(Classify("https://www.Example.com/path?q=1"), std::pair(Kind::Domain, QString("example.com")));
        QCOMPARE(Classify("notion.so"), std::pair(Kind::Domain, QString("notion.so")));
        QCOMPARE(Classify("Game.exe"), std::pair(Kind::Process, QString("Game.exe")));
        QCOMPARE(Classify(R"(C:\Games\Some Game\game.exe)"), std::pair(Kind::Process, QString("game.exe")));
        QCOMPARE(Classify("192.0.2.7"), std::pair(Kind::Cidr, QString("192.0.2.7/32")));
        QCOMPARE(Classify("198.51.100.0/24"), std::pair(Kind::Cidr, QString("198.51.100.0/24")));
        QCOMPARE(Classify("keyword:ads").first, Kind::Invalid);
        QCOMPARE(Classify("hello").first, Kind::Invalid);
        QCOMPARE(Classify("   ").first, Kind::Invalid);
    }
};

QTEST_MAIN(TestAppRoutes)
#include "test_app_routes.moc"
