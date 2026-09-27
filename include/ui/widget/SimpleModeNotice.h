#pragma once

#include <QObject>
#include <functional>

class UpdateStatusWidget;
namespace Configs {
class SettingsRepo;
}

// Suggests Simple mode once to whoever uses the full interface; trying it retires the tip.
class SimpleModeNotice final : public QObject {
public:
    SimpleModeNotice(UpdateStatusWidget *status, Configs::SettingsRepo &settings, std::function<void()> openSimpleMode);

    // Posts or retires the tip; call again whenever the window changes mode.
    void refresh();

private:
    void remember();
    UpdateStatusWidget *status_;
    Configs::SettingsRepo &settings_;
};
