#include "include/configs/outbounds/shadowsocks.h"

#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
bool shadowsocks::ParseFromLink(const QString& link) {
    QUrl url;
    if (SubStrBefore(link, "#").contains("@")) {
        url = QUrl(link);
    } else {
        // v2rayN format: base64 encoded full URL
        QString linkN = DecodeB64IfValid(SubStrBefore(SubStrAfter(link, "://"), "#"), QByteArray::Base64Option::Base64UrlEncoding);
        if (linkN.isEmpty()) return false;
        if (link.contains("#")) linkN += "#" + SubStrAfter(link, "#");
        url = QUrl("https://" + linkN);
    }
    if (!url.isValid()) return false;
    auto query = QUrlQuery(url.query());
    outbound::ParseFromLink(url.toString());

    if (url.password().isEmpty()) {
        // Traditional format: method:password base64 encoded in username
        auto method_password = DecodeB64IfValid(url.userName(), QByteArray::Base64Option::Base64UrlEncoding);
        if (method_password.isEmpty()) return false;
        method = SubStrBefore(method_password, ":");
        password = SubStrAfter(method_password, ":");
    } else {
        // 2022 format: method in username, password in password
        method = url.userName();
        password = url.password();
    }

    plugin = query.queryItemValue("plugin", QUrl::FullyDecoded).replace("simple-obfs;", "obfs-local;");
    plugin_opts = SubStrAfter(plugin, ";");
    plugin = SubStrBefore(plugin, ";");
    if (query.hasQueryItem("plugin-opts")) plugin_opts = query.queryItemValue("plugin-opts", QUrl::FullyDecoded);
    if (query.hasQueryItem("uot")) uot = query.queryItemValue("uot") == "true" || query.queryItemValue("uot").toInt() > 0;
    multiplex->ParseFromLink(link);

    return !(server.isEmpty() || method.isEmpty() || password.isEmpty());
}

bool shadowsocks::ParseFromJson(const QJsonObject& object) {
    if (object.isEmpty() || object["type"].toString() != "shadowsocks") return false;
    outbound::ParseFromJson(object);
    if (object.contains("method")) method = object["method"].toString();
    if (object.contains("password")) password = object["password"].toString();
    if (object.contains("plugin")) plugin = object["plugin"].toString();
    if (object.contains("plugin_opts")) plugin_opts = object["plugin_opts"].toString();
    if (object.contains("udp_over_tcp")) {
        if (object["udp_over_tcp"].isBool()) uot = object["udp_over_tcp"].toBool();
        if (object["udp_over_tcp"].isObject()) uot = object["udp_over_tcp"].toObject()["enabled"].toBool();
    }
    if (object.contains("multiplex")) multiplex->ParseFromJson(object["multiplex"].toObject());
    return true;
}

bool shadowsocks::ParseFromClash(const clash::Proxies& object) {
    if (object.type != "ss") return false;
    outbound::ParseFromClash(object);
    method = QString::fromStdString(object.cipher);
    password = QString::fromStdString(object.password);
    uot = object.udp_over_tcp;
    if (!object.plugin.empty()) {
        if (object.plugin == "v2ray-plugin") {
            plugin = "v2ray-plugin";
            QStringList ssPlugin;
            clash::v2rayPlugin plugin_config = object.plugin_opts.get_value<clash::v2rayPlugin>();
            if (plugin_config.tls) ssPlugin << "tls";
            if (!plugin_config.host.empty()) ssPlugin << "host=" + QString::fromStdString(plugin_config.host);
            if (!plugin_config.path.empty()) ssPlugin << "path=" + QString::fromStdString(plugin_config.path);
            if (!plugin_config.mode.empty()) ssPlugin << "mode=" + QString::fromStdString(plugin_config.mode);
            if (plugin_config.mux) ssPlugin << "mux";
            plugin_opts = ssPlugin.join(";");
        } else if (object.plugin == "obfs") {
            plugin = "obfs-local";
            QStringList ssPlugin;
            clash::obfs plugin_config = object.plugin_opts.get_value<clash::obfs>();
            if (!plugin_config.mode.empty()) ssPlugin << "obfs=" + QString::fromStdString(plugin_config.mode);
            if (!plugin_config.host.empty()) ssPlugin << "obfs-host=" + QString::fromStdString(plugin_config.host);
            plugin_opts = ssPlugin.join(";");
        }
    }

    multiplex->ParseFromClash(object);
    return true;
}

