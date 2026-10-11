#include "include/scanner/IpListParse.h"

#include <QSet>

#include <algorithm>
#include <bit>
#include <iterator>
#include <utility>
#include <vector>

namespace Scanner {
namespace {
constexpr int kIpListParseMaxSamples = 5;
constexpr int kIpListParseSampleChars = 64;

// IPv4 lives in the low 32 bits of lo.
struct IpListParseAddr {
    bool v6 = false;
    quint64 hi = 0;
    quint64 lo = 0;
};

enum class IpListParseKind { Address,
                             Port,
                             Other };

struct IpListParseToken {
    QStringView text;
    int field = 0;
    IpListParseKind kind = IpListParseKind::Other;
    QStringList cidrs;
    int port = -1;
    bool consumed = false;
};

int ipListParseBits(const IpListParseAddr &addr) { return addr.v6 ? 128 : 32; }

bool ipListParseDecimal(QStringView text, int maxDigits, quint32 &out) {
    if (text.isEmpty() || text.size() > maxDigits) return false;
    quint32 value = 0;
    for (const QChar c: text) {
        if (c < u'0' || c > u'9') return false;
        value = value * 10 + (c.unicode() - u'0');
    }
    out = value;
    return true;
}

int ipListParseHexDigit(QChar c) {
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9') return u - u'0';
    if (u >= u'a' && u <= u'f') return u - u'a' + 10;
    if (u >= u'A' && u <= u'F') return u - u'A' + 10;
    return -1;
}

bool ipListParsePort(QStringView text, int &port) {
    quint32 value = 0;
    if (!ipListParseDecimal(text, 5, value) || value > 65535) return false;
    port = static_cast<int>(value);
    return true;
}

// inet_aton's short ("1.2.3"), integer and octal ("010.0.0.1") forms are refused, as Go's netip does.
bool ipListParseV4(QStringView text, quint32 &out) {
    quint32 value = 0;
    int parts = 0;
    qsizetype start = 0;
    while (true) {
        const qsizetype dot = text.indexOf(u'.', start);
        const QStringView part = dot < 0 ? text.mid(start) : text.mid(start, dot - start);
        quint32 octet = 0;
        if (!ipListParseDecimal(part, 3, octet) || octet > 255) return false;
        if (part.size() > 1 && part.front() == u'0') return false;
        if (++parts > 4) return false;
        value = (value << 8) | octet;
        if (dot < 0) break;
        start = dot + 1;
    }
    if (parts != 4) return false;
    out = value;
    return true;
}

bool ipListParseV6Part(QStringView text, bool allowV4Tail, quint16 *groups, int &count) {
    if (text.isEmpty()) return true;
    qsizetype start = 0;
    while (true) {
        const qsizetype colon = text.indexOf(u':', start);
        const QStringView group = colon < 0 ? text.mid(start) : text.mid(start, colon - start);
        if (colon < 0 && allowV4Tail && group.contains(u'.')) {
            quint32 v4 = 0;
            if (count > 6 || !ipListParseV4(group, v4)) return false;
            groups[count++] = static_cast<quint16>(v4 >> 16);
            groups[count++] = static_cast<quint16>(v4 & 0xffff);
            return true;
        }
        if (group.isEmpty() || group.size() > 4 || count >= 8) return false;
        quint16 value = 0;
        for (const QChar c: group) {
            const int digit = ipListParseHexDigit(c);
            if (digit < 0) return false;
            value = static_cast<quint16>((value << 4) | digit);
        }
        groups[count++] = value;
        if (colon < 0) return true;
        start = colon + 1;
    }
}

bool ipListParseV6(QStringView text, quint64 &hi, quint64 &lo) {
    quint16 head[8] = {};
    quint16 tail[8] = {};
    int headCount = 0;
    int tailCount = 0;
    const qsizetype gap = text.indexOf(QStringView(u"::"));
    if (gap < 0) {
        if (!ipListParseV6Part(text, true, head, headCount) || headCount != 8) return false;
    } else {
        if (text.indexOf(QStringView(u"::"), gap + 1) >= 0) return false;
        if (!ipListParseV6Part(text.left(gap), false, head, headCount)) return false;
        if (!ipListParseV6Part(text.mid(gap + 2), true, tail, tailCount)) return false;
        if (headCount + tailCount > 7) return false;
    }
    quint16 groups[8] = {};
    for (int i = 0; i < headCount; ++i) groups[i] = head[i];
    for (int i = 0; i < tailCount; ++i) groups[8 - tailCount + i] = tail[i];
    hi = 0;
    lo = 0;
    for (int i = 0; i < 4; ++i) hi = (hi << 16) | groups[i];
    for (int i = 4; i < 8; ++i) lo = (lo << 16) | groups[i];
    return true;
}

bool ipListParseAddress(QStringView text, IpListParseAddr &out) {
    if (text.contains(u':')) {
        const qsizetype zone = text.indexOf(u'%');
        if (zone >= 0) text = text.left(zone);
        out.v6 = true;
        return ipListParseV6(text, out.hi, out.lo);
    }
    quint32 v4 = 0;
    if (!ipListParseV4(text, v4)) return false;
    out = {false, 0, v4};
    return true;
}

bool ipListParseIsMapped(const IpListParseAddr &addr) {
    return addr.v6 && addr.hi == 0 && (addr.lo >> 32) == 0xffff;
}

void ipListParseMask(IpListParseAddr &addr, int prefix) {
    if (!addr.v6) {
        addr.lo &= (0xffffffffULL << (32 - prefix)) & 0xffffffffULL;
    } else if (prefix < 64) {
        addr.hi &= prefix == 0 ? 0 : ~0ULL << (64 - prefix);
        addr.lo = 0;
    } else if (prefix == 64) {
        addr.lo = 0;
    } else if (prefix < 128) {
        addr.lo &= ~0ULL << (128 - prefix);
    }
}

QString ipListParseFormat(const IpListParseAddr &addr) {
    if (!addr.v6) {
        const auto v = static_cast<quint32>(addr.lo);
        return QString::number(v >> 24) + QLatin1Char('.') + QString::number((v >> 16) & 0xff) + QLatin1Char('.') +
               QString::number((v >> 8) & 0xff) + QLatin1Char('.') + QString::number(v & 0xff);
    }
    quint16 groups[8];
    for (int i = 0; i < 4; ++i) groups[i] = static_cast<quint16>(addr.hi >> (48 - 16 * i));
    for (int i = 0; i < 4; ++i) groups[4 + i] = static_cast<quint16>(addr.lo >> (48 - 16 * i));
    // RFC 5952, as Go's netip prints it: the first longest run of two or more zero groups becomes "::".
    int bestStart = -1;
    int bestLength = 1;
    for (int i = 0; i < 8;) {
        if (groups[i] != 0) {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && groups[j] == 0) ++j;
        if (j - i > bestLength) {
            bestStart = i;
            bestLength = j - i;
        }
        i = j;
    }
    QString out;
    for (int i = 0; i < 8; ++i) {
        if (i == bestStart) {
            out += QLatin1String("::");
            i += bestLength - 1;
            continue;
        }
        if (!out.isEmpty() && !out.endsWith(QLatin1Char(':'))) out += QLatin1Char(':');
        out += QString::number(groups[i], 16);
    }
    return out;
}

QString ipListParseCidr(const IpListParseAddr &addr, int prefix) {
    const QString address = ipListParseFormat(addr);
    return prefix >= ipListParseBits(addr) ? address : address + QLatin1Char('/') + QString::number(prefix);
}

bool ipListParseNetwork(QStringView text, IpListParseAddr &addr, int &prefix) {
    text = text.trimmed();
    QStringView body = text;
    QStringView suffix;
    if (text.startsWith(u'[')) {
        const qsizetype close = text.indexOf(u']');
        if (close < 0) return false;
        body = text.mid(1, close - 1);
        suffix = text.mid(close + 1);
        if (!suffix.isEmpty() && !suffix.startsWith(u'/')) return false;
    }
    QStringView prefixText;
    bool hasPrefix = false;
    if (const qsizetype slash = body.indexOf(u'/'); slash >= 0) {
        if (!suffix.isEmpty()) return false;
        prefixText = body.mid(slash + 1);
        body = body.left(slash);
        hasPrefix = true;
    } else if (!suffix.isEmpty()) {
        prefixText = suffix.mid(1);
        hasPrefix = true;
    }
    if (!ipListParseAddress(body, addr)) return false;
    const int bits = ipListParseBits(addr);
    prefix = bits;
    if (hasPrefix) {
        quint32 value = 0;
        if (!ipListParseDecimal(prefixText, 3, value) || value > static_cast<quint32>(bits)) return false;
        prefix = static_cast<int>(value);
    }
    if (ipListParseIsMapped(addr) && prefix >= 96) {
        addr = {false, 0, addr.lo & 0xffffffffULL};
        prefix -= 96;
    }
    if (prefix == 0) return false;
    ipListParseMask(addr, prefix);
    return true;
}

bool ipListParseLess(const IpListParseAddr &a, const IpListParseAddr &b) {
    return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo;
}

// false when the cover would be the whole address space (/0).
bool ipListParseCoverRange(IpListParseAddr first, const IpListParseAddr &last, QStringList &out) {
    const int bits = ipListParseBits(first);
    const quint64 topHi = first.v6 ? ~0ULL : 0;
    const quint64 topLo = first.v6 ? ~0ULL : 0xffffffffULL;
    if (first.hi == 0 && first.lo == 0 && last.hi == topHi && last.lo == topLo) return false;
    while (true) {
        int align = first.lo != 0 ? std::countr_zero(first.lo)
                                  : (first.hi != 0 ? 64 + std::countr_zero(first.hi) : 128);
        align = std::min(align, bits);
        const quint64 diffLo = last.lo - first.lo;
        const quint64 diffHi = last.hi - first.hi - (last.lo < first.lo ? 1 : 0);
        const quint64 sizeLo = diffLo + 1;
        const quint64 sizeHi = diffHi + (sizeLo == 0 ? 1 : 0);
        const int fit = sizeHi != 0 ? 64 + static_cast<int>(std::bit_width(sizeHi)) - 1
                                    : static_cast<int>(std::bit_width(sizeLo)) - 1;
        const int k = std::min(align, fit);
        out << ipListParseCidr(first, bits - k);
        // first is aligned to 2^k, so OR-ing 2^k - 1 yields the block's last address without a carry.
        IpListParseAddr blockEnd = first;
        if (k >= 64) {
            blockEnd.lo = ~0ULL;
            if (k > 64) blockEnd.hi |= (1ULL << (k - 64)) - 1;
        } else if (k > 0) {
            blockEnd.lo |= (1ULL << k) - 1;
        }
        if (blockEnd.hi == last.hi && blockEnd.lo == last.lo) return true;
        first.lo = blockEnd.lo + 1;
        first.hi = blockEnd.hi + (first.lo == 0 ? 1 : 0);
    }
}

bool ipListParseRangeEnd(QStringView text, IpListParseAddr &addr) {
    if (text.startsWith(u'[') && text.endsWith(u']')) text = text.mid(1, text.size() - 2);
    if (text.contains(u'/') || !ipListParseAddress(text, addr)) return false;
    if (ipListParseIsMapped(addr)) addr = {false, 0, addr.lo & 0xffffffffULL};
    return true;
}

bool ipListParseRangeToken(QStringView token, QStringList &cidrs, int &port) {
    const qsizetype dash = token.indexOf(u'-');
    const QStringView left = token.left(dash);
    QStringView right = token.mid(dash + 1);
    if (right.startsWith(u'[')) {
        const qsizetype close = right.indexOf(u']');
        if (close < 0) return false;
        const QStringView rest = right.mid(close + 1);
        if (!rest.isEmpty() && (!rest.startsWith(u':') || !ipListParsePort(rest.mid(1), port))) return false;
        right = right.left(close + 1);
    } else if (right.count(u':') == 1) {
        const qsizetype colon = right.indexOf(u':');
        if (!ipListParsePort(right.mid(colon + 1), port)) return false;
        right = right.left(colon);
    }
    IpListParseAddr first;
    IpListParseAddr last;
    if (!ipListParseRangeEnd(left, first)) return false;
    quint32 octet = 0;
    if (!first.v6 && ipListParseDecimal(right, 3, octet)) {
        if (octet > 255 || (right.size() > 1 && right.front() == u'0')) return false;
        last = first;
        last.lo = (first.lo & ~0xffULL) | octet;
    } else if (!ipListParseRangeEnd(right, last) || last.v6 != first.v6) {
        return false;
    }
    if (ipListParseLess(last, first)) std::swap(first, last);
    return ipListParseCoverRange(first, last, cidrs);
}

bool ipListParseAddressToken(QStringView token, QStringList &cidrs, int &port) {
    port = -1;
    if (token.isEmpty()) return false;
    if (token.contains(u'-')) return ipListParseRangeToken(token, cidrs, port);
    QStringView network = token;
    if (token.startsWith(u'[')) {
        const qsizetype close = token.indexOf(u']');
        if (close < 0) return false;
        const QStringView rest = token.mid(close + 1);
        if (rest.startsWith(u':')) {
            if (!ipListParsePort(rest.mid(1), port)) return false;
            network = token.left(close + 1);
        }
    } else if (token.count(u':') == 1) {
        const qsizetype colon = token.indexOf(u':');
        if (!ipListParsePort(token.mid(colon + 1), port)) return false;
        network = token.left(colon);
    }
    IpListParseAddr addr;
    int prefix = 0;
    if (!ipListParseNetwork(network, addr, prefix)) return false;
    cidrs << ipListParseCidr(addr, prefix);
    return true;
}

// Tabular rows ignore unrelated columns, but a field shaped like an address that fails to parse is reported.
bool ipListParseLooksLikeAddress(QStringView token) {
    if (token.isEmpty()) return false;
    if (token.startsWith(u'[') || token.contains(QStringView(u"::")) || token.count(u':') >= 7) return true;
    if (token.count(u'.') < 2 || token.front() < u'0' || token.front() > u'9') return false;
    return std::all_of(token.begin(), token.end(), [](QChar c) {
        return (c >= u'0' && c <= u'9') || c == u'.' || c == u'/' || c == u':' || c == u'-';
    });
}

// A header must name a column: a bare label line would otherwise switch the file to tabular parsing.
bool ipListParseIsColumnName(QStringView token) {
    static const char *const kNames[] = {"address", "host", "hostname", "cidr", "subnet", "network",
                                         "prefix", "range", "endpoint", "server", "target"};
    if (token.startsWith(QLatin1String("ip"), Qt::CaseInsensitive) ||
        token.startsWith(QLatin1String("addr"), Qt::CaseInsensitive) ||
        token.contains(QLatin1String("port"), Qt::CaseInsensitive))
        return true;
    return std::any_of(std::begin(kNames), std::end(kNames), [token](const char *name) {
        return token.compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
    });
}

bool ipListParseIsSeparator(QChar c) {
    return c == u',' || c == u';' || c == u'\t' || c == u'|';
}

bool ipListParseRangeChar(QChar c) {
    return ipListParseHexDigit(c) >= 0 || c == u'.' || c == u':' || c == u'[' || c == u']';
}

QString ipListParseJoinRanges(QStringView line) {
    QString out;
    out.reserve(line.size());
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == u'-' || c == QChar(0x2013)) {
            qsizetype end = out.size();
            while (end > 0 && out[end - 1].isSpace()) --end;
            qsizetype next = i + 1;
            while (next < line.size() && line[next].isSpace()) ++next;
            if (end > 0 && next < line.size() && ipListParseRangeChar(out[end - 1]) && ipListParseRangeChar(line[next])) {
                out.truncate(end);
                out += QLatin1Char('-');
                i = next - 1;
                continue;
            }
        }
        out += c;
    }
    return out;
}

