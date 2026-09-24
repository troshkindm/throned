#pragma once

#include <QObject>
#include <functional>

class UpdateStatusWidget;
namespace Configs {
class MarkersRepo;
class SettingsRepo;
} // namespace Configs

// Upstream retires Hijack; whoever still has it on is told once, in the shared footer.
class HijackDeprecationNotice final : public QObject {
public:
    HijackDeprecationNotice(UpdateStatusWidget* status, Configs::SettingsRepo& settings, Configs::MarkersRepo& markers,
                            std::function<void()> openSettings);

    // Posts or retires the notice; call again whenever the Hijack settings change.
    void refresh();

private:
    UpdateStatusWidget* status_;
    Configs::SettingsRepo& settings_;
    Configs::MarkersRepo& markers_;
};
