#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "include/database/entities/IpList.h"

namespace Scanner {
struct ParseResult {
    QList<Configs::IpListEntry> entries;
    int rejected = 0;
    int duplicates = 0;
    QStringList samples;
};

// Lines, CSV or whitespace separated: ip, cidr, ip:port, [v6]:port, "cidr port", a-b ranges; '#', "//" and ';' comments.
ParseResult ParseIpListText(const QByteArray &text, int defaultPort = 0);

// Empty when invalid; IPv4 needs all four octets, IPv6 a ':'.
QString NormalizeCidr(const QString &text);

// Must round-trip through ParseIpListText.
QString FormatEntry(const Configs::IpListEntry &entry);

QString FormatTarget(const QString &address, int port);

[[nodiscard]] bool IsHostCidr(const QString &cidr);

// Saturates at 2^63.
quint64 AddressCount(const QString &cidr);

QList<Configs::IpListEntry> EntriesFromCidrs(const QStringList &cidrs, int defaultPort);
} // namespace Scanner
