#include "include/configs/RuleSetCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <srslist.h>

#include "include/configs/generate.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"

namespace Configs::RuleSetCache {
namespace {
QString directory() { return GetBasePath() + QStringLiteral("/rulesets"); }

QString filePath(const QString &name) { return directory() + QLatin1Char('/') + name + QStringLiteral(".srs"); }
} // namespace

QString SourceUrl(const QString &name) {
    const QByteArray key = name.toUtf8();
    const std::string_view wanted(key.constData(), key.size());
    const auto it = std::lower_bound(ruleSetList.begin(), ruleSetList.end(), wanted,
                                     [](const auto &entry, std::string_view k) { return entry.first < k; });
    if (it == ruleSetList.end() || it->first != wanted) return {};
    return QString::fromUtf8(it->second.data(), qsizetype(it->second.size()));
}

QString LocalPath(const QString &name) {
    // Only catalog names reach the disk, so a name can never climb out of the folder.
    if (SourceUrl(name).isEmpty()) return {};
    const QString path = filePath(name);
    return QFileInfo(path).size() > 0 ? path : QString();
}

QStringList Missing(const QStringList &names) {
    QStringList missing;
    for (const QString &name: names)
        if (LocalPath(name).isEmpty() && !SourceUrl(name).isEmpty() && !missing.contains(name)) missing.append(name);
    return missing;
}

QStringList Stale(const QStringList &names, int maxAgeDays) {
    QStringList stale;
    const QDateTime limit = QDateTime::currentDateTime().addDays(-maxAgeDays);
    for (const QString &name: names)
        if (const QString path = LocalPath(name); !path.isEmpty() && QFileInfo(path).lastModified() < limit && !stale.contains(name))
            stale.append(name);
    return stale;
}

QString Download(const QString &name, bool useProxy) {
    const QString url = SourceUrl(name);
    if (url.isEmpty()) return QObject::tr("Unknown rule-set: %1").arg(name);
    QDir().mkpath(directory());
    const QString relative = QStringLiteral("rulesets/") + name + QStringLiteral(".srs.download");
    if (QString error = NetworkRequestHelper::DownloadAsset(get_jsdelivr_link(url), relative, useProxy); !error.isEmpty()) return error;
    const QString downloaded = GetBasePath() + QLatin1Char('/') + relative;
    QFile file(downloaded);
    // A captive portal or a mirror error page must not replace a working rule-set: sing-box would refuse to start on it.
    const bool valid = file.open(QIODevice::ReadOnly) && file.read(3) == QByteArrayLiteral("SRS");
    file.close();
    if (!valid) {
        QFile::remove(downloaded);
        return QObject::tr("The download of %1 is not a rule-set.").arg(name);
    }
    QFile::remove(filePath(name));
    if (!QFile::rename(downloaded, filePath(name))) return QObject::tr("Could not write file.");
    return {};
}

} // namespace Configs::RuleSetCache
