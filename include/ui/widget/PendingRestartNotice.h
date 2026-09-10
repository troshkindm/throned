#pragma once

#include <QObject>
#include <QStringList>
#include <functional>

class UpdateStatusWidget;

class PendingRestartNotice final : public QObject {
public:
    PendingRestartNotice(UpdateStatusWidget* status, std::function<void()> restart);

    void noteChange(const QString& reason);
    void clear();

private:
    UpdateStatusWidget* status_;
    QStringList reasons_;
};
