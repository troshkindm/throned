#include "include/ui/widget/json/JsonSchemaValidator.h"

#include <QJsonArray>
#include <QObject>
#include <QRegularExpression>
#include <algorithm>
#include <functional>

namespace JsonEdit {
namespace {
constexpr int kMaxSchemaDepth = 64;
constexpr int kMaxIssues = 200;
constexpr int kMaxListed = 10;
constexpr int kMaxUnionIssues = 6;

// a branch the value simply is not (wrong "type"/"action") ranks below one it almost matches
constexpr int kVariantPenalty = 1000;

bool rejectedVariant(const QList<Issue>& issues) {
    for (const auto& issue: issues) {
        if (issue.variantMismatch) return true;
    }
    return false;
}

int errorCount(const QList<Issue>& issues) {
    int count = 0;
    for (const auto& issue: issues) {
        if (issue.severity == Severity::Error) count++;
    }
    return count;
}

bool matchesType(const QString& name, const Value& value) {
    if (name == QLatin1String("object")) return value.type == ValueType::Object;
    if (name == QLatin1String("array")) return value.type == ValueType::Array;
    if (name == QLatin1String("string")) return value.type == ValueType::String;
    if (name == QLatin1String("boolean")) return value.type == ValueType::Bool;
    if (name == QLatin1String("null")) return value.type == ValueType::Null;
    if (name == QLatin1String("number")) return value.type == ValueType::Number;
    if (name == QLatin1String("integer")) return value.type == ValueType::Number && value.integral;
    return true;
}

QStringList schemaTypes(const QJsonValue& type) {
    QStringList names;
    if (type.isString()) {
        names << type.toString();
    } else {
        for (const auto& entry: type.toArray()) names << entry.toString();
    }
    return names;
}

bool equalsJson(const Value& value, const QJsonValue& json) {
    switch (value.type) {
        case ValueType::String:
            return json.isString() && json.toString() == value.stringValue;
        case ValueType::Bool:
            return json.isBool() && json.toBool() == value.boolValue;
        case ValueType::Number:
            return json.isDouble() && qFuzzyCompare(json.toDouble() + 1.0, value.numberValue + 1.0);
        case ValueType::Null:
            return json.isNull();
        default:
            return false;
    }
}

QString scalarText(const QJsonValue& json) {
    if (json.isString()) return json.toString();
    if (json.isBool()) return json.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (json.isDouble()) return QString::number(json.toDouble());
    if (json.isNull()) return QStringLiteral("null");
    return {};
}

QString describeValue(const Value& value) {
    switch (value.type) {
        case ValueType::String:
            return QStringLiteral("\"") + value.stringValue + QStringLiteral("\"");
        case ValueType::Bool:
            return value.boolValue ? QStringLiteral("true") : QStringLiteral("false");
        case ValueType::Number:
            return QString::number(value.numberValue);
        case ValueType::Null:
            return QStringLiteral("null");
        default:
            return TypeName(value.type);
    }
}

QString joinScalars(const QJsonArray& values) {
    QStringList text;
    for (const auto& value: values) {
        if (text.size() >= kMaxListed) {
            text << QStringLiteral("...");
            break;
        }
        text << scalarText(value);
    }
    return text.join(QStringLiteral(", "));
}

bool pointerLookup(const QJsonObject& document, const QString& pointer, QJsonObject* out) {
    QString path = pointer;
    if (path.startsWith(QLatin1Char('#'))) path.remove(0, 1);
    if (path.isEmpty() || path == QStringLiteral("/")) {
        *out = document;
        return true;
    }
    if (!path.startsWith(QLatin1Char('/'))) return false;

    QJsonValue current = document;
    for (const auto& rawToken: path.mid(1).split(QLatin1Char('/'))) {
        QString token = rawToken;
        token.replace(QStringLiteral("~1"), QStringLiteral("/"));
        token.replace(QStringLiteral("~0"), QStringLiteral("~"));
        if (current.isObject()) {
            const QJsonObject object = current.toObject();
            if (!object.contains(token)) return false;
            current = object.value(token);
            continue;
        }
        if (current.isArray()) {
            bool ok = false;
            const int index = token.toInt(&ok);
            const QJsonArray array = current.toArray();
            if (!ok || index < 0 || index >= array.size()) return false;
            current = array.at(index);
            continue;
        }
        return false;
    }
    if (!current.isObject()) return false;
    *out = current.toObject();
    return true;
}
} // namespace

SchemaValidator::SchemaValidator(QJsonObject document, QJsonObject root)
    : m_document(std::move(document)), m_root(std::move(root)) {
}

std::shared_ptr<SchemaValidator> SchemaValidator::Create(const QJsonObject& schema, const QString& rootRef,
                                                         QString* error) {
    if (schema.isEmpty()) {
        if (error) *error = QObject::tr("The schema is empty");
        return nullptr;
    }
    QJsonObject root = schema;
    if (!rootRef.isEmpty() && !pointerLookup(schema, rootRef, &root)) {
        if (error) *error = QObject::tr("The schema has no definition at %1").arg(rootRef);
        return nullptr;
    }
    return std::shared_ptr<SchemaValidator>(new SchemaValidator(schema, root));
}

std::shared_ptr<SchemaValidator> SchemaValidator::CreateForNode(const QJsonObject& document,
                                                                const QJsonObject& root) {
    return std::shared_ptr<SchemaValidator>(new SchemaValidator(document, root));
}

void SchemaValidator::AllowExtraType(const QString& propertyName, const ValueType type) {
    m_relaxations[propertyName].insert(static_cast<int>(type));
}

bool SchemaValidator::relaxed(const QString& key, const ValueType type) const {
    const auto it = m_relaxations.constFind(key);
    return it != m_relaxations.constEnd() && it->contains(static_cast<int>(type));
}

QJsonObject SchemaValidator::deref(const QJsonObject& node, const int depth) const {
    if (depth > 8) return node;
    const QString ref = node.value(QStringLiteral("$ref")).toString();
    if (ref.isEmpty()) return node;
    QJsonObject target;
    if (!pointerLookup(m_document, ref, &target)) return node;
    return deref(target, depth + 1);
}

bool SchemaValidator::lookup(const QString& pointer, QJsonObject* out) const {
    return pointerLookup(m_document, pointer, out);
}

QList<Issue> SchemaValidator::Validate(const Value& root) const {
    QList<Issue> issues;
    QSet<QString> evaluated;
    validate(m_root, root, {}, 0, issues, &evaluated);
    std::sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) {
        return a.span.offset < b.span.offset;
    });
    return issues;
}

