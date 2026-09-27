#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <utility>

namespace Configs {
class RouteRule;

// The Simple mode routing screen: one entry per app, stored as ordinary route rules.
namespace AppRoutes {

// Rules this screen owns carry the prefix; every other rule in the profile is left exactly where it was.
inline constexpr auto RulePrefix = "throned-app:";
inline constexpr auto CustomId = "custom";

struct CatalogEntry {
    QString id;
    QString name;
    QString category;
    QString color;
    QStringList detect;            // installed-application names that mean the app is present
    QStringList detectExecutables; // found-on-this-computer only, never routed: names too generic to route by
    QStringList detectPaths;       // install-path fragments those executables must sit under
    QStringList packages;          // Microsoft Store package family prefixes
    QStringList processes;         // this platform's executable names, routed while the program runs
    QStringList ruleSets;          // srslist.h names; the maintained lists the app routes by
    QStringList domains;           // only where no maintained list covers the service
    QStringList cidrs;
};

// The bundled catalog, with each app's processes narrowed to the running platform.
QList<CatalogEntry> ParseCatalog(const QByteArray &json, const QString &platform, QString *error = nullptr);
QList<CatalogEntry> Catalog();

struct Route {
    QString id;
    QStringList processes;
    QStringList ruleSets;
    QStringList domains;
    QStringList cidrs;
    int outbound = 0;
    [[nodiscard]] bool isEmpty() const { return processes.isEmpty() && ruleSets.isEmpty() && domains.isEmpty() && cidrs.isEmpty(); }
    bool operator==(const Route &) const = default;
};

// A program the computer has: installed, running, behind a Start menu shortcut or a Store package.
struct Program {
    QString name;
    QString executable;
    QString path;
    QString package;
    bool running = false;
};
// Catalog id -> whether one of its routed processes is running right now.
[[nodiscard]] QHash<QString, bool> Detect(const QList<CatalogEntry> &catalog, const QList<Program> &programs);
// Catalog id -> an executable to take the icon from: a routed process first, then the program found by its name.
[[nodiscard]] QHash<QString, QString> IconPaths(const QList<CatalogEntry> &catalog, const QList<Program> &programs);

[[nodiscard]] bool IsOwned(const RouteRule &rule);
[[nodiscard]] QList<Route> Read(const QList<std::shared_ptr<RouteRule>> &rules);
// Replaces the owned rules, keeping them where the first one stood so a hand-made order survives.
void Write(QList<std::shared_ptr<RouteRule>> &rules, const QList<Route> &routes);
[[nodiscard]] int ForeignRuleCount(const QList<std::shared_ptr<RouteRule>> &rules);

enum class Kind { Invalid,
                  Process,
                  Domain,
                  Cidr };
// One typed entry to what the screen can store; keywords, regexes and rule-sets belong to the full editor.
[[nodiscard]] std::pair<Kind, QString> Classify(const QString &input);

} // namespace AppRoutes
} // namespace Configs
