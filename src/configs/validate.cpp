#include "include/configs/validate.h"

#include <QJsonArray>
#include <QSet>

namespace Configs {
QStringList FindDanglingReferences(const QJsonObject &config) {
    QStringList problems;

    const auto dnsObj = config.value("dns").toObject();
    const auto routeObj = config.value("route").toObject();

    QSet<QString> dnsServers;
    for (const auto &value: dnsObj.value("servers").toArray()) {
        if (const auto tag = value.toObject().value("tag").toString(); !tag.isEmpty()) dnsServers << tag;
    }
    QSet<QString> outbounds;
    for (const auto &section: {QStringLiteral("outbounds"), QStringLiteral("endpoints")}) {
        for (const auto &value: config.value(section).toArray()) {
            if (const auto tag = value.toObject().value("tag").toString(); !tag.isEmpty()) outbounds << tag;
        }
    }

    int index = 0;
    for (const auto &value: dnsObj.value("rules").toArray()) {
        const auto server = value.toObject().value("server").toString();
        if (!server.isEmpty() && !dnsServers.contains(server))
            problems << QString("dns rule[%1] routes to a missing server \"%2\"").arg(index).arg(server);
        index++;
    }
    for (const auto &value: dnsObj.value("servers").toArray()) {
        const auto server = value.toObject();
        const auto tag = server.value("tag").toString();
        if (const auto detour = server.value("detour").toString();
            !detour.isEmpty() && !outbounds.contains(detour))
            problems << QString("dns server \"%1\" detours through a missing outbound \"%2\"").arg(tag, detour);
        if (const auto resolver = server.value("domain_resolver").toString();
            !resolver.isEmpty() && !dnsServers.contains(resolver))
            problems << QString("dns server \"%1\" resolves through a missing server \"%2\"").arg(tag, resolver);
    }

    index = 0;
    for (const auto &value: routeObj.value("rules").toArray()) {
        const auto outbound = value.toObject().value("outbound");
        // An id that never got mapped to a tag lands here as a bare number,
        // which sing-box rejects just as surely as an unknown tag.
        if (outbound.isDouble())
            problems << QString("route rule[%1] points at unmapped outbound id %2").arg(index).arg(outbound.toInt());
        else if (const auto tag = outbound.toString(); !tag.isEmpty() && !outbounds.contains(tag))
            problems << QString("route rule[%1] points at a missing outbound \"%2\"").arg(index).arg(tag);
        index++;
    }
    if (const auto finalOut = routeObj.value("final").toString();
        !finalOut.isEmpty() && !outbounds.contains(finalOut))
        problems << QString("route final points at a missing outbound \"%1\"").arg(finalOut);
    if (const auto resolver = routeObj.value("default_domain_resolver").toObject().value("server").toString();
        !resolver.isEmpty() && !dnsServers.contains(resolver))
        problems << QString("default_domain_resolver names a missing server \"%1\"").arg(resolver);

    return problems;
}
} // namespace Configs
