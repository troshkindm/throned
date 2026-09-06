#pragma once

#include <QList>
#include <QString>

// Comments are accepted as whitespace, matching what Qt's own parser tolerates.
namespace JsonEdit {
enum class ValueType { Null,
                       Bool,
                       Number,
                       String,
                       Array,
                       Object };

struct Span {
    int offset = 0;
    int length = 0;
};

struct Value {
    ValueType type = ValueType::Null;
    Span span;

    bool boolValue = false;
    double numberValue = 0;
    bool integral = false;
    QString stringValue;

    QList<QString> keys;
    QList<Span> keySpans;
    QList<Value> members;
    QList<Value> elements;

    [[nodiscard]] int indexOfKey(const QString& key) const;
};

struct ParseResult {
    bool ok = false;
    Value root;
    QString error;
    Span errorSpan;
};

ParseResult Parse(const QString& text);

QString TypeName(ValueType type);
void OffsetToLineColumn(const QString& text, int offset, int* line, int* column);
} // namespace JsonEdit
