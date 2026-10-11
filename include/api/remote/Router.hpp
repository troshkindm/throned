#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

#include "include/api/remote/Message.hpp"

class MainWindow;
class QRegularExpression;
class QTimer;

namespace Configs {
class Group;
class Profile;
class RouteProfile;
} // namespace Configs

namespace RemoteApi {
// The endpoints of spec/api.md in throneproj/Throne-api. UI thread only.
class Router : public QObject {
    Q_OBJECT

public:
    explicit Router(MainWindow *mw);

    void dispatch(const Request &request, const Responder &respond);

private:
    // Answered exactly once: by the change it waits for, or with onDeadline() once its timer fires.
    struct Waiter {
        Responder respond;
        std::function<Response()> onDeadline;
        QTimer *deadline = nullptr;
        bool done = false;
    };
    using WaiterPtr = std::shared_ptr<Waiter>;

    struct WaitOptions {
        bool wait = true;
        int timeoutMs = 30000;
    };

    struct Job {
        int id = 0;
        QString kind;
        QString state;
        QList<int> profileIds;
        qint64 startedAt = 0;
        qint64 finishedAt = 0;
        bool cancelRequested = false;
        QList<WaiterPtr> waiters;
    };

    // id is the {id} path parameter, -1 for a path without one.
    using Handler = void (Router::*)(const Request &request, const Responder &respond, int id);

    void getStatus(const Request &request, const Responder &respond, int id);
    void getGroups(const Request &request, const Responder &respond, int id);
    void getProfiles(const Request &request, const Responder &respond, int id);
    void getProfile(const Request &request, const Responder &respond, int id);
    void putConnection(const Request &request, const Responder &respond, int id);
    void deleteConnection(const Request &request, const Responder &respond, int id);
    void putModes(const Request &request, const Responder &respond, int id);
    void getRoutes(const Request &request, const Responder &respond, int id);
    void putActiveRoute(const Request &request, const Responder &respond, int id);
    void postTest(const Request &request, const Responder &respond, int id);
    void getTests(const Request &request, const Responder &respond, int id);
    void getTest(const Request &request, const Responder &respond, int id);
    void deleteTest(const Request &request, const Responder &respond, int id);
    void deleteResults(const Request &request, const Responder &respond, int id);
    void getLiveStats(const Request &request, const Responder &respond, int id);
    void getTrafficStats(const Request &request, const Responder &respond, int id);

    static Response badRequest(const QString &message);
    static Response notFound(const QString &message);
    static Response noProfiles();

    // The readers leave *out alone for a missing or null field, and fail with a bad_request in *error for a mistyped one.
    static bool bodyObject(const Request &request, bool required, QJsonObject *body, Response *error);
    static bool present(const QJsonObject &body, const QString &key);
    static bool readBool(const QJsonObject &body, const QString &key, std::optional<bool> *out, Response *error);
    static bool readNumber(const QJsonObject &body, const QString &key, std::optional<double> *out, Response *error);
    static bool readInteger(const QJsonObject &body, const QString &key, std::optional<qint64> *out, Response *error);
    static bool readId(const QJsonObject &body, const QString &key, std::optional<int> *out, Response *error);
    static bool readString(const QJsonObject &body, const QString &key, std::optional<QString> *out, Response *error);
    static bool readWait(const QJsonObject &body, bool defaultWait, int defaultTimeoutSec, WaitOptions *out, Response *error);
    static bool readNameFilter(const QString &pattern, QRegularExpression *out, Response *error);
    // `profiles`, or `group` with an optional `name`; in order, without repeats.
    bool readSelection(const QJsonObject &body, QList<int> *ids, Response *error) const;

    QList<std::shared_ptr<Configs::Profile>> groupProfiles(int gid, const QRegularExpression &nameFilter) const;
    bool connectionBusy() const;

    QJsonObject statusJson() const;
    QJsonObject profileJson(const std::shared_ptr<Configs::Profile> &profile) const;
    static QJsonObject groupJson(const Configs::Group &group);
    static QJsonObject routeJson(const Configs::RouteProfile &route);
    QJsonObject jobJson(const Job &job, bool withResults) const;
    Response jobResponse(const Job &job) const;
    Response accepted() const;

    WaiterPtr makeWaiter(const Responder &respond, int timeoutMs, std::function<Response()> onDeadline);
    void answer(const WaiterPtr &waiter, const Response &response);
    // Answers the waiter, or logs the error when nobody waits for it any more.
    void fail(const WaiterPtr &waiter, const Response &error);
    void completeStart(const WaiterPtr &waiter, const std::optional<Response> &failure);
    // Runs apply(serial), which returns whether it restarted the profile with that serial, then waits for the restart.
    void applyWithRestart(const WaitOptions &options, const Responder &respond, const std::function<bool(quint64)> &apply);
    // Starts the profile unattended; returns the response instead when no start is needed or possible.
    std::optional<Response> launch(int profileId, bool restart, const WaiterPtr &waiter);
    void startLowestLatency(const QJsonObject &body, bool restart, const WaitOptions &options, const Responder &respond);
    void startBestCandidate(const QList<int> &candidates, qint64 requestedAt, qint64 maxAge, bool restart, const WaiterPtr &waiter);
    void finishJob(int jobId, bool stopped);
    std::shared_ptr<Job> findJob(int jobId) const;

    MainWindow *mw_;
    // Keyed by StartRequest serial; called once, with no failure when the profile started.
    QHash<quint64, std::function<void(const std::optional<Response> &failure)>> starts_;
    QList<std::function<void()>> stops_;
    // Started, waiting for the window to leave the connecting state before the Status is final.
    QList<WaiterPtr> settling_;
    // Newest first.
    QList<std::shared_ptr<Job>> jobs_;
    int lastJobId_ = 0;
};
} // namespace RemoteApi
