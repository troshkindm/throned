#include "include/database/entities/AppRoutes.h"

#include <QFile>
#include <QRegularExpression>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <iterator>

#include "include/database/entities/RouteProfile.h"

namespace Configs::AppRoutes {
namespace {
constexpr auto ProcessSuffix = ":process";

QStringList strings(const QJsonValue &value) {
    QStringList result;
    for (const auto &item: value.toArray())
        if (const QString text = item.toString().trimmed(); !text.isEmpty()) result.append(text);
    return result;
}

QString currentPlatform() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

std::shared_ptr<RouteRule> makeRule(const QString &name, int outbound) {
    auto rule = std::make_shared<RouteRule>();
    rule->name = name;
    rule->type = custom;
    rule->action = QStringLiteral("route");
    rule->outboundID = outbound;
    return rule;
}
} // namespace

QList<CatalogEntry> ParseCatalog(const QByteArray &json, const QString &platform, QString *error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) *error = parseError.errorString();
        return {};
    }
    QList<CatalogEntry> entries;
    for (const auto &value: document.object().value(QStringLiteral("apps")).toArray()) {
        const QJsonObject app = value.toObject();
        CatalogEntry entry{
            .id = app.value(QStringLiteral("id")).toString(),
            .name = app.value(QStringLiteral("name")).toString(),
            .category = app.value(QStringLiteral("category")).toString(),
            .color = app.value(QStringLiteral("color")).toString(),
            .detect = strings(app.value(QStringLiteral("detect"))),
            .detectExecutables = strings(app.value(QStringLiteral("detectExecutables"))),
            .detectPaths = strings(app.value(QStringLiteral("detectPaths"))),
            .packages = platform == QStringLiteral("windows") ? strings(app.value(QStringLiteral("packages"))) : QStringList(),
            .processes = strings(app.value(QStringLiteral("processes")).toObject().value(platform)),
            .ruleSets = strings(app.value(QStringLiteral("ruleSets"))),
            .domains = strings(app.value(QStringLiteral("domains"))),
            .cidrs = strings(app.value(QStringLiteral("cidrs"))),
        };
        // An id doubles as the rule name, so it cannot collide with the custom entry or the process suffix.
        if (entry.id.isEmpty() || entry.name.isEmpty() || entry.id == QLatin1String(CustomId) || entry.id.contains(QLatin1Char(':')))
            continue;
        if (entry.processes.isEmpty() && entry.ruleSets.isEmpty() && entry.domains.isEmpty() && entry.cidrs.isEmpty()) continue;
        entries.append(entry);
    }
    return entries;
}

QList<CatalogEntry> Catalog() {
    static const QList<CatalogEntry> catalog = [] {
        QFile file(QStringLiteral(":/routing/apps.json"));
        return file.open(QIODevice::ReadOnly) ? ParseCatalog(file.readAll(), currentPlatform()) : QList<CatalogEntry>();
    }();
    return catalog;
}

QHash<QString, bool> Detect(const QList<CatalogEntry> &catalog, const QList<Program> &programs) {
    // Whole words only: "Box" is Box Drive, not Boxcryptor.
    const auto namedAs = [](const QString &name, const QString &wanted) {
        return name.compare(wanted, Qt::CaseInsensitive) == 0 || name.startsWith(wanted + QLatin1Char(' '), Qt::CaseInsensitive);
    };
    QHash<QString, bool> found;
    for (const auto &entry: catalog) {
        for (const auto &program: programs) {
            const bool routed = !program.executable.isEmpty() && entry.processes.contains(program.executable, Qt::CaseInsensitive);
            const bool generic = !program.executable.isEmpty() && entry.detectExecutables.contains(program.executable, Qt::CaseInsensitive) &&
                                 std::any_of(entry.detectPaths.begin(), entry.detectPaths.end(), [&program](const QString &fragment) {
                                     return program.path.contains(fragment, Qt::CaseInsensitive);
                                 });
            const bool named = std::any_of(entry.detect.begin(), entry.detect.end(), [&](const QString &wanted) { return namedAs(program.name, wanted); });
            const bool packaged = !program.package.isEmpty() && std::any_of(entry.packages.begin(), entry.packages.end(), [&program](const QString &prefix) {
                return program.package.startsWith(prefix + QLatin1Char('_'), Qt::CaseInsensitive);
            });
            if (!routed && !generic && !named && !packaged) continue;
            found[entry.id] = found.value(entry.id) || (routed && program.running);
        }
    }
    return found;
}

