#pragma once

#include <QJsonObject>
#include <QString>

namespace Configs {
// Where a profile's server address comes from when its config is built.
struct EndpointSource {
    // Inherit: a profile follows its group; on a group it changes nothing.
    enum class Mode { Inherit,
                      Own,
                      Address,
                      IpList };

    Mode mode = Mode::Inherit;
    // Mode::Address only (groups); a host without a port.
    QString address;
    // Mode::IpList only.
    int ipListId = -1;

    [[nodiscard]] QJsonObject ToJson() const;

    static EndpointSource FromJson(const QJsonObject &json);

    bool operator==(const EndpointSource &other) const = default;
};
} // namespace Configs
