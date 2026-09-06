#include "include/configs/outbounds/mieru.h"

#include <QJsonArray>
#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
namespace {
QJsonArray splitServerPorts(const QString& raw) {
    QJsonArray arr;
    for (auto part: raw.split(',', Qt::SkipEmptyParts)) {
        part = part.trimmed();
        if (!part.isEmpty()) arr.append(part);
    }
    return arr;
}
} // namespace

bool mieru::ParseFromLink(const QString& link) {
    // Only mieru's "simple" link is mappable; the "standard" base64-protobuf link is rejected here.
    auto url = QUrl(link);
    if (!url.isValid() || url.host().isEmpty() || url.query().isEmpty()) return false;
    auto query = QUrlQuery(url.query());

    outbound::ParseFromLink(link);
    username = url.userName();
    password = url.password();

    // mieru pairs every port with its own protocol; we model one transport, so take the first.
    const auto protocols = query.allQueryItemValues("protocol");
    if (!protocols.isEmpty()) transport = protocols.first().toUpper();

    // The core only accepts ranges in server_ports, so an extra single port becomes "N-N".
    QStringList ranges;
    bool haveSinglePort = false;
    server_port = 0;
    for (auto port: query.allQueryItemValues("port")) {
        port = port.trimmed();
        if (port.isEmpty()) continue;
        if (port.contains('-')) {
            ranges << port;
        } else if (!haveSinglePort) {
            server_port = port.toInt();
            haveSinglePort = true;
        } else {
            ranges << QStringLiteral("%1-%1").arg(port);
        }
    }
    server_ports = ranges.join(",");

    if (query.hasQueryItem("multiplexing")) multiplexing = query.queryItemValue("multiplexing");
    if (query.hasQueryItem("traffic-pattern")) traffic_pattern = query.queryItemValue("traffic-pattern");

    return true;
}

bool mieru::ParseFromJson(const QJsonObject& object) {
    if (object.isEmpty() || object["type"].toString() != "mieru") return false;
    outbound::ParseFromJson(object);
    if (object.contains("transport")) transport = object["transport"].toString();
    if (object.contains("username")) username = object["username"].toString();
    if (object.contains("password")) password = object["password"].toString();
    if (object.contains("multiplexing")) multiplexing = object["multiplexing"].toString();
    if (object.contains("traffic_pattern")) traffic_pattern = object["traffic_pattern"].toString();
    if (object.contains("server_ports")) {
        QStringList ports;
        for (const auto& v: object["server_ports"].toArray()) ports << v.toString();
        server_ports = ports.join(",");
    }
    return true;
}

bool mieru::ParseFromClash(const clash::Proxies& object) {
    // Clash does not support the mieru protocol.
    return false;
}

QString mieru::ExportToLink() {
    // mieru carries no authority port: every port is a repeated "port" item paired with a "protocol".
    QUrl url;
    QUrlQuery query;
    url.setScheme("mierus");
    url.setUserName(username);
    url.setPassword(password);
    url.setHost(server);
    if (!name.isEmpty()) url.setFragment(name);

    // mieru requires a profile name; we don't model one, so use "default".
    query.addQueryItem("profile", "default");

    const auto protocol = transport.isEmpty() ? QStringLiteral("TCP") : transport.toUpper();
    auto addPort = [&](const QString& port) {
        query.addQueryItem("port", port);
        query.addQueryItem("protocol", protocol);
    };
    if (server_port > 0) addPort(QString::number(server_port));
    for (auto part: server_ports.split(',', Qt::SkipEmptyParts)) {
        part = part.trimmed();
        if (!part.isEmpty()) addPort(part);
    }

    if (!multiplexing.isEmpty()) query.addQueryItem("multiplexing", multiplexing);
    if (!traffic_pattern.isEmpty()) query.addQueryItem("traffic-pattern", traffic_pattern);

    mergeUrlQuery(query, outbound::ExportToLink());

    if (!query.isEmpty()) url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QJsonObject mieru::ExportToJson() {
    QJsonObject object;
    object["type"] = "mieru";
    mergeJsonObjects(object, outbound::ExportToJson());
    object["transport"] = transport.isEmpty() ? QString("TCP") : transport;
    if (!username.isEmpty()) object["username"] = username;
    if (!password.isEmpty()) object["password"] = password;
    if (!multiplexing.isEmpty()) object["multiplexing"] = multiplexing;
    if (!traffic_pattern.isEmpty()) object["traffic_pattern"] = traffic_pattern;
    auto ports = splitServerPorts(server_ports);
    if (!ports.isEmpty()) object["server_ports"] = ports;
    return object;
}

BuildResult mieru::Build() {
    QJsonObject object;
    object["type"] = "mieru";
    mergeJsonObjects(object, outbound::Build().object);
    object["transport"] = transport.isEmpty() ? QString("TCP") : transport;
    if (!username.isEmpty()) object["username"] = username;
    if (!password.isEmpty()) object["password"] = password;
    if (!multiplexing.isEmpty()) object["multiplexing"] = multiplexing;
    if (!traffic_pattern.isEmpty()) object["traffic_pattern"] = traffic_pattern;
    auto ports = splitServerPorts(server_ports);
    if (!ports.isEmpty()) object["server_ports"] = ports;
    return {object, ""};
}

QString mieru::DisplayType() {
    return "Mieru";
}

SecurityInfo mieru::GetSecurity() {
    return {QObject::tr("Encrypted"), {}, SecurityLevel::Secure};
}
} // namespace Configs
