#pragma once

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QStringList>

#include <functional>

class ApplicationPickerDialog final : public QDialog {
public:
    explicit ApplicationPickerDialog(QWidget *parent = nullptr);
    ~ApplicationPickerDialog() override;

    [[nodiscard]] QStringList selectedRules() const;

private:
    class Private;
    Private *d;
};

// A sing-box process rule carries only an executable name, so its icon can be
// extracted just once the name has been mapped back to a path. The index over
// running processes and installed applications is built off the UI thread and
// kept for the rest of the session.
namespace ApplicationIcons {

// Delivers the icon on the UI thread once it is known. `ready` is not called
// when the executable cannot be located, or after `context` is destroyed.
void resolve(const QString &executableName, QObject *context, std::function<void(const QIcon &)> ready);

} // namespace ApplicationIcons

// Installed and running programs, as the picker lists them. Reads the registry or
// application folders and the process table, so call it off the UI thread.
namespace InstalledApplications {
struct Entry {
    QString name;
    QString executable;
    QString path;
    QString package; // Microsoft Store package full name, for a Store app
    bool running = false;
};
QList<Entry> Scan();
} // namespace InstalledApplications
