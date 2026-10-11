#include "include/database/entities/EndpointSource.h"

namespace Configs {
QJsonObject EndpointSource::ToJson() const {
    switch (mode) {
        case Mode::Inherit:
            return {};
        case Mode::Own:
            return {{"mode", "own"}};
        case Mode::Address:
            return {{"mode", "address"}, {"address", address}};
        case Mode::IpList:
            return {{"mode", "list"}, {"list", ipListId}};
    }
    return {};
}

EndpointSource EndpointSource::FromJson(const QJsonObject &json) {
    EndpointSource source;
    const QString mode = json.value("mode").toString();
    if (mode == QLatin1String("own")) {
        source.mode = Mode::Own;
    } else if (mode == QLatin1String("address")) {
        source.address = json.value("address").toString().trimmed();
        if (!source.address.isEmpty()) source.mode = Mode::Address;
    } else if (mode == QLatin1String("list")) {
        source.ipListId = json.value("list").toInt(-1);
        if (source.ipListId >= 0) source.mode = Mode::IpList;
    }
    return source;
}
} // namespace Configs