void SchemaValidator::validate(const QJsonObject& schema, const Value& value, const QString& pointer,
                               const int depth, QList<Issue>& issues, QSet<QString>* evaluated) const {
    if (depth > kMaxSchemaDepth || issues.size() >= kMaxIssues) return;

    QSet<QString> local;
    const auto propagate = [&] {
        if (evaluated) evaluated->unite(local);
    };

    if (const QString ref = schema.value(QStringLiteral("$ref")).toString(); !ref.isEmpty()) {
        QJsonObject target;
        if (lookup(ref, &target)) validate(target, value, pointer, depth + 1, issues, &local);
    }

    if (schema.contains(QStringLiteral("type"))) {
        const QStringList types = schemaTypes(schema.value(QStringLiteral("type")));
        bool ok = false;
        for (const auto& name: types) {
            if (matchesType(name, value)) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            issues.append({Severity::Error,
                           QObject::tr("Expected %1, got %2").arg(types.join(QObject::tr(" or ")), TypeName(value.type)),
                           pointer, value.span});
            propagate();
            return;
        }
    }

    if (schema.contains(QStringLiteral("const"))) {
        const QJsonValue expected = schema.value(QStringLiteral("const"));
        if (!equalsJson(value, expected)) {
            issues.append({Severity::Error,
                           QObject::tr("Expected %1 here").arg(scalarText(expected)), pointer, value.span});
            propagate();
            return;
        }
    }

    if (schema.contains(QStringLiteral("enum"))) {
        const QJsonArray allowed = schema.value(QStringLiteral("enum")).toArray();
        bool ok = false;
        for (const auto& entry: allowed) {
            if (equalsJson(value, entry)) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            issues.append({Severity::Error,
                           QObject::tr("%1 is not valid here (expected: %2)")
                               .arg(describeValue(value), joinScalars(allowed)),
                           pointer, value.span});
            propagate();
            return;
        }
    }

    if (schema.value(QStringLiteral("deprecated")).toBool()) {
        issues.append({Severity::Warning, QObject::tr("This option is deprecated"), pointer, value.span});
    }

    if (value.type == ValueType::String && schema.contains(QStringLiteral("pattern"))) {
        const QRegularExpression pattern(schema.value(QStringLiteral("pattern")).toString());
        if (pattern.isValid() && !pattern.match(value.stringValue).hasMatch()) {
            issues.append({Severity::Error,
                           QObject::tr("\"%1\" is not in the expected format").arg(value.stringValue),
                           pointer, value.span});
        }
    }

    if (value.type == ValueType::Number) {
        if (schema.contains(QStringLiteral("minimum"))) {
            const double minimum = schema.value(QStringLiteral("minimum")).toDouble();
            if (value.numberValue < minimum) {
                issues.append({Severity::Error,
                               QObject::tr("Value must be at least %1").arg(minimum, 0, 'g', 16),
                               pointer, value.span});
            }
        }
        if (schema.contains(QStringLiteral("maximum"))) {
            const double maximum = schema.value(QStringLiteral("maximum")).toDouble();
            if (value.numberValue > maximum) {
                issues.append({Severity::Error,
                               QObject::tr("Value must be at most %1").arg(maximum, 0, 'g', 16),
                               pointer, value.span});
            }
        }
    }

    if (value.type == ValueType::Array && schema.contains(QStringLiteral("items"))) {
        const QJsonObject items = schema.value(QStringLiteral("items")).toObject();
        for (int i = 0; i < value.elements.size(); ++i) {
            validate(items, value.elements.at(i), pointer + QStringLiteral("/") + QString::number(i),
                     depth + 1, issues, nullptr);
        }
    }

    if (value.type == ValueType::Object) validateObject(schema, value, pointer, depth, issues, local);

    for (const auto& branch: schema.value(QStringLiteral("allOf")).toArray()) {
        validate(branch.toObject(), value, pointer, depth + 1, issues, &local);
    }
    if (schema.contains(QStringLiteral("anyOf"))) {
        validateUnion(schema.value(QStringLiteral("anyOf")).toArray(), false, value, pointer, depth, issues, local);
    }
    if (schema.contains(QStringLiteral("oneOf"))) {
        validateUnion(schema.value(QStringLiteral("oneOf")).toArray(), true, value, pointer, depth, issues, local);
    }

    if (value.type == ValueType::Object) {
        const QJsonValue unevaluated = schema.value(QStringLiteral("unevaluatedProperties"));
        if (unevaluated.isBool() && !unevaluated.toBool()) {
            for (int i = 0; i < value.keys.size(); ++i) {
                const QString& key = value.keys.at(i);
                if (local.contains(key) || relaxed(key, value.members.at(i).type)) continue;
                issues.append({Severity::Error, QObject::tr("Unknown field \"%1\"").arg(key), pointer,
                               value.keySpans.at(i)});
            }
        }
    }

    propagate();
}

