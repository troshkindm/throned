#pragma once

#include <QString>

namespace ReleaseNotes {

// Selects the Markdown block matching localeName from a GitHub release body.
// Bodies without Throned language markers are returned unchanged.
QString LocalizedMarkdown(const QString &releaseBody, const QString &localeName);

} // namespace ReleaseNotes
