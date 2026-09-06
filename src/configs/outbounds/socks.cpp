#include "include/configs/outbounds/socks.h"

#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
bool socks::ParseFromLink(const QString& link) {
    auto url = QUrl(link);
    if (!url.isValid()) return false;
    auto query = QUrlQuery(url.query());

    outbound::ParseFromLink(link);
    if (query.hasQueryItem("version")) {
        version = query.queryItemValue("version").toInt();
    } else {
        if (url.scheme() == "socks4") version = 4;
        if (url.scheme() == "socks5") version = 5;
    }

    if (!url.password().isEmpty() || !url.userName().isEmpty()) {
        username = url.userName();
        password = url.password();

        // v2rayN base64-encodes "user:pass" into the username.
        if (password.isEmpty() && !username.isEmpty()) {
            QString decoded = DecodeB64IfValid(username);
            if (!decoded.isEmpty()) {
                username = SubStrBefore(decoded, ":");
                password = SubStrAfter(decoded, ":");
            }
        }
    }

    if (query.hasQueryItem("uot")) uot = query.queryItemValue("uot") == "true" || query.queryItemValue("uot").toInt() > 0;

    if (server_port == 0) server_port = 1080;

    return !server.isEmpty();
}

bool socks::ParseFromJson(const QJsonObject& object) {
    if (object.isEmpty() || object["type"].toString() != "socks") return false;
    outbound::ParseFromJson(object);
    if (object.contains("username")) username = object["username"].toString();
    if (object.contains("password")) password = object["password"].toString();
    if (object.contains("version")) version = object["version"].toInt();
    if (object.contains("uot")) uot = object["uot"].toBool();
    return true;
}

bool socks::ParseFromClash(const clash::Proxies& object) {
    if (object.type != "socks5") return false;
    outbound::ParseFromClash(object);
    username = QString::fromStdString(object.username);
    password = QString::fromStdString(object.password);

    return true;
}

QString socks::ExportToLink() {
    QUrl url;
    QUrlQuery query;

    QString scheme = "socks5";
    if (version == 4) {
        scheme = "socks4";
    }
    url.setScheme(scheme);

    url.setHost(server);
    url.setPort(server_port);
    if (!name.isEmpty()) url.setFragment(name);

    if (!username.isEmpty()) url.setUserName(username);
    if (!password.isEmpty()) url.setPassword(password);
    if (uot) query.addQueryItem("uot", "1");

    mergeUrlQuery(query, outbound::ExportToLink());
    if (!query.isEmpty()) url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QJsonObject socks::ExportToJson() {
    QJsonObject object;
    object["type"] = "socks";
    mergeJsonObjects(object, outbound::ExportToJson());
    if (!username.isEmpty()) object["username"] = username;
    if (!password.isEmpty()) object["password"] = password;
    if (version == 4) object["version"] = "4";
    if (uot) object["uot"] = uot;
    return object;
}

BuildResult socks::Build() {
    QJsonObject object;
    object["type"] = "socks";
    mergeJsonObjects(object, outbound::Build().object);
    if (!username.isEmpty()) object["username"] = username;
    if (!password.isEmpty()) object["password"] = password;
    if (version == 4) object["version"] = "4";
    if (uot) object["uot"] = uot;
    return {object, ""};
}

QString socks::DisplayType() {
    return "Socks";
}
} // namespace Configs
