// Parsing of the free-form rule lines the simple editor and the control API accept.
// Split out of RouteProfile.cpp so it can be tested: everything here is pure text
// handling, while the rest of that file needs the whole data layer to link.
#include "include/database/entities/RouteProfile.h"

#include <QHostAddress>
#include <QMap>

namespace Configs {
const QMap<QString, QString> &ruleLineAliases() {
    static const QMap<QString, QString> aliases{
        {QStringLiteral("domain"), QStringLiteral("domain")},
        {QStringLiteral("full"), QStringLiteral("domain")},
        {QStringLiteral("suffix"), QStringLiteral("suffix")},
        {QStringLiteral("domain_suffix"), QStringLiteral("suffix")},
        {QStringLiteral("keyword"), QStringLiteral("keyword")},
        {QStringLiteral("domain_keyword"), QStringLiteral("keyword")},
        {QStringLiteral("regex"), QStringLiteral("regex")},
        {QStringLiteral("regexp"), QStringLiteral("regex")},
        {QStringLiteral("domain_regex"), QStringLiteral("regex")},
        {QStringLiteral("ruleset"), QStringLiteral("ruleset")},
        {QStringLiteral("rule_set"), QStringLiteral("ruleset")},
        {QStringLiteral("ip"), QStringLiteral("ip")},
        {QStringLiteral("ip_cidr"), QStringLiteral("ip")},
        {QStringLiteral("cidr"), QStringLiteral("ip")},
        {QStringLiteral("processname"), QStringLiteral("processName")},
        {QStringLiteral("process_name"), QStringLiteral("processName")},
        {QStringLiteral("processpath"), QStringLiteral("processPath")},
        {QStringLiteral("process_path"), QStringLiteral("processPath")},
    };
    return aliases;
}

QString guessRuleKind(const QString &value) {
    // Paths first: the only kind allowed to contain spaces.
    if (value.contains(QLatin1Char('\\')) || value.startsWith(QLatin1Char('/')))
        return QStringLiteral("processPath");
    if (value.contains(QLatin1Char(' '))) return {};
    if (value.startsWith(QStringLiteral("geosite-")) || value.startsWith(QStringLiteral("geoip-")))
        return QStringLiteral("ruleset");
    if (value.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) return QStringLiteral("processName");
    const QString address = value.section(QLatin1Char('/'), 0, 0);
    if (!address.isEmpty() && !QHostAddress(address).isNull()) return QStringLiteral("ip");
    if (value.startsWith(QLatin1Char('.'))) return QStringLiteral("suffix");
    if (value.startsWith(QStringLiteral("*."))) return QStringLiteral("domain");
    if (value.contains(QLatin1Char('.'))) return QStringLiteral("domain");
    return {};
}

std::pair<QString, QString> SplitRuleLine(const QString &line) {
    const QString clean = line.trimmed();
    const int separator = clean.indexOf(QLatin1Char(':'));
    if (separator <= 0) return {};
    // Only the first colon separates, so a Windows process path keeps its drive letter.
    return {clean.left(separator).trimmed(), clean.mid(separator + 1).trimmed()};
}

QString NormalizeRuleLine(const QString &line) {
    QString clean = line.trimmed();
    if (clean.isEmpty() || clean.startsWith(QLatin1Char('#')) || clean.startsWith(QStringLiteral("//"))) return {};
    while (clean.startsWith(QLatin1Char('-')) || clean.startsWith(QLatin1Char('"'))) clean = clean.mid(1).trimmed();
    while (clean.endsWith(QLatin1Char(',')) || clean.endsWith(QLatin1Char('"'))) clean.chop(1);
    clean = clean.trimmed();
    if (clean.isEmpty()) return {};

    const int separator = clean.indexOf(QLatin1Char(':'));
    if (separator > 0) {
        const QString kind = ruleLineAliases().value(clean.left(separator).trimmed().toLower());
        const QString value = clean.mid(separator + 1).trimmed();
        if (!kind.isEmpty() && !value.isEmpty()) {
            if (kind == QStringLiteral("suffix") && value.startsWith(QLatin1Char('.'))) {
                const QString bare = value.mid(1);
                // A bare "." would normalise to a matcher with nothing in it, leaving a
                // rule that is all action and no condition -- a catch-all by accident.
                if (bare.isEmpty()) return {};
                return kind + QLatin1Char(':') + bare;
            }
            return kind + QLatin1Char(':') + value;
        }
    }

    const QString guess = guessRuleKind(clean);
    if (guess.isEmpty()) return {};
    if (guess == QStringLiteral("suffix")) {
        const QString bare = clean.mid(1);
        if (bare.isEmpty()) return {};
        return guess + QLatin1Char(':') + bare;
    }
    return guess + QLatin1Char(':') + clean;
}
} // namespace Configs
