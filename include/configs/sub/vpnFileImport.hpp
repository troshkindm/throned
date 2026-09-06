#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace Configs {
class openvpn;
class openconnect;

bool ParseOvpnConfig(const QString &body, openvpn &out);

bool ParseOpenConnectProfile(const QString &text, openconnect &out);

// A fatal problem appends its reason too, then returns false.
bool ParseOvpnConfig(const QString &body, openvpn &out, QStringList *problems);

bool ParseOpenConnectProfile(const QString &text, openconnect &out, QStringList *problems);

// An AnyConnect / Cisco Secure Client XML profile lists one host per entry.
bool ParseAnyConnectXml(const QString &xml, QList<std::shared_ptr<openconnect>> &out, QStringList *problems = nullptr);
} // namespace Configs