void SchemaValidator::validateObject(const QJsonObject& schema, const Value& value, const QString& pointer,
                                     const int depth, QList<Issue>& issues, QSet<QString>& evaluated) const {
    const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    const QJsonValue additional = schema.value(QStringLiteral("additionalProperties"));
    const QJsonObject propertyNames = schema.value(QStringLiteral("propertyNames")).toObject();

    for (int i = 0; i < value.keys.size(); ++i) {
        const QString& key = value.keys.at(i);
        const Value& member = value.members.at(i);

        if (relaxed(key, member.type)) {
            evaluated.insert(key);
            continue;
        }

        if (!propertyNames.isEmpty()) {
            Value name;
            name.type = ValueType::String;
            name.stringValue = key;
            name.span = value.keySpans.at(i);
            validate(propertyNames, name, pointer, depth + 1, issues, nullptr);
        }

        if (properties.contains(key)) {
            evaluated.insert(key);
            validate(properties.value(key).toObject(), member, pointer + QStringLiteral("/") + key,
                     depth + 1, issues, nullptr);
            continue;
        }
        if (additional.isObject()) {
            evaluated.insert(key);
            validate(additional.toObject(), member, pointer + QStringLiteral("/") + key, depth + 1, issues, nullptr);
            continue;
        }
        if (additional.isBool() && !additional.toBool()) {
            issues.append({Severity::Error, QObject::tr("Unknown field \"%1\"").arg(key), pointer,
                           value.keySpans.at(i)});
        }
    }

    for (const auto& entry: schema.value(QStringLiteral("required")).toArray()) {
        const QString name = entry.toString();
        if (value.indexOfKey(name) < 0) {
            issues.append({Severity::Error, QObject::tr("Missing required field \"%1\"").arg(name), pointer, {value.span.offset, 1}});
        }
    }
}

