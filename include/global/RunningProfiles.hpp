#pragma once

#include <QSet>

namespace Configs {
// Includes the started profile, every chain hop, route outbound, endpoint and auto-selector member.
void SetRunningProfiles(const QSet<int> &profileIDs);

void ClearRunningProfiles();

bool RunningUsesProfile(int profileID);
} // namespace Configs
