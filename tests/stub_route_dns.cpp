#include "include/database/entities/RouteProfile.h"

// xrayStreamSetting.cpp asks for the active route's DNS; the outbound tests run without a routing repository.
namespace Configs {
RouteDns RouteDns::Current(bool *own) {
    if (own != nullptr) *own = false;
    return {};
}
} // namespace Configs