static QJsonArray pinnedValues(const QJsonObject& rule) {
    if (rule.contains(QStringLiteral("const"))) return QJsonArray{rule.value(QStringLiteral("const"))};
    if (rule.contains(QStringLiteral("enum"))) return rule.value(QStringLiteral("enum")).toArray();
    return {};
}

bool SchemaValidator::branchProperty(const QJsonObject& branch, const QString& key, QJsonObject* out,
                                     const int level) const {
    if (level > 6) return false;
    const QJsonObject node = deref(branch);
    if (const QJsonObject properties = node.value(QStringLiteral("properties")).toObject();
        properties.contains(key)) {
        *out = deref(properties.value(key).toObject());
        return true;
    }
    for (const auto& sub: node.value(QStringLiteral("allOf")).toArray()) {
        if (branchProperty(sub.toObject(), key, out, level + 1)) return true;
    }
    return false;
}

bool SchemaValidator::branchRequires(const QJsonObject& branch, const QString& key, const int level) const {
    if (level > 6) return false;
    const QJsonObject node = deref(branch);
    for (const auto& entry: node.value(QStringLiteral("required")).toArray()) {
        if (entry.toString() == key) return true;
    }
    for (const auto& sub: node.value(QStringLiteral("allOf")).toArray()) {
        if (branchRequires(sub.toObject(), key, level + 1)) return true;
    }
    return false;
}

QStringList SchemaValidator::branchKeys(const QJsonObject& branch, const int level) const {
    if (level > 6) return {};
    const QJsonObject node = deref(branch);
    QStringList keys = node.value(QStringLiteral("properties")).toObject().keys();
    for (const auto& sub: node.value(QStringLiteral("allOf")).toArray()) {
        for (const auto& key: branchKeys(sub.toObject(), level + 1)) {
            if (!keys.contains(key)) keys.append(key);
        }
    }
    return keys;
}

