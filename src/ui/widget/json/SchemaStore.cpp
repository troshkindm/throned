#include "include/ui/widget/json/SchemaStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include "include/global/Logger.hpp"

namespace JsonEdit {
const QJsonObject& SingBoxSchema() {
    static const QJsonObject schema = [] {
        QFile file(QStringLiteral(":/schema/sing-box.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            LOG_WARN("json schema: res/schema/sing-box.json is missing from the build");
            return QJsonObject{};
        }
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            LOG_WARN("json schema: " + error.errorString());
            return QJsonObject{};
        }
        return document.object();
    }();
    return schema;
}

std::shared_ptr<SchemaValidator> SingBoxValidator(const QString& rootRef) {
    const QJsonObject& schema = SingBoxSchema();
    if (schema.isEmpty()) return nullptr;
    QString error;
    auto validator = SchemaValidator::Create(schema, rootRef, &error);
    if (validator == nullptr) LOG_WARN("json schema: " + error);
    return validator;
}

std::shared_ptr<SchemaValidator> SingBoxValidator(const QStringList& rootRefs) {
    const QJsonObject& schema = SingBoxSchema();
    if (schema.isEmpty()) return nullptr;
    QJsonArray branches;
    for (const auto& ref: rootRefs) branches.append(QJsonObject{{QStringLiteral("$ref"), ref}});
    return SchemaValidator::CreateForNode(schema, QJsonObject{{QStringLiteral("oneOf"), branches}});
}
} // namespace JsonEdit
