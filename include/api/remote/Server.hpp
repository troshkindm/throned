#pragma once

#include <QHostAddress>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QString>

#include <functional>

class QThread;

#include "include/api/remote/Message.hpp"
#include "include/api/remote/Protocol.hpp"

namespace RemoteApi {
struct Config {
    bool enabled = false;
    bool lan = false;
    quint16 port = kDefaultPort;
    QByteArray key;
    // Applies only with lan; empty allows every address.
    QList<QPair<QHostAddress, int>> allow;
};

// Reads the remote_api_* settings. UI thread only.
Config ConfigFromSettings();

// The listener and its connections live on a thread of their own; the public methods are for the UI thread.
class Server : public QObject {
    Q_OBJECT

public:
    using Dispatcher = std::function<void(const Request &, const Responder &)>;

    static Server *instance();

    // Each request is handed over on the UI thread. Set once, before the first apply().
    void setDispatcher(Dispatcher dispatcher);

    // Starts, moves or stops the listener. A changed key, address or port closes every open connection.
    void apply(const Config &config);

    // Stops listening, closes every connection and joins the thread.
    void shutdown();

    // "Off", "Listening on 127.0.0.1:9095", or why listening failed. Thread-safe.
    QString statusText() const;

signals:
    void statusChanged(const QString &text);

private:
    class Worker;

    Server();

    ~Server() override;

    // Any thread.
    void publishStatus(const QString &text);

    void dispatch(const Request &request, const Responder &respond);

    QThread *thread_ = nullptr;
    Worker *worker_ = nullptr;
    Dispatcher dispatcher_;
    bool stopped_ = false;
    mutable QMutex statusMutex_;
    QString status_;
};
} // namespace RemoteApi