void SchemaValidator::validateUnion(const QJsonArray& branches, const bool exclusive, const Value& value,
                                    const QString& pointer, const int depth, QList<Issue>& issues,
                                    QSet<QString>& evaluated) const {
    if (branches.isEmpty()) return;

    QList<QJsonObject> resolved;
    // A branch that is itself a bare union contributes its own branches, so variants split by a second constant still discriminate.
    std::function<void(const QJsonObject&, int)> flatten = [&](const QJsonObject& node, const int level) {
        const QJsonObject branch = deref(node);
        if (level < 3 && branch.size() == 1) {
            for (const auto& key: {QStringLiteral("oneOf"), QStringLiteral("anyOf")}) {
                if (!branch.contains(key)) continue;
                for (const auto& sub: branch.value(key).toArray()) flatten(sub.toObject(), level + 1);
                return;
            }
        }
        resolved.append(branch);
    };
    for (const auto& branch: branches) flatten(branch.toObject(), 0);

    QList<int> candidates;
    for (int i = 0; i < resolved.size(); ++i) candidates.append(i);

    // Narrow repeatedly by the constants branches pin their properties to: "type" first, then variants of it.
    if (value.type == ValueType::Object) {
        QSet<QString> tried;
        for (int round = 0; round < 4 && candidates.size() > 1; ++round) {
            QString key;
            QList<QJsonArray> allowedPerBranch;
            for (const auto& name: branchKeys(resolved.at(candidates.first()))) {
                if (tried.contains(name)) continue;
                QList<QJsonArray> allowed;
                bool usable = true;
                for (const int index: candidates) {
                    QJsonObject rule;
                    if (!branchProperty(resolved.at(index), name, &rule)) {
                        usable = false;
                        break;
                    }
                    const QJsonArray pinned = pinnedValues(rule);
                    if (pinned.isEmpty()) {
                        usable = false;
                        break;
                    }
                    allowed.append(pinned);
                }
                if (usable) {
                    key = name;
                    allowedPerBranch = allowed;
                    break;
                }
            }
            if (key.isEmpty()) break;
            tried.insert(key);

            QList<int> narrowed;
            const int keyIndex = value.indexOfKey(key);
            if (keyIndex >= 0) {
                const Value& given = value.members.at(keyIndex);
                for (int n = 0; n < candidates.size(); ++n) {
                    for (const auto& option: allowedPerBranch.at(n)) {
                        // the empty string marks "may be omitted", not a variant name
                        if (scalarText(option).isEmpty() || !equalsJson(given, option)) continue;
                        narrowed.append(candidates.at(n));
                        break;
                    }
                }
                if (narrowed.isEmpty()) {
                    QJsonArray names;
                    QSet<QString> seen;
                    for (const auto& options: allowedPerBranch) {
                        for (const auto& option: options) {
                            const QString text = scalarText(option);
                            if (text.isEmpty() || seen.contains(text)) continue;
                            seen.insert(text);
                            names.append(option);
                        }
                    }
                    evaluated.insert(key);
                    issues.append({Severity::Error,
                                   QObject::tr("Unknown %1 %2 (expected: %3)")
                                       .arg(key, describeValue(given), joinScalars(names)),
                                   pointer, given.span, true});
                    return;
                }
            } else {
                for (const int index: candidates) {
                    if (!branchRequires(resolved.at(index), key)) narrowed.append(index);
                }
                if (narrowed.isEmpty()) break;
            }
            candidates = narrowed;
        }
    }

    if (candidates.size() == 1) {
        validate(resolved.at(candidates.first()), value, pointer, depth + 1, issues, &evaluated);
        return;
    }

    int bestIndex = -1;
    int bestScore = 0;
    QList<Issue> bestIssues;
    QSet<QString> bestEvaluated;
    for (const int index: candidates) {
        QList<Issue> trial;
        QSet<QString> trialEvaluated;
        validate(resolved.at(index), value, pointer, depth + 1, trial, &trialEvaluated);
        if (errorCount(trial) == 0) {
            evaluated.unite(trialEvaluated);
            issues.append(trial);
            return;
        }
        const int score = errorCount(trial) + (rejectedVariant(trial) ? kVariantPenalty : 0);
        if (bestIndex < 0 || score < bestScore) {
            bestIndex = index;
            bestScore = score;
            bestIssues = trial;
            bestEvaluated = trialEvaluated;
        }
    }

    if (exclusive && !bestIssues.isEmpty()) {
        evaluated.unite(bestEvaluated);
        issues.append(bestIssues.mid(0, kMaxUnionIssues));
        return;
    }

    QStringList forms;
    for (const auto& branch: resolved) {
        for (const auto& name: schemaTypes(branch.value(QStringLiteral("type")))) {
            if (!name.isEmpty() && !forms.contains(name)) forms.append(name);
        }
    }
    if (forms.isEmpty()) {
        issues.append({Severity::Error, QObject::tr("Value does not match any accepted form"), pointer, value.span});
    } else {
        issues.append({Severity::Error,
                       QObject::tr("Expected %1, got %2").arg(forms.join(QObject::tr(" or ")), TypeName(value.type)),
                       pointer, value.span});
    }
}
} // namespace JsonEdit
