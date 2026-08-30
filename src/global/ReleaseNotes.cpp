#include "include/global/ReleaseNotes.hpp"

#include <QMap>
#include <QRegularExpression>
#include <QStringList>

namespace {

QString NormalizeLanguageTag(QString tag) {
    tag = tag.trimmed().toLower();
    tag = tag.section(QLatin1Char('.'), 0, 0);
    tag = tag.section(QLatin1Char('@'), 0, 0);
    tag.replace(QLatin1Char('_'), QLatin1Char('-'));
    while (tag.contains(QStringLiteral("--"))) tag.replace(QStringLiteral("--"), QStringLiteral("-"));
    return tag;
}

QString TrimBlankLines(QStringList lines) {
    while (!lines.isEmpty() && lines.front().trimmed().isEmpty()) lines.removeFirst();
    while (!lines.isEmpty() && lines.back().trimmed().isEmpty()) lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

} // namespace

namespace ReleaseNotes {

QString LocalizedMarkdown(const QString &releaseBody, const QString &localeName) {
    // HTML comments stay invisible on GitHub, unlike visible language headings,
    // and let a translated section live inside a collapsed <details> block.
    static const QRegularExpression marker(
        QStringLiteral(R"(^\s*<!--\s*throned:lang=([a-z0-9_-]+)\s*-->\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    QMap<QString, QStringList> sections;
    QStringList languageOrder;
    QString activeLanguage;
    QStringList activeLines;
    bool sawMarker = false;
    bool malformed = false;

    const QStringList lines = releaseBody.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = marker.match(line);
        if (!match.hasMatch()) {
            if (!activeLanguage.isEmpty()) activeLines.append(line);
            continue;
        }

        sawMarker = true;
        const QString tag = NormalizeLanguageTag(match.captured(1));
        if (tag == QStringLiteral("end")) {
            if (activeLanguage.isEmpty()) {
                malformed = true;
                break;
            }
            const QString section = TrimBlankLines(activeLines);
            if (!section.isEmpty()) {
                if (!sections.contains(activeLanguage)) languageOrder.append(activeLanguage);
                sections[activeLanguage].append(section);
            }
            activeLanguage.clear();
            activeLines.clear();
            continue;
        }

        if (!activeLanguage.isEmpty() || tag.isEmpty()) {
            malformed = true;
            break;
        }
        activeLanguage = tag;
        activeLines.clear();
    }

    if (!activeLanguage.isEmpty()) malformed = true;
    if (!sawMarker || malformed || sections.isEmpty()) return releaseBody;

    QStringList preferences;
    QString language = NormalizeLanguageTag(localeName);
    while (!language.isEmpty()) {
        if (!preferences.contains(language)) preferences.append(language);
        const int separator = language.lastIndexOf(QLatin1Char('-'));
        if (separator < 0) break;
        language.truncate(separator);
    }
    if (!preferences.contains(QStringLiteral("en"))) preferences.append(QStringLiteral("en"));

    for (const QString &preferred : preferences) {
        if (sections.contains(preferred)) return sections.value(preferred).join(QStringLiteral("\n\n"));
    }
    return sections.value(languageOrder.front()).join(QStringLiteral("\n\n"));
}

} // namespace ReleaseNotes
