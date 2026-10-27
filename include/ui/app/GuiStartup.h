#pragma once

#include <QString>
#include <QStringList>

class QApplication;
class QLocalSocket;

namespace Engine {
class InstanceServer;
}

// The window-only half of startup, called from main() in order.
namespace GuiStartup {
// No-op off macOS.
void InstallMacOpenEventFilter(QApplication &app);

void LoadEmojiFont();

// A JSON object is a control command and is answered on the socket; anything else is a deeplink or file:// url.
void HandleLaunchLine(QLocalSocket *socket, const QString &line);

// Raises the window once the peer hangs up, unless it turned out to be a control client.
void HandleLaunchConnected(QLocalSocket *socket);

void SetupIconTheme();

#ifdef Q_OS_WIN
// True when an elevated copy was started and this process must return 0.
bool RelaunchElevatedIfNeeded(Engine::InstanceServer &server, const QStringList &arguments, const QString &launchDeeplink,
                              const QStringList &launchFiles);
#endif

// Installs the catalogue for locale, Qt's own for the standard buttons, and en_US for English plurals.
void LoadTranslation(const QString &locale);

void LoadTranslator();

// Skins first, so -theme and the stored theme can name one; then -theme and -lang override the settings for this run.
void ApplyAppearanceArguments(const QStringList &arguments);

// The readable production font, or the one a pinned preview asks for.
void SetupApplicationFont(QApplication &app);

void InstallSessionEndHooks(QApplication &app, void (*onSessionEnd)(int));

// After the engine init: a headless Throne running on this host (another data dir) gets a log line and a passive notice.
void WarnAboutHeadless();
} // namespace GuiStartup