QStringView ipListParseUnquote(QStringView text) {
    text = text.trimmed();
    while (!text.isEmpty() && (text.front() == u'"' || text.front() == u'\'')) text = text.mid(1);
    while (!text.isEmpty() && (text.back() == u'"' || text.back() == u'\'')) text.chop(1);
    return text.trimmed();
}

void ipListParseSplit(QStringView text, bool bySeparator, std::vector<QStringView> &out) {
    qsizetype start = 0;
    for (qsizetype i = 0; i <= text.size(); ++i) {
        const bool boundary = i == text.size() || (bySeparator ? ipListParseIsSeparator(text[i]) : text[i].isSpace());
        if (!boundary) continue;
        const QStringView part = text.mid(start, i - start);
        // Separated fields keep their empty cells so column indexes line up with the header.
        if (bySeparator || !part.isEmpty()) out.push_back(part);
        start = i + 1;
    }
}
} // namespace

QString NormalizeCidr(const QString &text) {
    IpListParseAddr addr;
    int prefix = 0;
    if (!ipListParseNetwork(text, addr, prefix)) return {};
    return ipListParseCidr(addr, prefix);
}

ParseResult ParseIpListText(const QByteArray &text, int defaultPort) {
    ParseResult result;
    if (defaultPort < 0 || defaultPort > 65535) defaultPort = 0;

    QString content = QString::fromUtf8(text);
    if (content.startsWith(QChar(0xFEFF))) content.remove(0, 1);

    QSet<QString> seen;
    bool firstContentLine = true;
    bool tabularFile = false;
    int portColumn = -1;

    const auto reject = [&result](QStringView token) {
        ++result.rejected;
        if (result.samples.size() < kIpListParseMaxSamples)
            result.samples << token.left(kIpListParseSampleChars).toString();
    };
    const auto add = [&](const QStringList &cidrs, int port) {
        if (port <= 0) port = defaultPort;
        for (const auto &cidr: cidrs) {
            const QString key = cidr + QLatin1Char(' ') + QString::number(port);
            const auto before = seen.size();
            seen.insert(key);
            if (seen.size() == before) {
                ++result.duplicates;
                continue;
            }
            result.entries.append({cidr, port, 0});
        }
    };

    std::vector<QStringView> fields;
    std::vector<QStringView> words;
    std::vector<IpListParseToken> tokens;
    const QStringView all(content);
    qsizetype lineStart = 0;
    while (lineStart < all.size()) {
        qsizetype lineEnd = all.indexOf(u'\n', lineStart);
        if (lineEnd < 0) lineEnd = all.size();
        QStringView line = all.mid(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        if (const qsizetype hash = line.indexOf(u'#'); hash >= 0) line = line.left(hash);
        if (const qsizetype slashes = line.indexOf(QStringView(u"//")); slashes >= 0) line = line.left(slashes);
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';')) continue;

        QString joined;
        if (line.contains(u'-') || line.contains(QChar(0x2013))) {
            joined = ipListParseJoinRanges(line);
            line = joined;
        }

        const bool separated = std::any_of(line.begin(), line.end(), ipListParseIsSeparator);
        fields.clear();
        ipListParseSplit(line, separated, fields);
        tokens.clear();
        for (int f = 0; f < static_cast<int>(fields.size()); ++f) {
            fields[f] = ipListParseUnquote(fields[f]);
            words.clear();
            ipListParseSplit(fields[f], false, words);
            for (const auto word: words) {
                IpListParseToken token;
                token.text = ipListParseUnquote(word);
                token.field = f;
                if (token.text.isEmpty()) continue;
                if (ipListParseAddressToken(token.text, token.cidrs, token.port)) {
                    token.kind = IpListParseKind::Address;
                } else {
                    token.cidrs.clear();
                    token.kind = ipListParsePort(token.text, token.port) ? IpListParseKind::Port : IpListParseKind::Other;
                }
                tokens.push_back(std::move(token));
            }
        }
        if (tokens.empty()) continue;

        const bool tabular = separated || tabularFile;
        int addresses = 0;
        bool addressShaped = false;
        bool namesColumn = false;
        for (const auto &token: tokens) {
            if (token.kind == IpListParseKind::Address)
                ++addresses;
            else if (ipListParseLooksLikeAddress(token.text))
                addressShaped = true;
            if (ipListParseIsColumnName(token.text)) namesColumn = true;
        }

        if (addresses == 0) {
            if (firstContentLine && !addressShaped && namesColumn) {
                firstContentLine = false;
                tabularFile = true;
                for (int f = 0; f < static_cast<int>(fields.size()); ++f) {
                    if (fields[f].contains(QLatin1String("port"), Qt::CaseInsensitive)) {
                        portColumn = f;
                        break;
                    }
                }
                continue;
            }
            firstContentLine = false;
            if (!tabular) {
                for (const auto &token: tokens) reject(token.text);
            } else if (addressShaped) {
                for (const auto &token: tokens)
                    if (ipListParseLooksLikeAddress(token.text)) reject(token.text);
            } else {
                reject(line);
            }
            continue;
        }
        firstContentLine = false;

        for (size_t i = 0; i < tokens.size(); ++i) {
            auto &token = tokens[i];
            if (token.consumed) continue;
            if (token.kind == IpListParseKind::Address) {
                int port = token.port;
                if (port < 0 && tabularFile) {
                    if (portColumn >= 0 && portColumn < static_cast<int>(fields.size()) && portColumn != token.field) {
                        int columnPort = -1;
                        if (ipListParsePort(fields[portColumn], columnPort)) port = columnPort;
                    }
                } else if (port < 0 && i + 1 < tokens.size() && tokens[i + 1].kind == IpListParseKind::Port) {
                    port = tokens[i + 1].port;
                    tokens[i + 1].consumed = true;
                }
                add(token.cidrs, port);
            } else if (token.kind == IpListParseKind::Port) {
                if (!tabular) reject(token.text);
            } else if (!tabular || ipListParseLooksLikeAddress(token.text)) {
                reject(token.text);
            }
        }
    }
    return result;
}

