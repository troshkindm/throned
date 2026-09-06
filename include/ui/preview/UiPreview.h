#pragma once

class QApplication;
class QString;

namespace UiPreview {

void ApplyTheme(const QApplication &app);
int RunRouteEditor(QApplication &app);
void RunMainWindow(const QString &outputPrefix);

// Snapshot comparison is only meaningful when every run rasterises the same
// glyphs, so a pinned run passes the family instead of taking what the host has.
QString RequestedFont(const QApplication &app);

} // namespace UiPreview
