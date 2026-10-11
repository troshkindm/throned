#include "include/configs/common/utils.h"

#include <algorithm>
#include <QHostAddress>
#include <QRegularExpression>

#include "include/global/Configs.hpp"

namespace Configs {
void mergeUrlQuery(QUrlQuery& baseQuery, const QString& strQuery) {
    QUrlQuery query = QUrlQuery(strQuery);
    for (const auto& item: query.queryItems()) {
        baseQuery.addQueryItem(item.first, item.second);
    }
}

QString formDecodedQueryValue(const QUrlQuery& query, const QString& key) {
    // urlencode()-style panels send '+' for a space, which QUrlQuery never decodes but keeps apart from %2B.
    auto raw = query.queryItemValue(key, QUrl::FullyEncoded).toUtf8();
    raw.replace('+', "%20");
    return QUrl::fromPercentEncoding(raw);
}

void mergeJsonObjects(QJsonObject& baseObject, const QJsonObject& obj) {
    for (const auto& key: obj.keys()) {
        baseObject[key] = obj[key];
    }
}

QStringList jsonObjectToQStringList(const QJsonObject& obj) {
    auto result = QStringList();
    for (const auto& key: obj.keys()) {
        result << key << obj[key].toString();
    }
    return result;
}

QJsonObject qStringListToJsonObject(const QStringList& list) {
    auto result = QJsonObject();
    if (list.count() % 2 != 0) {
        qDebug() << "QStringList of odd length in qStringListToJsonObject:" << list;
        return result;
    }
    for (int i = 0; i < list.size(); i += 2) {
        result[list[i]] = list[i + 1];
    }
    return result;
}

// TODO add setting items and use them here
bool useXrayVless(const QString& link) {
    auto url = QUrl(link);
    if (!url.isValid()) return false;
    auto query = QUrlQuery(url.query());
    const auto transport = query.queryItemValue("type");
    const auto security = query.queryItemValue("security");
    // sing-box's http transport speaks the raw HTTP header only in plaintext; TLS (which a bare sni also enables) turns it into h2
    const bool rawHttpOverTls = (transport.isEmpty() || transport == "tcp" || transport == "raw") && query.queryItemValue("headerType") == "http" && ((!security.isEmpty() && security != "none") || !query.queryItemValue("sni").isEmpty() || !query.queryItemValue("peer").isEmpty());

    if (dataManager->settingsRepo->xray_vless_preference == Xray::AllVLESS || rawHttpOverTls || transport == "xhttp" || query.hasQueryItem("fm") || query.hasQueryItem("finalmask")
        // sing-box has no counterpart to verifyPeerCertByName
        || query.hasQueryItem("vcn") || (security == "reality" && dataManager->settingsRepo->xray_vless_preference == Xray::XhttpAndReality) || (query.queryItemValue("encryption") != "none" && query.queryItemValue("encryption") != "") || query.queryItemValue("extra") != "") return true;
    return false;
}

QString toAceHost(const QString& host) {
    // the http transport and Xray's raw header carry a comma list of hosts
    if (host.contains(',')) {
        auto parts = host.split(',');
        for (auto& part: parts) part = toAceHost(part.trimmed());
        return parts.join(',');
    }
    bool ascii = true;
    for (const auto ch: host) {
        if (ch.unicode() > 0x7F) {
            ascii = false;
            break;
        }
    }
    if (ascii) return host;
    // toAce is empty for IP literals and for names it rejects
    const auto ace = QString::fromLatin1(QUrl::toAce(host));
    return ace.isEmpty() ? host : ace;
}

bool IsPrivateHost(const QString& host) {
    auto bare = host.trimmed().toLower();
    bare.remove('[').remove(']');
    if (bare.endsWith('.')) bare.chop(1);
    if (bare.isEmpty()) return false;

    if (QHostAddress ip; ip.setAddress(bare)) {
        // Folds a v4-mapped IPv6 address onto the IPv4 ranges.
        bool isV4 = false;
        if (const auto v4 = ip.toIPv4Address(&isV4); isV4) ip = QHostAddress(v4);
        static const auto ranges = [] {
            QList<QPair<QHostAddress, int>> list;
            for (const auto cidr: {"0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8", "169.254.0.0/16",
                                   "172.16.0.0/12", "192.0.0.0/24", "192.0.2.0/24", "192.88.99.0/24", "192.168.0.0/16",
                                   "198.18.0.0/15", "198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/3",
                                   "::/127", "fc00::/7", "fe80::/10", "ff00::/8"}) {
                list << QHostAddress::parseSubnet(QLatin1String(cidr));
            }
            return list;
        }();
        return std::any_of(ranges.cbegin(), ranges.cend(), [&ip](const auto& range) { return ip.isInSubnet(range); });
    }

    static const QStringList suffixes = {"lan", "localdomain", "example", "invalid", "localhost", "test", "local",
                                         "home.arpa", "internal"};
    for (const auto& suffix: suffixes) {
        if (bare == suffix || bare.endsWith('.' + suffix)) return true;
    }
    // A dotless name only resolves through the local search domain.
    static const QRegularExpression dotless(QStringLiteral("^[a-z]([a-z0-9-]{0,61}[a-z0-9])?$"));
    return dotless.match(bare).hasMatch();
}

QString getHeadersString(const QStringList& headers) {
    QString result;
    if (headers.length() % 2 != 0) {
        return "";
    }
    QStringList formatted;
    formatted.reserve(headers.length() / 2);

    for (int i = 0; i < headers.length(); i += 2) {
        formatted.append(QStringLiteral("%1=\"%2\"").arg(headers.at(i), headers.at(i + 1)));
    }
    return formatted.join(' ');
}

QStringList parseHeaderPairs(const QString& rawHeader) {
    bool inQuote = false;
    QString curr;
    QStringList list;
    for (const auto& ch: rawHeader) {
        if (inQuote) {
            if (ch == '"') {
                inQuote = false;
                list << curr;
                curr = "";
                continue;
            } else {
                curr += ch;
                continue;
            }
        }
        if (ch == '"') {
            inQuote = true;
            continue;
        }
        if (ch == ' ') {
            if (!curr.isEmpty()) {
                list << curr;
                curr = "";
            }
            continue;
        }
        if (ch == '=') {
            if (!curr.isEmpty()) {
                list << curr;
                curr = "";
            }
            continue;
        }
        curr += ch;
    }
    if (!curr.isEmpty()) list << curr;

    if (list.size() % 2 != 0) {
        return {};
    }

    return list;
}
} // namespace Configs