QString FormatTarget(const QString &address, int port) {
    if (port <= 0) return address;
    if (address.contains(QLatin1Char(':')))
        return QLatin1Char('[') + address + QLatin1String("]:") + QString::number(port);
    return address + QLatin1Char(':') + QString::number(port);
}

QString FormatEntry(const Configs::IpListEntry &entry) {
    if (IsHostCidr(entry.cidr)) {
        const qsizetype slash = entry.cidr.indexOf(QLatin1Char('/'));
        return FormatTarget(slash < 0 ? entry.cidr : entry.cidr.left(slash), entry.port);
    }
    if (entry.port <= 0) return entry.cidr;
    return entry.cidr + QLatin1Char(' ') + QString::number(entry.port);
}

bool IsHostCidr(const QString &cidr) {
    if (cidr.isEmpty()) return false;
    const qsizetype slash = cidr.indexOf(QLatin1Char('/'));
    if (slash < 0) return true;
    const QStringView view(cidr);
    return view.mid(slash + 1) == (view.left(slash).contains(u':') ? QStringView(u"128") : QStringView(u"32"));
}

quint64 AddressCount(const QString &cidr) {
    if (cidr.isEmpty()) return 0;
    const qsizetype slash = cidr.indexOf(QLatin1Char('/'));
    if (slash < 0) return 1;
    const int bits = QStringView(cidr).left(slash).contains(u':') ? 128 : 32;
    quint32 prefix = 0;
    if (!ipListParseDecimal(QStringView(cidr).mid(slash + 1), 3, prefix) || prefix > static_cast<quint32>(bits)) return 0;
    const int hostBits = bits - static_cast<int>(prefix);
    return hostBits >= 63 ? quint64(1) << 63 : quint64(1) << hostBits;
}

QList<Configs::IpListEntry> EntriesFromCidrs(const QStringList &cidrs, int defaultPort) {
    if (defaultPort < 0 || defaultPort > 65535) defaultPort = 0;
    QList<Configs::IpListEntry> entries;
    entries.reserve(cidrs.size());
    QSet<QString> seen;
    seen.reserve(cidrs.size());
    for (const auto &text: cidrs) {
        const QString cidr = NormalizeCidr(text);
        if (cidr.isEmpty() || seen.contains(cidr)) continue;
        seen.insert(cidr);
        entries.append({cidr, defaultPort, 0});
    }
    return entries;
}
} // namespace Scanner
