#include "include/configs/common/DialFields.h"

#include <QUrlQuery>

namespace Configs {
bool DialFields::ParseFromLink(const QString& link) {
    auto url = QUrl(link);
    if (!url.isValid()) return false;
    auto query = QUrlQuery(url.query());

    if (query.hasQueryItem("reuse_addr")) reuse_addr = query.queryItemValue("reuse_addr") == "true";
    if (query.hasQueryItem("connect_timeout")) connect_timeout = query.queryItemValue("connect_timeout");
    if (query.hasQueryItem("tcp_fast_open")) tcp_fast_open = query.queryItemValue("tcp_fast_open") == "true";
    if (query.hasQueryItem("tcp_multi_path")) tcp_multi_path = query.queryItemValue("tcp_multi_path") == "true";
    if (query.hasQueryItem("udp_fragment")) udp_fragment = query.queryItemValue("udp_fragment") == "true";
    if (query.hasQueryItem("bind_interface")) bind_interface = query.queryItemValue("bind_interface");
    if (query.hasQueryItem("inet4_bind_address")) inet4_bind_address = query.queryItemValue("inet4_bind_address");
    if (query.hasQueryItem("inet6_bind_address")) inet6_bind_address = query.queryItemValue("inet6_bind_address");
    return true;
}
bool DialFields::ParseFromJson(const QJsonObject& object) {
    if (object.isEmpty()) return false;

    if (object.contains("reuse_addr")) reuse_addr = object["reuse_addr"].toBool();
    if (object.contains("connect_timeout")) connect_timeout = object["connect_timeout"].toString();
    if (object.contains("tcp_fast_open")) tcp_fast_open = object["tcp_fast_open"].toBool();
    if (object.contains("tcp_multi_path")) tcp_multi_path = object["tcp_multi_path"].toBool();
    if (object.contains("udp_fragment")) udp_fragment = object["udp_fragment"].toBool();
    if (object.contains("bind_interface")) bind_interface = object["bind_interface"].toString();
    if (object.contains("inet4_bind_address")) inet4_bind_address = object["inet4_bind_address"].toString();
    if (object.contains("inet6_bind_address")) inet6_bind_address = object["inet6_bind_address"].toString();
    return true;
}
QString DialFields::ExportToLink() {
    QUrlQuery query;
    if (reuse_addr) query.addQueryItem("reuse_addr", "true");
    if (!connect_timeout.isEmpty()) query.addQueryItem("connect_timeout", connect_timeout);
    if (tcp_fast_open) query.addQueryItem("tcp_fast_open", "true");
    if (tcp_multi_path) query.addQueryItem("tcp_multi_path", "true");
    if (udp_fragment) query.addQueryItem("udp_fragment", "true");
    if (!bind_interface.isEmpty()) query.addQueryItem("bind_interface", bind_interface);
    if (!inet4_bind_address.isEmpty()) query.addQueryItem("inet4_bind_address", inet4_bind_address);
    if (!inet6_bind_address.isEmpty()) query.addQueryItem("inet6_bind_address", inet6_bind_address);
    return query.toString();
}
QJsonObject DialFields::ExportToJson() {
    QJsonObject object;
    if (reuse_addr) object["reuse_addr"] = reuse_addr;
    if (!connect_timeout.isEmpty()) object["connect_timeout"] = connect_timeout;
    if (tcp_fast_open) object["tcp_fast_open"] = tcp_fast_open;
    if (tcp_multi_path) object["tcp_multi_path"] = tcp_multi_path;
    if (udp_fragment) object["udp_fragment"] = udp_fragment;
    if (!bind_interface.isEmpty()) object["bind_interface"] = bind_interface;
    if (!inet4_bind_address.isEmpty()) object["inet4_bind_address"] = inet4_bind_address;
    if (!inet6_bind_address.isEmpty()) object["inet6_bind_address"] = inet6_bind_address;
    return object;
}
BuildResult DialFields::Build() {
    return {ExportToJson(), ""};
}
} // namespace Configs
