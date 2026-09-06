#pragma once

class MainWindow;
class QString;
class QWidget;

namespace UiPreview {

void SavePopupComposite(QWidget *window, QWidget *popup, const QString &path);
void CaptureQuickAdd(MainWindow *window, const QString &outputPrefix, bool fromEmptyState);
void VerifyStatsPanelAndCapture(MainWindow *window, const QString &outputPrefix);

} // namespace UiPreview
