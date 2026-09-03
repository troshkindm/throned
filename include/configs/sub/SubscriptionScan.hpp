#pragma once

#include <QByteArray>

#include <cstddef>
#include <string_view>

// Allocation-free scanning over a subscription body; items are views into the caller's buffer.
namespace Subscription::scan {
    inline bool isSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    }

    inline bool isBase64Char(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    }

    inline unsigned char byteAt(std::string_view s, std::size_t i) {
        return static_cast<unsigned char>(s[i]);
    }

    inline std::string_view trim(std::string_view s) {
        for (;;) {
            if (!s.empty() && isSpace(s.front())) {
                s.remove_prefix(1);
            } else if (s.size() >= 2 && byteAt(s, 0) == 0xC2 && byteAt(s, 1) == 0xA0) {
                s.remove_prefix(2);
            } else if (s.size() >= 3 && byteAt(s, 0) == 0xEF && byteAt(s, 1) == 0xBB && byteAt(s, 2) == 0xBF) {
                s.remove_prefix(3);
            } else {
                break;
            }
        }
        for (;;) {
            if (!s.empty() && isSpace(s.back())) {
                s.remove_suffix(1);
            } else if (s.size() >= 2 && byteAt(s, s.size() - 2) == 0xC2 && byteAt(s, s.size() - 1) == 0xA0) {
                s.remove_suffix(2);
            } else {
                break;
            }
        }
        return s;
    }

    inline std::string_view view(const QByteArray &buf) {
        return {buf.constData(), static_cast<std::size_t>(buf.size())};
    }

    inline void trimInPlace(QByteArray &buf) {
        const auto kept = trim(view(buf));
        const auto front = static_cast<qsizetype>(kept.data() - buf.constData());
        buf.chop(buf.size() - front - static_cast<qsizetype>(kept.size()));
        buf.remove(0, front);
    }

    inline bool startsWithNoCase(std::string_view s, std::string_view lowerPrefix) {
        if (s.size() < lowerPrefix.size()) return false;
        for (std::size_t i = 0; i < lowerPrefix.size(); ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != lowerPrefix[i]) return false;
        }
        return true;
    }

    // Index of the bracket closing the one at `open`, honouring JSON string escapes; npos when unbalanced.
    inline std::size_t matchingClose(std::string_view s, std::size_t open) {
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (std::size_t i = open; i < s.size(); ++i) {
            const char c = s[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') {
                inString = true;
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                if (--depth == 0) return i;
            }
        }
        return std::string_view::npos;
    }

    // Same segments as QString::split('\n'); `fn` returns false to stop.
    template <typename F>
    void forEachLine(std::string_view s, F &&fn) {
        std::size_t idx = 0;
        for (;;) {
            const std::size_t nl = s.find('\n', idx);
            if (nl == std::string_view::npos) {
                fn(s.substr(idx));
                return;
            }
            if (!fn(s.substr(idx, nl - idx))) return;
            idx = nl + 1;
        }
    }

    // One item per line, except that a line opening a bracket yields the whole balanced block.
    template <typename F>
    void forEachItem(std::string_view s, F &&fn) {
        const std::size_t n = s.size();
        std::size_t idx = 0;
        while (idx < n) {
            if (s[idx] == '\n') {
                ++idx;
                continue;
            }
            if (s[idx] == '{' || s[idx] == '[') {
                if (const auto end = matchingClose(s, idx); end != std::string_view::npos) {
                    fn(s.substr(idx, end - idx + 1));
                    idx = end + 1;
                    continue;
                }
            }
            std::size_t nl = s.find('\n', idx);
            if (nl == std::string_view::npos) nl = n;
            fn(s.substr(idx, nl - idx));
            idx = nl + 1;
        }
    }

    inline bool looksLikeBase64(std::string_view s) {
        if (s.empty()) return false;
        for (const char c : s) {
            if (!isBase64Char(c)) return false;
        }
        return true;
    }

    // A single base64 body wrapped at a fixed column, as `base64` and MIME emit it; several pasted blobs have ragged widths.
    inline bool looksLikeWrappedBase64(std::string_view s) {
        std::size_t width = 0;
        std::size_t lines = 0;
        bool sawShort = false;
        bool ok = true;
        forEachLine(s, [&](std::string_view raw) {
            auto line = raw;
            while (!line.empty() && isSpace(line.back())) line.remove_suffix(1);
            if (line.empty()) return true;
            for (const char c : line) {
                if (!isBase64Char(c)) {
                    ok = false;
                    return false;
                }
            }
            ++lines;
            if (width == 0) {
                width = line.size();
                return true;
            }
            if (line.size() > width || sawShort) {
                ok = false;
                return false;
            }
            if (line.size() < width) sawShort = true;
            return true;
        });
        return ok && lines >= 2 && width % 4 == 0;
    }

    inline QByteArray decodeBase64(std::string_view s, QByteArray::Base64Options options = QByteArray::Base64Encoding) {
        const auto raw = QByteArray::fromRawData(s.data(), static_cast<qsizetype>(s.size()));
        const auto result = QByteArray::fromBase64Encoding(raw, options | QByteArray::AbortOnBase64DecodingErrors);
        return result ? result.decoded : QByteArray();
    }
}
