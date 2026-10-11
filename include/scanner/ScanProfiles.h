#pragma once

#include <QJsonObject>
#include <QString>
#include <memory>

#include "include/configs/sub/warp.h"
#include "include/database/entities/IpScan.h"
#include "include/database/entities/Profile.h"

namespace Scanner {
[[nodiscard]] bool IsScanBaseType(const QString &type);

// Always a deep copy, never the live repo object.
std::shared_ptr<Configs::Profile> ResolveScanBase(const Configs::IpScan &scan, QString *error);

std::shared_ptr<Configs::Profile> BuildWarpTemplate(const Configs::ScanWarpOptions &opt, QString *error);

// transport: "wireguard" | "masque".
QJsonObject WarpIdentityToBeanJson(const QString &transport, const Configs_network::WarpIdentity &identity);

[[nodiscard]] QString WarpTransportOf(const QString &mode);
} // namespace Scanner
