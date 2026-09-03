#pragma once

#include <QObject>
#include <functional>

namespace Configs_network {
    using DownloadProgressCallback = std::function<void(qint64 received, qint64 total)>;

    struct HTTPResponse {
        QString error;
        QByteArray data;
        QList<QPair<QByteArray, QByteArray>> header;
    };

    struct DownloadProgressReport
    {
        QString fileName;
        qint64 downloadedSize;
        qint64 totalSize;
    };

    class NetworkRequestHelper : QObject {
        Q_OBJECT

        explicit NetworkRequestHelper(QObject *parent) : QObject(parent){};

        ~NetworkRequestHelper() override = default;
        ;

    public:
        // maxBytes > 0 aborts the transfer once the body exceeds it and reports an error.
        static HTTPResponse HttpGet(const QString &url, bool sendHwid = false, bool useProxy = false, qint64 maxBytes = 0);

        static QString GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name);

        static QString DownloadAsset(const QString &url, const QString &fileName, bool useProxy = false,
                                     const DownloadProgressCallback &progress = {});
    };
} // namespace Configs_network

using namespace Configs_network;
