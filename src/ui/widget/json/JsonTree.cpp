#include "include/ui/widget/json/JsonTree.h"

#include <QObject>

namespace JsonEdit {
int Value::indexOfKey(const QString& key) const {
    for (int i = keys.size() - 1; i >= 0; --i) {
        if (keys.at(i) == key) return i;
    }
    return -1;
}

QString TypeName(const ValueType type) {
    switch (type) {
        case ValueType::Null:
            return QObject::tr("null");
        case ValueType::Bool:
            return QObject::tr("boolean");
        case ValueType::Number:
            return QObject::tr("number");
        case ValueType::String:
            return QObject::tr("string");
        case ValueType::Array:
            return QObject::tr("array");
        case ValueType::Object:
            return QObject::tr("object");
    }
    return {};
}

void OffsetToLineColumn(const QString& text, const int offset, int* line, int* column) {
    int l = 1, c = 1;
    const int end = qMin(offset, static_cast<int>(text.size()));
    for (int i = 0; i < end; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            l++;
            c = 1;
        } else {
            c++;
        }
    }
    if (line) *line = l;
    if (column) *column = c;
}

namespace {
constexpr int kMaxParseDepth = 96;

class Parser {
public:
    explicit Parser(const QString& text) : m_text(text) {}

    ParseResult Run() {
        ParseResult res;
        skipTrivia();
        if (atEnd()) {
            res.error = QObject::tr("Empty document");
            res.errorSpan = {m_pos, 0};
            return res;
        }
        if (!parseValue(0, &res.root)) {
            res.error = m_error;
            res.errorSpan = m_errorSpan;
            return res;
        }
        skipTrivia();
        if (!atEnd()) {
            res.error = QObject::tr("Unexpected content after the end of the document");
            res.errorSpan = {m_pos, 1};
            return res;
        }
        res.ok = true;
        return res;
    }

private:
    const QString& m_text;
    int m_pos = 0;
    QString m_error;
    Span m_errorSpan;

    [[nodiscard]] bool atEnd() const { return m_pos >= m_text.size(); }

    [[nodiscard]] QChar peek() const {
        return m_pos < m_text.size() ? m_text.at(m_pos) : QChar(u'\0');
    }

    bool fail(const QString& message, const int offset, const int length = 1) {
        if (m_error.isEmpty()) {
            m_error = message;
            m_errorSpan = {offset, length};
        }
        return false;
    }

