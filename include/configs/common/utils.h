#pragma once
#include <QJsonObject>
#include <QUrlQuery>

namespace Configs {
void mergeUrlQuery(QUrlQuery& baseQuery, const QString& strQuery);

QString formDecodedQueryValue(const QUrlQuery& query, const QString& key);

void mergeJsonObjects(QJsonObject& baseObject, const QJsonObject& obj);

QStringList jsonObjectToQStringList(const QJsonObject& obj);

QJsonObject qStringListToJsonObject(const QStringList& list);

bool useXrayVless(const QString& link);

QString toAceHost(const QString& host);

// Xray's own private list (common/geodata/consts.go): loopback, LAN, CGNAT and reserved ranges, local-only names.
bool IsPrivateHost(const QString& host);

QString getHeadersString(const QStringList& headers);

QStringList parseHeaderPairs(const QString& rawHeader);
} // namespace Configs
