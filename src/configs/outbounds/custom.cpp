#include "include/configs/outbounds/custom.h"

#include "include/configs/common/OutboundFactory.h"
#include "include/configs/common/utils.h"
#include "include/global/Utils.hpp"

#include <QJsonArray>
#include <memory>

namespace Configs {
namespace {
bool isSingBoxInfra(const QString& t) {
    return t == "direct" || t == "block" || t == "dns";
}

SecurityInfo analyzeSingBoxOutbound(const QJsonObject& o) {
    const auto type = o["type"].toString();
    if (type.isEmpty() || type == "custom" || type == "selector" || type == "urltest" || isSingBoxInfra(type))
        return {};
    std::shared_ptr<outbound> ob(NewOutboundByType(type));
    if (!ob || ob->invalid) return {};
    ob->ParseFromJson(o);
    return ob->GetSecurity();
}

QJsonObject singBoxOutboundIdentity(const QJsonObject& o) {
    const auto type = o["type"].toString();
    if (type.isEmpty() || type == "custom" || type == "selector" || type == "urltest" || isSingBoxInfra(type))
        return {};
    std::shared_ptr<outbound> ob(NewOutboundByType(type));
    if (!ob || ob->invalid) return {};
    ob->ParseFromJson(o);
    return ob->ExportIdentity();
}

QJsonObject resolveSingBoxEgress(const QJsonArray& outbounds, const QString& tag, int depth) {
    if (depth <= 0 || outbounds.isEmpty()) return {};
    // The named tag, or the first outbound (sing-box's default egress).
    QJsonObject target;
    if (tag.isEmpty()) {
        target = outbounds.first().toObject();
    } else {
        for (const auto& v: outbounds) {
            if (auto obj = v.toObject(); obj["tag"].toString() == tag) {
                target = obj;
                break;
            }
        }
    }
    if (target.isEmpty()) return {};
    const auto type = target["type"].toString();
    if (type == "selector" || type == "urltest") {
        auto subs = target["outbounds"].toArray();
        QString next = target["default"].toString();
        if (next.isEmpty() && !subs.isEmpty()) next = subs.first().toString();
        return resolveSingBoxEgress(outbounds, next, depth - 1);
    }
    if (type.isEmpty() || isSingBoxInfra(type)) return {};
    return target;
}

bool isXrayInfra(const QString& p) {
    return p == "freedom" || p == "blackhole" || p == "dns" || p == "loopback";
}

SecurityInfo analyzeXrayOutbound(const QJsonObject& o) {
    const auto protocol = o["protocol"].toString();
    if (protocol.isEmpty() || isXrayInfra(protocol)) return {};

    const auto stream = o["streamSettings"].toObject();
    SecurityInfo info;
    info.transport = DisplayTransportName(stream["network"].toString());

    const auto security = stream["security"].toString();
    if (security == "reality") {
        info.label = QObject::tr("Reality");
        info.level = SecurityLevel::Secure;
        return info;
    }
    if (security == "tls") {
        const bool insecure = stream["tlsSettings"].toObject()["allowInsecure"].toBool();
        info.label = insecure ? QObject::tr("Insecure TLS") : QObject::tr("TLS");
        info.level = insecure ? SecurityLevel::Weak : SecurityLevel::Secure;
        return info;
    }
    // No transport security: shadowsocks still encrypts its payload; vmess is trivially detectable.
    if (protocol == "shadowsocks") {
        info.label = QObject::tr("Encrypted");
        info.level = SecurityLevel::Secure;
        return info;
    }
    if (protocol == "vmess") {
        info.label = QObject::tr("Encrypted");
        info.level = SecurityLevel::Weak;
        return info;
    }
    info.label = QObject::tr("Raw");
    info.level = SecurityLevel::None;
    return info;
}

QJsonObject xrayOutboundIdentity(const QJsonObject& o) {
    const auto protocol = o["protocol"].toString();
    if (protocol.isEmpty() || isXrayInfra(protocol)) return {};
    QJsonObject id;
    id["protocol"] = protocol;
    const auto settings = o["settings"].toObject();
    QJsonObject peer;
    if (settings.contains("vnext"))
        peer = settings["vnext"].toArray().first().toObject();
    else if (settings.contains("servers"))
        peer = settings["servers"].toArray().first().toObject();
    if (!peer.isEmpty()) {
        id["server"] = peer["address"];
        id["server_port"] = peer["port"];
    }
    const auto stream = o["streamSettings"].toObject();
    QJsonObject sid;
    if (const auto network = stream["network"].toString(); !network.isEmpty()) sid["network"] = network;
    const auto security = stream["security"].toString();
    if (!security.isEmpty()) sid["security"] = security;
    if (security == "reality") {
        const auto r = stream["realitySettings"].toObject();
        if (r.contains("serverName")) sid["sni"] = r["serverName"];
        if (r.contains("fingerprint")) sid["fingerprint"] = r["fingerprint"];
    } else if (security == "tls") {
        const auto t = stream["tlsSettings"].toObject();
        if (t.contains("serverName")) sid["sni"] = t["serverName"];
        if (t.contains("fingerprint")) sid["fingerprint"] = t["fingerprint"];
    }
    if (!sid.isEmpty()) id["stream"] = sid;
    return id;
}

// Xray routes to the first outbound by default; pick the first real proxy.
QJsonObject firstXrayEgress(const QJsonArray& outbounds) {
    for (const auto& v: outbounds) {
        if (auto obj = v.toObject(); !isXrayInfra(obj["protocol"].toString())) return obj;
    }
    return {};
}
} // namespace

SecurityInfo Custom::GetSecurity() {
    if (type == CustomOutbound) {
        return analyzeSingBoxOutbound(QString2QJsonObject(config));
    }
    if (type == CustomFullConfig) {
        const auto obj = QString2QJsonObject(config);
        const auto egress = resolveSingBoxEgress(obj["outbounds"].toArray(),
                                                 obj["route"].toObject()["final"].toString(), 5);
        return egress.isEmpty() ? SecurityInfo{} : analyzeSingBoxOutbound(egress);
    }
    if (type == CustomXrayOutbound) {
        return analyzeXrayOutbound(QString2QJsonObject(config));
    }
    if (type == CustomXrayFullConfig) {
        const auto obj = QString2QJsonObject(config);
        const auto egress = firstXrayEgress(obj["outbounds"].toArray());
        return egress.isEmpty() ? SecurityInfo{} : analyzeXrayOutbound(egress);
    }
    return {};
}

QJsonObject Custom::ExportIdentity() {
    QJsonObject ob;
    if (type == CustomOutbound) {
        ob = singBoxOutboundIdentity(QString2QJsonObject(config));
    } else if (type == CustomFullConfig) {
        const auto obj = QString2QJsonObject(config);
        const auto egress = resolveSingBoxEgress(obj["outbounds"].toArray(),
                                                 obj["route"].toObject()["final"].toString(), 5);
        if (!egress.isEmpty()) ob = singBoxOutboundIdentity(egress);
    } else if (type == CustomXrayOutbound) {
        ob = xrayOutboundIdentity(QString2QJsonObject(config));
    } else if (type == CustomXrayFullConfig) {
        const auto obj = QString2QJsonObject(config);
        const auto egress = firstXrayEgress(obj["outbounds"].toArray());
        if (!egress.isEmpty()) ob = xrayOutboundIdentity(egress);
    }
    if (ob.isEmpty()) return ExportToJson();
    return QJsonObject{{"subtype", type}, {"ob", ob}};
}
} // namespace Configs
