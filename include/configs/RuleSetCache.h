#pragma once

#include <QString>
#include <QStringList>

// Rule-sets downloaded by the app itself, so a profile that names them starts
// without waiting for the core to fetch them, and a failed download shows up
// where the user asked for it instead of as a connection that will not start.
namespace Configs::RuleSetCache {

// The catalog URL for a rule-set name from srslist.h; empty when the name is unknown.
QString SourceUrl(const QString &name);
// The downloaded file, or empty when there is none yet.
QString LocalPath(const QString &name);
QStringList Missing(const QStringList &names);
// Present but older than the given age, so worth refreshing in the background.
QStringList Stale(const QStringList &names, int maxAgeDays = 7);
// Blocks on the network; call it off the UI thread. Returns an error, or empty on success.
QString Download(const QString &name, bool useProxy);

} // namespace Configs::RuleSetCache
