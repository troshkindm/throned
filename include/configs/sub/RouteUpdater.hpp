#pragma once

#include <QString>
#include <memory>

#include "include/database/entities/RouteProfile.h"

namespace RouteUpdate {
// Blocks on the network (worker thread only); returns an error string, empty on success.
QString UpdateProfile(const std::shared_ptr<Configs::RouteProfile>& profile, QString* warnings = nullptr);
} // namespace RouteUpdate

// Runs the whole batch on a worker thread and saves each result; the active route is not reloaded.
void UI_update_all_remote_routes(bool onlyAllowed = false);
