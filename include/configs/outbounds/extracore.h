#pragma once
#include "include/configs/common/Outbound.h"

namespace Configs {
class extracore : public outbound {
public:
    QString socksAddress = "127.0.0.1";
    int socksPort;
    QString extraCorePath;
    QString extraCoreArgs;
    QString extraCoreConf;
    bool noLogs = false;

    bool ParseFromJson(const QJsonObject &object) override {
        if (object.isEmpty()) return false;
        if (object.contains("name")) name = object["name"].toString();
        if (object.contains("socks_address")) socksAddress = object["socks_address"].toString();
        if (object.contains("socks_port")) socksPort = object["socks_port"].toInt();
        if (object.contains("extra_core_path")) extraCorePath = object["extra_core_path"].toString();
        if (object.contains("extra_core_args")) extraCoreArgs = object["extra_core_args"].toString();
        if (object.contains("extra_core_conf")) extraCoreConf = object["extra_core_conf"].toString();
        if (object.contains("no_logs")) noLogs = object["no_logs"].toBool();
        return true;
    }

    QJsonObject ExportToJson() override {
        QJsonObject object;
        object["name"] = name;
        object["type"] = "extracore";
        object["socks_address"] = socksAddress;
        object["socks_port"] = socksPort;
        object["extra_core_path"] = extraCorePath;
        object["extra_core_args"] = extraCoreArgs;
        object["extra_core_conf"] = extraCoreConf;
        object["no_logs"] = noLogs;
        return object;
    }

    QString DisplayType() override { return "ExtraCore"; };

    // The config is owned by an external core, so we cannot inspect it.
    SecurityInfo GetSecurity() override { return {}; }

    bool IsExtraCore() override { return true; }

    BuildResult Build() override {
        QJsonObject socksOutbound;
        socksOutbound["type"] = "socks";
        socksOutbound["server"] = socksAddress;
        socksOutbound["server_port"] = socksPort;
        return {socksOutbound, ""};
    }
};
} // namespace Configs