    void skipTrivia() {
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
                m_pos++;
                continue;
            }
            if (c == QLatin1Char('/') && m_pos + 1 < m_text.size()) {
                const QChar next = m_text.at(m_pos + 1);
                if (next == QLatin1Char('/')) {
                    m_pos += 2;
                    while (m_pos < m_text.size() && m_text.at(m_pos) != QLatin1Char('\n')) m_pos++;
                    continue;
                }
                if (next == QLatin1Char('*')) {
                    m_pos += 2;
                    while (m_pos + 1 < m_text.size() &&
                           !(m_text.at(m_pos) == QLatin1Char('*') && m_text.at(m_pos + 1) == QLatin1Char('/'))) {
                        m_pos++;
                    }
                    m_pos = m_pos + 1 < m_text.size() ? m_pos + 2 : static_cast<int>(m_text.size());
                    continue;
                }
            }
            break;
        }
    }

    bool expect(const QChar c, const QString& message) {
        skipTrivia();
        if (peek() != c) return fail(message, m_pos);
        m_pos++;
        return true;
    }

    bool parseValue(const int depth, Value* out) {
        if (depth > kMaxParseDepth) return fail(QObject::tr("The document is nested too deeply"), m_pos);
        skipTrivia();
        if (atEnd()) return fail(QObject::tr("Unexpected end of document"), m_pos, 0);

        switch (peek().unicode()) {
            case u'{':
                return parseObject(depth, out);
            case u'[':
                return parseArray(depth, out);
            case u'"':
                return parseString(out);
            case u't':
                return parseKeyword(QStringLiteral("true"), out);
            case u'f':
                return parseKeyword(QStringLiteral("false"), out);
            case u'n':
                return parseKeyword(QStringLiteral("null"), out);
            default:
                return parseNumber(out);
        }
    }

    bool parseObject(const int depth, Value* out) {
        const int start = m_pos;
        m_pos++;
        out->type = ValueType::Object;
        skipTrivia();
        if (peek() == QLatin1Char('}')) {
            m_pos++;
            out->span = {start, m_pos - start};
            return true;
        }
        while (true) {
            skipTrivia();
            if (peek() != QLatin1Char('"')) return fail(QObject::tr("Expected a property name in quotes"), m_pos);
            Value key;
            if (!parseString(&key)) return false;
            if (!expect(QLatin1Char(':'), QObject::tr("Expected a colon after the property name"))) return false;
            Value member;
            if (!parseValue(depth + 1, &member)) return false;
            out->keys.append(key.stringValue);
            out->keySpans.append(key.span);
            out->members.append(member);
            skipTrivia();
            if (peek() == QLatin1Char(',')) {
                m_pos++;
                skipTrivia();
                if (peek() == QLatin1Char('}')) return fail(QObject::tr("Trailing comma"), m_pos - 1);
                continue;
            }
            if (peek() == QLatin1Char('}')) {
                m_pos++;
                out->span = {start, m_pos - start};
                return true;
            }
            return fail(QObject::tr("Expected a comma or a closing brace"), m_pos);
        }
    }

    bool parseArray(const int depth, Value* out) {
        const int start = m_pos;
        m_pos++;
        out->type = ValueType::Array;
        skipTrivia();
        if (peek() == QLatin1Char(']')) {
            m_pos++;
            out->span = {start, m_pos - start};
            return true;
        }
        while (true) {
            Value element;
            if (!parseValue(depth + 1, &element)) return false;
            out->elements.append(element);
            skipTrivia();
            if (peek() == QLatin1Char(',')) {
                m_pos++;
                skipTrivia();
                if (peek() == QLatin1Char(']')) return fail(QObject::tr("Trailing comma"), m_pos - 1);
                continue;
            }
            if (peek() == QLatin1Char(']')) {
                m_pos++;
                out->span = {start, m_pos - start};
                return true;
            }
            return fail(QObject::tr("Expected a comma or a closing bracket"), m_pos);
        }
    }

    bool parseString(Value* out) {
        const int start = m_pos;
        m_pos++;
        QString text;
        while (true) {
            if (atEnd()) return fail(QObject::tr("Unterminated string"), start);
            const QChar c = m_text.at(m_pos);
            if (c == QLatin1Char('"')) {
                m_pos++;
                out->type = ValueType::String;
                out->stringValue = text;
                out->span = {start, m_pos - start};
                return true;
            }
            if (c == QLatin1Char('\\')) {
                if (m_pos + 1 >= m_text.size()) return fail(QObject::tr("Unterminated escape sequence"), m_pos);
                const QChar esc = m_text.at(m_pos + 1);
                m_pos += 2;
                switch (esc.unicode()) {
                    case u'"':
                        text += QLatin1Char('"');
                        break;
                    case u'\\':
                        text += QLatin1Char('\\');
                        break;
                    case u'/':
                        text += QLatin1Char('/');
                        break;
                    case u'b':
                        text += QLatin1Char('\b');
                        break;
                    case u'f':
                        text += QLatin1Char('\f');
                        break;
                    case u'n':
                        text += QLatin1Char('\n');
                        break;
                    case u'r':
                        text += QLatin1Char('\r');
                        break;
                    case u't':
                        text += QLatin1Char('\t');
                        break;
                    case u'u': {
                        if (m_pos + 4 > m_text.size())
                            return fail(QObject::tr("Incomplete unicode escape sequence"), m_pos - 2, 2);
                        bool ok = false;
                        const auto code = m_text.mid(m_pos, 4).toUShort(&ok, 16);
                        if (!ok) return fail(QObject::tr("Invalid unicode escape sequence"), m_pos - 2, 6);
                        text += QChar(code);
                        m_pos += 4;
                        break;
                    }
                    default:
                        return fail(QObject::tr("Invalid escape sequence"), m_pos - 2, 2);
                }
                continue;
            }
            if (c < QChar(0x20)) return fail(QObject::tr("Control character in string"), m_pos);
            text += c;
            m_pos++;
        }
    }

    bool parseKeyword(const QString& keyword, Value* out) {
        const int start = m_pos;
        if (m_text.mid(m_pos, keyword.size()) != keyword) return fail(QObject::tr("Invalid value"), m_pos);
        m_pos += static_cast<int>(keyword.size());
        out->span = {start, m_pos - start};
        if (keyword == QStringLiteral("null")) {
            out->type = ValueType::Null;
        } else {
            out->type = ValueType::Bool;
            out->boolValue = keyword == QStringLiteral("true");
        }
        return true;
    }

    bool parseNumber(Value* out) {
        const int start = m_pos;
        if (peek() == QLatin1Char('-') || peek() == QLatin1Char('+')) m_pos++;
        bool fractional = false;
        while (!atEnd()) {
            const QChar c = m_text.at(m_pos);
            if (c.isDigit()) {
                m_pos++;
                continue;
            }
            if (c == QLatin1Char('.') || c == QLatin1Char('e') || c == QLatin1Char('E')) {
                fractional = true;
                m_pos++;
                if (!atEnd() && (peek() == QLatin1Char('-') || peek() == QLatin1Char('+'))) m_pos++;
                continue;
            }
            break;
        }
        if (m_pos == start) return fail(QObject::tr("Invalid value"), m_pos);
        bool ok = false;
        const double number = m_text.mid(start, m_pos - start).toDouble(&ok);
        if (!ok) return fail(QObject::tr("Invalid number"), start, m_pos - start);
        out->type = ValueType::Number;
        out->numberValue = number;
        out->integral = !fractional;
        out->span = {start, m_pos - start};
        return true;
    }
};
} // namespace

ParseResult Parse(const QString& text) {
    Parser parser(text);
    return parser.Run();
}
} // namespace JsonEdit
