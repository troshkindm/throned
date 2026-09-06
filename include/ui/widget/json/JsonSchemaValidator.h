#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <memory>

#include "include/ui/widget/json/JsonValidator.h"

namespace JsonEdit {
// oneOf is resolved through the discriminating constant (type/action), so errors name the variant.
class SchemaValidator final : public Validator {
public:
    // rootRef is a JSON pointer into the schema document ("#/$defs/RouteOptions"); empty is the document root.
    static std::shared_ptr<SchemaValidator> Create(const QJsonObject& schema,
                                                   const QString& rootRef = {},
                                                   QString* error = nullptr);
    static std::shared_ptr<SchemaValidator> CreateForNode(const QJsonObject& document, const QJsonObject& root);

    void AllowExtraType(const QString& propertyName, ValueType type);

    QList<Issue> Validate(const Value& root) const override;

private:
    SchemaValidator(QJsonObject document, QJsonObject root);

    [[nodiscard]] bool relaxed(const QString& key, ValueType type) const;
    [[nodiscard]] QJsonObject deref(const QJsonObject& node, int depth = 0) const;
    [[nodiscard]] bool lookup(const QString& pointer, QJsonObject* out) const;

    void validate(const QJsonObject& schema, const Value& value, const QString& pointer, int depth,
                  QList<Issue>& issues, QSet<QString>* evaluated) const;
    void validateObject(const QJsonObject& schema, const Value& value, const QString& pointer, int depth,
                        QList<Issue>& issues, QSet<QString>& evaluated) const;
    void validateUnion(const QJsonArray& branches, bool exclusive, const Value& value, const QString& pointer,
                       int depth, QList<Issue>& issues, QSet<QString>& evaluated) const;

    // a branch is read through its allOf/$ref composition, one property at a time
    [[nodiscard]] bool branchProperty(const QJsonObject& branch, const QString& key, QJsonObject* out,
                                      int level = 0) const;
    [[nodiscard]] bool branchRequires(const QJsonObject& branch, const QString& key, int level = 0) const;
    [[nodiscard]] QStringList branchKeys(const QJsonObject& branch, int level = 0) const;

    QJsonObject m_document;
    QJsonObject m_root;
    QHash<QString, QSet<int>> m_relaxations;
};
} // namespace JsonEdit
