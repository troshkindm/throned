#include "include/configs/outbounds/ssh.h"

#include <QJsonArray>
#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
bool ssh::ParseFromLink(const QString& link) {
    auto url = QUrl(link);
    if (!url.isValid()) return false;
    auto query = QUrlQuery(url.query());

    outbound::ParseFromLink(link);

    if (query.hasQueryItem("user")) user = query.queryItemValue("user", QUrl::FullyDecoded);
    if (query.hasQueryItem("password")) password = query.queryItemValue("password", QUrl::FullyDecoded);

    QString privateKeyB64 = query.queryItemValue("private_key");
    if (!privateKeyB64.isEmpty()) {
        private_key = QByteArray::fromBase64(privateKeyB64.toUtf8(), QByteArray::OmitTrailingEquals);
    }
    if (query.hasQueryItem("private_key_path")) private_key_path = query.queryItemValue("private_key_path", QUrl::FullyDecoded);
    if (query.hasQueryItem("private_key_passphrase")) private_key_passphrase = query.queryItemValue("private_key_passphrase", QUrl::FullyDecoded);

    QString hostKeysRaw = query.queryItemValue("host_key");
    if (!hostKeysRaw.isEmpty()) {
        for (const auto& item: hostKeysRaw.split("-")) {
            auto b64hostKey = QByteArray::fromBase64(item.toUtf8(), QByteArray::OmitTrailingEquals);
            if (!b64hostKey.isEmpty()) host_key.append(QString(b64hostKey));
        }
    }

    QString hostKeyAlgsRaw = query.queryItemValue("host_key_algorithms");
    if (!hostKeyAlgsRaw.isEmpty()) {
        for (const auto& item: hostKeyAlgsRaw.split("-")) {
            auto b64hostKeyAlg = QByteArray::fromBase64(item.toUtf8(), QByteArray::OmitTrailingEquals);
            if (!b64hostKeyAlg.isEmpty()) host_key_algorithms.append(QString(b64hostKeyAlg));
        }
    }

    if (query.hasQueryItem("client_version")) client_version = query.queryItemValue("client_version", QUrl::FullyDecoded);

    return !server.isEmpty();
}

bool ssh::ParseFromJson(const QJsonObject& object) {
    if (object.isEmpty() || object["type"].toString() != "ssh") return false;
    outbound::ParseFromJson(object);
    if (object.contains("user")) user = object["user"].toString();
    if (object.contains("password")) password = object["password"].toString();
    if (object.contains("private_key")) private_key = object["private_key"].toString();
    if (object.contains("private_key_path")) private_key_path = object["private_key_path"].toString();
    if (object.contains("private_key_passphrase")) private_key_passphrase = object["private_key_passphrase"].toString();
    if (object.contains("host_key")) {
        host_key = QJsonArray2QListString(object["host_key"].toArray());
    }
    if (object.contains("host_key_algorithms")) {
        host_key_algorithms = QJsonArray2QListString(object["host_key_algorithms"].toArray());
    }
    if (object.contains("client_version")) client_version = object["client_version"].toString();
    return true;
}

bool ssh::ParseFromClash(const clash::Proxies& object) {
    if (object.type != "ssh") return false;
    outbound::ParseFromClash(object);
    user = QString::fromStdString(object.username);
    password = QString::fromStdString(object.password);
    if (!object.private_key.empty()) private_key = QString::fromStdString(object.private_key);
    if (!object.private_key_passphrase.empty()) private_key_passphrase = QString::fromStdString(object.private_key_passphrase);
    for (const auto& s: object.host_key) {
        host_key.append(QString::fromStdString(s));
    }
    for (const auto& s: object.host_key_algorithms) {
        host_key_algorithms.append(QString::fromStdString(s));
    }

    return true;
}

QString ssh::ExportToLink() {
    QUrl url;
    QUrlQuery query;
    url.setScheme("ssh");
    url.setHost(server);
    url.setPort(server_port);
    if (!name.isEmpty()) url.setFragment(name);

    if (!user.isEmpty()) query.addQueryItem("user", user);
    if (!password.isEmpty()) query.addQueryItem("password", password);
    if (!private_key.isEmpty()) {
        query.addQueryItem("private_key", private_key.toUtf8().toBase64(QByteArray::OmitTrailingEquals));
    }
    if (!private_key_path.isEmpty()) query.addQueryItem("private_key_path", private_key_path);
    if (!private_key_passphrase.isEmpty()) query.addQueryItem("private_key_passphrase", private_key_passphrase);

    if (!host_key.isEmpty()) {
        QStringList b64HostKeys;
        for (const auto& item: host_key) {
            b64HostKeys.append(item.toUtf8().toBase64(QByteArray::OmitTrailingEquals));
        }
        query.addQueryItem("host_key", b64HostKeys.join("-"));
    }

    if (!host_key_algorithms.isEmpty()) {
        QStringList b64HostKeyAlgs;
        for (const auto& item: host_key_algorithms) {
            b64HostKeyAlgs.append(item.toUtf8().toBase64(QByteArray::OmitTrailingEquals));
        }
        query.addQueryItem("host_key_algorithms", b64HostKeyAlgs.join("-"));
    }

    if (!client_version.isEmpty()) query.addQueryItem("client_version", client_version);

    mergeUrlQuery(query, outbound::ExportToLink());

    if (!query.isEmpty()) url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QJsonObject ssh::ExportToJson() {
    QJsonObject object;
    object["type"] = "ssh";
    mergeJsonObjects(object, outbound::ExportToJson());
    if (!user.isEmpty()) object["user"] = user;
    if (!password.isEmpty()) object["password"] = password;
    if (!private_key.isEmpty()) object["private_key"] = private_key;
    if (!private_key_path.isEmpty()) object["private_key_path"] = private_key_path;
    if (!private_key_passphrase.isEmpty()) object["private_key_passphrase"] = private_key_passphrase;
    if (!host_key.isEmpty()) object["host_key"] = QListStr2QJsonArray(host_key);
    if (!host_key_algorithms.isEmpty()) object["host_key_algorithms"] = QListStr2QJsonArray(host_key_algorithms);
    if (!client_version.isEmpty()) object["client_version"] = client_version;
    return object;
}

BuildResult ssh::Build() {
    QJsonObject object;
    object["type"] = "ssh";
    mergeJsonObjects(object, outbound::Build().object);
    if (!user.isEmpty()) object["user"] = user;
    if (!password.isEmpty()) object["password"] = password;
    if (!private_key.isEmpty()) object["private_key"] = private_key;
    if (!private_key_path.isEmpty()) object["private_key_path"] = private_key_path;
    if (!private_key_passphrase.isEmpty()) object["private_key_passphrase"] = private_key_passphrase;
    if (!host_key.isEmpty()) object["host_key"] = QListStr2QJsonArray(host_key);
    if (!host_key_algorithms.isEmpty()) object["host_key_algorithms"] = QListStr2QJsonArray(host_key_algorithms);
    if (!client_version.isEmpty()) object["client_version"] = client_version;
    return {object, ""};
}

QString ssh::DisplayType() {
    return "SSH";
}

SecurityInfo ssh::GetSecurity() {
    return {QObject::tr("Encrypted"), {}, SecurityLevel::Secure};
}
} // namespace Configs
