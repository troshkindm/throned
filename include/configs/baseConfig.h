#pragma once

#include <QJsonObject>
#include "include/global/Configs.hpp"
#include "include/configs/sub/clash.hpp"

namespace Configs {
struct BuildResult {
    QJsonObject object;
    QString error;
};

class baseConfig {
public:
    virtual ~baseConfig() = default;

    virtual bool ParseFromLink(const QString& link) {
        return false;
    }

    virtual bool ParseFromJson(const QJsonObject& object) {
        return false;
    }

    virtual bool ParseFromClash(const clash::Proxies& object) {
        return false;
    }

    virtual QString ExportToLink() {
        return {};
    }

    virtual QJsonObject ExportToJson() {
        return {};
    }

    virtual QJsonObject ExportIdentity() {
        return ExportToJson();
    }

    virtual BuildResult Build() {
        return {{}, "base class function called!"};
    }
};
} // namespace Configs
