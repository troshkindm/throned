#pragma once

class MainWindow;
class QString;
class QWidget;
class QTabWidget;

namespace UiPreview {

void SavePopupComposite(QWidget *window, QWidget *popup, const QString &path);
void CaptureGraphPreview(MainWindow *window, QTabWidget *statsTabs, const QString &prefix);
void CaptureQuickAdd(MainWindow *window, const QString &outputPrefix, bool fromEmptyState);
void CaptureProtocolEditors(QWidget *parent, const QString &outputPrefix);
void VerifyStatsPanelAndCapture(MainWindow *window, const QString &outputPrefix);
void VerifyNarrowLayout(MainWindow *window, const QString &outputPrefix);
void VerifySimpleMode(MainWindow *window, const QString &outputPrefix);

} // namespace UiPreview
