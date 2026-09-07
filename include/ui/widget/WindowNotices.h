#pragma once

class UpdateStatusWidget;
namespace Configs {
class SettingsRepo;
}

void InstallWindowNotices(UpdateStatusWidget *status, Configs::SettingsRepo &settings);