bool shadowsocks::ParseFromSIP008(const QJsonObject& object) {
    if (object.isEmpty()) return false;
    outbound::ParseFromJson(object);
    if (object.contains("remarks")) name = object["remarks"].toString();
    if (object.contains("method")) method = object["method"].toString();
    if (object.contains("password")) password = object["password"].toString();
    if (object.contains("plugin")) plugin = object["plugin"].toString().replace("simple-obfs", "obfs-local");
    if (object.contains("plugin_opts")) plugin_opts = object["plugin_opts"].toString();
    if (object.contains("uot")) {
        if (object["uot"].isBool()) uot = object["uot"].toBool();
        if (object["uot"].isObject()) uot = object["uot"].toObject()["enabled"].toBool();
    }
    if (object.contains("multiplex")) multiplex->ParseFromJson(object["multiplex"].toObject());
    return !(server.isEmpty() || method.isEmpty() || password.isEmpty());
}

QString shadowsocks::ExportToLink() {
    QUrl url;
    QUrlQuery query;
    url.setScheme("ss");

    if (method.startsWith("2022-")) {
        url.setUserName(method);
        url.setPassword(password);
    } else {
        auto method_password = method + ":" + password;
        url.setUserName(method_password.toUtf8().toBase64(QByteArray::Base64Option::Base64UrlEncoding));
    }

    url.setHost(server);
    url.setPort(server_port);
    if (!name.isEmpty()) url.setFragment(name);

    if (!plugin.isEmpty()) {
        QString pluginString = plugin;
        if (!plugin_opts.isEmpty()) pluginString.append(";" + plugin_opts);
        query.addQueryItem("plugin", QUrl::toPercentEncoding(pluginString));
    }
    if (uot) query.addQueryItem("uot", "1");

    mergeUrlQuery(query, multiplex->ExportToLink());
    mergeUrlQuery(query, outbound::ExportToLink());

    if (!query.isEmpty()) url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QJsonObject shadowsocks::ExportToJson() {
    QJsonObject object;
    object["type"] = "shadowsocks";
    mergeJsonObjects(object, outbound::ExportToJson());
    if (!method.isEmpty()) object["method"] = method;
    if (!password.isEmpty()) object["password"] = password;
    if (!plugin.isEmpty()) object["plugin"] = plugin;
    if (!plugin_opts.isEmpty()) object["plugin_opts"] = plugin_opts;
    if (uot) object["udp_over_tcp"] = uot;
    if (auto muxObj = multiplex->ExportToJson(); !muxObj.isEmpty()) object["multiplex"] = muxObj;
    return object;
}

BuildResult shadowsocks::Build() {
    if (plugin.contains(";")) {
        plugin_opts = SubStrAfter(plugin, ";");
        plugin = SubStrBefore(plugin, ";");
    }
    QJsonObject object;
    object["type"] = "shadowsocks";
    mergeJsonObjects(object, outbound::Build().object);
    if (!method.isEmpty()) object["method"] = method;
    if (!password.isEmpty()) object["password"] = password;
    if (!plugin.isEmpty()) object["plugin"] = plugin;
    if (!plugin_opts.isEmpty()) object["plugin_opts"] = plugin_opts;
    if (uot) object["udp_over_tcp"] = uot;
    if (auto muxObj = multiplex->Build().object; !muxObj.isEmpty()) object["multiplex"] = muxObj;
    return {object, ""};
}

QString shadowsocks::DisplayType() {
    return "Shadowsocks";
}

SecurityInfo shadowsocks::GetSecurity() {
    SecurityInfo info;
    if (method.isEmpty() || method == "none") {
        info.label = QObject::tr("Raw");
        info.level = SecurityLevel::None;
        return info;
    }
    // Pre-AEAD stream ciphers: no integrity, trivially detectable.
    static const QStringList streamCiphers = {
        "aes-128-ctr",
        "aes-192-ctr",
        "aes-256-ctr",
        "aes-128-cfb",
        "aes-192-cfb",
        "aes-256-cfb",
        "rc4-md5",
        "chacha20-ietf",
        "xchacha20",
    };
    if (streamCiphers.contains(method)) {
        info.label = QObject::tr("Weak Cipher");
        info.level = SecurityLevel::Weak;
        return info;
    }
    info.label = QObject::tr("Encrypted");
    info.level = SecurityLevel::Secure;
    return info;
}
} // namespace Configs