QHash<QString, QString> IconPaths(const QList<CatalogEntry> &catalog, const QList<Program> &programs) {
    QHash<QString, QString> paths;
    for (const auto &entry: catalog) {
        QString named;
        for (const auto &program: programs) {
            if (program.path.isEmpty() || !program.path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) continue;
            if (entry.processes.contains(program.executable, Qt::CaseInsensitive)) {
                paths.insert(entry.id, program.path);
                named.clear();
                break;
            }
            if (named.isEmpty() && std::any_of(entry.detect.begin(), entry.detect.end(), [&program](const QString &wanted) {
                    return program.name.compare(wanted, Qt::CaseInsensitive) == 0 || program.name.startsWith(wanted + QLatin1Char(' '), Qt::CaseInsensitive);
                }))
                named = program.path;
        }
        if (!named.isEmpty()) paths.insert(entry.id, named);
    }
    return paths;
}

bool IsOwned(const RouteRule &rule) {
    return rule.type == custom && rule.name.startsWith(QLatin1String(RulePrefix));
}

QList<Route> Read(const QList<std::shared_ptr<RouteRule>> &rules) {
    QList<Route> routes;
    for (const auto &rule: rules) {
        if (rule == nullptr || !IsOwned(*rule)) continue;
        QString id = rule->name.mid(QLatin1String(RulePrefix).size());
        const bool processRule = id.endsWith(QLatin1String(ProcessSuffix));
        if (processRule) id.chop(QLatin1String(ProcessSuffix).size());
        auto it = std::find_if(routes.begin(), routes.end(), [&id](const Route &route) { return route.id == id; });
        if (it == routes.end()) {
            routes.append({.id = id, .outbound = rule->outboundID});
            it = std::prev(routes.end());
        }
        if (processRule) {
            it->processes += rule->process_name;
        } else {
            it->ruleSets += rule->rule_set;
            it->domains += rule->domain_suffix;
            it->cidrs += rule->ip_cidr;
        }
    }
    return routes;
}

void Write(QList<std::shared_ptr<RouteRule>> &rules, const QList<Route> &routes) {
    int insertAt = -1;
    for (int i = 0; i < rules.size();) {
        if (rules.at(i) != nullptr && IsOwned(*rules.at(i))) {
            if (insertAt < 0) insertAt = i;
            rules.removeAt(i);
        } else {
            ++i;
        }
    }
    // A first entry goes on top: a user who names an app expects it to win over broader rules.
    if (insertAt < 0) insertAt = 0;
    QList<std::shared_ptr<RouteRule>> owned;
    for (const Route &route: routes) {
        // sing-box ANDs process_name with the address fields, so an app needs two rules to match either.
        // A rule-set's matchers merge into the address group, so it ORs with the rule's own domains.
        if (!route.ruleSets.isEmpty() || !route.domains.isEmpty() || !route.cidrs.isEmpty()) {
            auto rule = makeRule(QLatin1String(RulePrefix) + route.id, route.outbound);
            rule->rule_set = route.ruleSets;
            rule->domain_suffix = route.domains;
            rule->ip_cidr = route.cidrs;
            owned.append(rule);
        }
        if (!route.processes.isEmpty()) {
            auto rule = makeRule(QLatin1String(RulePrefix) + route.id + QLatin1String(ProcessSuffix), route.outbound);
            rule->process_name = route.processes;
            owned.append(rule);
        }
    }
    for (int i = 0; i < owned.size(); ++i) rules.insert(insertAt + i, owned.at(i));
}

int ForeignRuleCount(const QList<std::shared_ptr<RouteRule>> &rules) {
    int count = 0;
    for (const auto &rule: rules) {
        if (rule == nullptr || IsOwned(*rule) || rule->type == endpointPreferredBy) continue;
        if (rule->name == QLatin1String(LocalProxyRuleName)) continue;
        ++count;
    }
    return count;
}

std::pair<Kind, QString> Classify(const QString &input) {
    QString text = input.trimmed();
    if (text.contains(QStringLiteral("://"))) text = QUrl(text).host();
    const auto [kind, value] = SplitRuleLine(NormalizeRuleLine(text));
    if (value.isEmpty()) return {Kind::Invalid, {}};
    if (kind == QStringLiteral("processName")) return {Kind::Process, value};
    if (kind == QStringLiteral("processPath")) return {Kind::Process, value.section(QRegularExpression(QStringLiteral("[\\\\/]")), -1)};
    if (kind == QStringLiteral("domain") || kind == QStringLiteral("suffix")) {
        QString domain = value.toLower();
        if (domain.startsWith(QStringLiteral("*."))) domain = domain.mid(2);
        if (domain.startsWith(QStringLiteral("www."))) domain = domain.mid(4);
        return {Kind::Domain, domain};
    }
    if (kind == QStringLiteral("ip")) {
        if (value.contains(QLatin1Char('/'))) return {Kind::Cidr, value};
        const QHostAddress address(value);
        return {Kind::Cidr, value + (address.protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("/128") : QStringLiteral("/32"))};
    }
    return {Kind::Invalid, {}};
}

} // namespace Configs::AppRoutes
