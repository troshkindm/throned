#include "include/api/remote/Message.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace RemoteApi {
Response Response::Ok(const QJsonValue &body, int status) {
    Response response;
    response.status = status;
    response.body = body;
    return response;
}

Response Response::Error(int status, const QString &code, const QString &message) {
    Response response;
    response.status = status;
    response.errorCode = code;
    response.errorMessage = message;
    return response;
}

bool ParseRequest(const QByteArray &json, Request *request, QString *error) {
    const auto fail = [error](const QString &message) {
        if (error != nullptr) *error = message;
        return false;
    };

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(QCoreApplication::translate("RemoteApi", "The request is not valid JSON: %1").arg(parseError.errorString()));
    }
    if (!document.isObject()) return fail(QCoreApplication::translate("RemoteApi", "The request is not a JSON object"));
    const auto object = document.object();

    const auto method = object.value(QLatin1String("method")).toString();
    if (method != QLatin1String("GET") && method != QLatin1String("POST") && method != QLatin1String("PUT") && method != QLatin1String("DELETE")) {
        return fail(QCoreApplication::translate("RemoteApi", "The method must be GET, POST, PUT or DELETE"));
    }

    const auto pathValue = object.value(QLatin1String("path"));
    const auto path = pathValue.toString();
    if (!pathValue.isString() || !path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('?')) || path.contains(QLatin1Char('#'))) {
        return fail(QCoreApplication::translate("RemoteApi", "The path must start with / and carry no query string or fragment"));
    }

    QHash<QString, QString> query;
    if (object.contains(QLatin1String("query"))) {
        const auto queryValue = object.value(QLatin1String("query"));
        if (!queryValue.isObject()) return fail(QCoreApplication::translate("RemoteApi", "The query must be an object"));
        const auto queryObject = queryValue.toObject();
        for (auto it = queryObject.constBegin(); it != queryObject.constEnd(); ++it) {
            if (!it.value().isString()) return fail(QCoreApplication::translate("RemoteApi", "Query value %1 is not a string").arg(it.key()));
            query.insert(it.key(), it.value().toString());
        }
    }

    request->method = method;
    request->path = path;
    request->query = query;
    const auto body = object.value(QLatin1String("body"));
    request->body = body.isUndefined() ? QJsonValue() : body;
    return true;
}

// Written by hand so that status comes first: QJsonObject sorts its keys.
QByteArray SerializeResponse(const Response &response) {
    QByteArray out = "{\"status\":" + QByteArray::number(response.status);
    if (response.status < 400) {
        // QJsonDocument cannot hold a scalar, so the body goes through a one-element array.
        const auto wrapped = QJsonDocument(QJsonArray{response.body.isUndefined() ? QJsonValue() : response.body}).toJson(QJsonDocument::Compact);
        out.reserve(out.size() + wrapped.size() + 8);
        out += ",\"body\":";
        out.append(wrapped.constData() + 1, wrapped.size() - 2);
    } else {
        const QJsonObject error{{QLatin1String("code"), response.errorCode}, {QLatin1String("message"), response.errorMessage}};
        out += ",\"error\":" + QJsonDocument(error).toJson(QJsonDocument::Compact);
    }
    out += '}';
    return out;
}
} // namespace RemoteApi
