#include "include/api/remote/Router.hpp"

#include "NkrVersion.h"
#include "include/ui/mainwindow.h"
#include "include/ui/mainWindow/TestRunner.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/TrafficStatsRepo.h"
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/sys/KillSwitch.hpp"

namespace RemoteApi::RouterImpl {
constexpr int kDefaultWaitSec = 30;
constexpr int kMaxWaitSec = 300;
constexpr qint64 kDefaultMaxAgeSec = 3600;
constexpr qint64 kTrafficWindowSec = 24 * 3600;
constexpr qint64 kLiveRatesFreshMs = 3000;

bool matchPath(const QString &pattern, const QString &path, int *id) {
    const auto want = pattern.split(QLatin1Char('/'));
    const auto got = path.split(QLatin1Char('/'));
    if (want.size() != got.size()) return false;
    for (qsizetype i = 0; i < want.size(); ++i) {
        if (want[i] != QLatin1String("{id}")) {
            if (want[i] != got[i]) return false;
            continue;
        }
        bool ok = false;
        const int value = got[i].toInt(&ok);
        if (!ok) return false;
        *id = value;
    }
    return true;
}

QList<int> groupOrder() {
    auto order = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    for (const int gid: Configs::dataManager->groupsRepo->GetAllGroupIds()) {
        if (!order.contains(gid)) order.append(gid);
    }
    return order;
}

QString profileDisplayName(const Configs::Profile &profile) {
    return profile.outbound ? profile.outbound->name : profile.name;
}

qint64 bitsPerSecond(const QString &speed) {
    const double bps = Configs::bitrateToBps(speed);
    return bps < 0 ? -1 : qRound64(bps);
}

// A result this request's own test produced counts even under max_age 0.
bool resultCounts(const Configs::Profile &profile, qint64 requestedAt, qint64 maxAge) {
    if (profile.latency_at <= 0) return false;
    return profile.latency_at >= requestedAt || requestedAt - profile.latency_at < maxAge;
}

QString connectionStateName(MainWindow::ConnectionState state) {
    switch (state) {
        case MainWindow::ConnectionState::Connecting:
            return QStringLiteral("connecting");
        case MainWindow::ConnectionState::Running:
            return QStringLiteral("running");
        case MainWindow::ConnectionState::Stopping:
            return QStringLiteral("stopping");
        case MainWindow::ConnectionState::Idle:
            break;
    }
    return QStringLiteral("idle");
}

QString killSwitchName() {
    if (!Configs::dataManager->settingsRepo->kill_switch) return QStringLiteral("off");
    switch (Sys::KillSwitch::instance()->state()) {
        case Sys::KillSwitch::State::Arming:
            return QStringLiteral("arming");
        case Sys::KillSwitch::State::Armed:
            return QStringLiteral("armed");
        case Sys::KillSwitch::State::Failed:
            return QStringLiteral("failed");
        case Sys::KillSwitch::State::Disabled:
            break;
    }
    return QStringLiteral("off");
}

Response busyConnection() {
    return Response::Error(409, QStringLiteral("busy"), QStringLiteral("A profile is starting or stopping"));
}

Response startFailure(MainWindow::StartOutcome outcome, const QString &error) {
    const auto make = [&error](int status, const char *code, const char *fallback) {
        return Response::Error(status, QString::fromLatin1(code), error.isEmpty() ? QString::fromLatin1(fallback) : error);
    };
    switch (outcome) {
        case MainWindow::StartOutcome::Exiting:
            return make(503, "exiting", "Throne is shutting down");
        case MainWindow::StartOutcome::NotFound:
            return make(404, "not_found", "The profile does not exist");
        case MainWindow::StartOutcome::GroupUnavailable:
            return make(409, "group_archived", "The profile's group is archived");
        case MainWindow::StartOutcome::KillSwitchInactive:
            return make(409, "kill_switch_inactive", "The kill switch is on but not working, so Throne does not start profiles");
        case MainWindow::StartOutcome::Superseded:
            return make(409, "superseded", "A newer start replaced this one before it ran");
        case MainWindow::StartOutcome::CoreUnavailable:
            return make(503, "core_unavailable", "Throne's core process is not reachable");
        case MainWindow::StartOutcome::BuildFailed:
            return make(422, "build_failed", "Throne could not build the profile's configuration");
        case MainWindow::StartOutcome::Busy:
            return make(409, "busy", "Another profile is starting or stopping");
        case MainWindow::StartOutcome::ExtraCoreBlocked:
            return make(409, "kill_switch_blocked", "The kill switch could not allow the profile's extra core");
        case MainWindow::StartOutcome::GeoAssetsMissing:
            return make(422, "geo_assets_missing", "The profile needs geoip or geosite files that are missing");
        case MainWindow::StartOutcome::StrictRouteUnavailable:
            return make(422, "strict_route_unavailable", "Windows could not enable strict routing");
        case MainWindow::StartOutcome::TunFailed:
            return make(422, "tun_failed", "The Tun interface could not be configured");
        case MainWindow::StartOutcome::StartFailed:
        case MainWindow::StartOutcome::Started:
            break;
    }
    return make(422, "start_failed", "The core rejected the configuration");
}

QString trafficProfileName(int profileId, const QHash<int, QString> &metaNames) {
    if (profileId == Stats::DIRECT_STAT_PROFILE_ID) return QStringLiteral("Direct");
    if (const auto name = metaNames.value(profileId); !name.isEmpty()) return name;
    if (const auto profile = Configs::dataManager->profilesRepo->GetProfile(profileId)) return profileDisplayName(*profile);
    if (profileId == Configs::warpProfileID) return QStringLiteral("built-in warp");
    return {};
}
} // namespace RemoteApi::RouterImpl

namespace RemoteApi {
Router::Router(MainWindow *mw) : QObject(mw), mw_(mw) {
    connect(mw, &MainWindow::start_finished, this,
            [this](quint64 serial, int, MainWindow::StartOutcome outcome, const QString &error) {
                const auto callback = starts_.take(serial);
                if (!callback) return;
                if (outcome == MainWindow::StartOutcome::Started)
                    callback(std::nullopt);
                else
                    callback(RouterImpl::startFailure(outcome, error));
            });
    connect(mw, &MainWindow::stop_finished, this, [this](int) {
        for (const auto &callback: std::exchange(stops_, {})) callback();
    });
    connect(mw, &MainWindow::connection_state_changed, this, [this](MainWindow::ConnectionState state) {
        if (state == MainWindow::ConnectionState::Connecting) return;
        for (const auto &waiter: std::exchange(settling_, {})) answer(waiter, Response::Ok(statusJson()));
    });
}

void Router::dispatch(const Request &request, const Responder &respond) {
    struct Endpoint {
        const char *method;
        const char *path;
        Handler handler;
    };
    static const Endpoint endpoints[] = {
        {"GET", "/v1/status", &Router::getStatus},
        {"GET", "/v1/groups", &Router::getGroups},
        {"GET", "/v1/profiles", &Router::getProfiles},
        {"GET", "/v1/profiles/{id}", &Router::getProfile},
        {"PUT", "/v1/connection", &Router::putConnection},
        {"DELETE", "/v1/connection", &Router::deleteConnection},
        {"PUT", "/v1/modes", &Router::putModes},
        {"GET", "/v1/routes", &Router::getRoutes},
        {"PUT", "/v1/routes/active", &Router::putActiveRoute},
        {"POST", "/v1/tests", &Router::postTest},
        {"GET", "/v1/tests", &Router::getTests},
        {"GET", "/v1/tests/{id}", &Router::getTest},
        {"DELETE", "/v1/tests/{id}", &Router::deleteTest},
        {"DELETE", "/v1/results", &Router::deleteResults},
        {"GET", "/v1/stats/live", &Router::getLiveStats},
        {"GET", "/v1/stats/traffic", &Router::getTrafficStats},
    };

    bool knownPath = false;
    for (const auto &endpoint: endpoints) {
        int id = -1;
        if (!RouterImpl::matchPath(QString::fromLatin1(endpoint.path), request.path, &id)) continue;
        knownPath = true;
        if (request.method != QLatin1String(endpoint.method)) continue;
        if (request.method != QLatin1String("GET")) {
            MW_show_log(tr("[API] %1 %2 from %3").arg(request.method, request.path, request.peer));
            if (Configs::dataManager->settingsRepo->prepare_exit) {
                respond(Response::Error(503, QStringLiteral("exiting"), QStringLiteral("Throne is shutting down")));
                return;
            }
        }
        (this->*endpoint.handler)(request, respond, id);
        return;
    }
    if (knownPath) {
        respond(Response::Error(405, QStringLiteral("method_not_allowed"),
                                QStringLiteral("%1 is not allowed on %2").arg(request.method, request.path)));
    } else {
        respond(notFound(QStringLiteral("Unknown path %1").arg(request.path)));
    }
}

Response Router::badRequest(const QString &message) {
    return Response::Error(400, QStringLiteral("bad_request"), message);
}

Response Router::notFound(const QString &message) {
    return Response::Error(404, QStringLiteral("not_found"), message);
}

Response Router::noProfiles() {
    return Response::Error(400, QStringLiteral("no_profiles"), QStringLiteral("The selection matched no profile"));
}

bool Router::bodyObject(const Request &request, bool required, QJsonObject *body, Response *error) {
    if (request.body.isObject()) {
        *body = request.body.toObject();
        return true;
    }
    if (!required && (request.body.isNull() || request.body.isUndefined())) return true;
    *error = badRequest(required ? QStringLiteral("The request needs a JSON object body")
                                 : QStringLiteral("The body must be a JSON object"));
    return false;
}

bool Router::present(const QJsonObject &body, const QString &key) {
    const auto value = body.value(key);
    return !value.isUndefined() && !value.isNull();
}

bool Router::readBool(const QJsonObject &body, const QString &key, std::optional<bool> *out, Response *error) {
    if (!present(body, key)) return true;
    const auto value = body.value(key);
    if (!value.isBool()) {
        *error = badRequest(QStringLiteral("\"%1\" must be true or false").arg(key));
        return false;
    }
    *out = value.toBool();
    return true;
}

bool Router::readNumber(const QJsonObject &body, const QString &key, std::optional<double> *out, Response *error) {
    if (!present(body, key)) return true;
    const auto value = body.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        *error = badRequest(QStringLiteral("\"%1\" must be a number").arg(key));
        return false;
    }
    *out = value.toDouble();
    return true;
}

bool Router::readInteger(const QJsonObject &body, const QString &key, std::optional<qint64> *out, Response *error) {
    constexpr double kExactLimit = 9007199254740992.0;
    if (!present(body, key)) return true;
    const auto value = body.value(key);
    const double number = value.toDouble();
    if (!value.isDouble() || std::trunc(number) != number || std::abs(number) > kExactLimit) {
        *error = badRequest(QStringLiteral("\"%1\" must be an integer").arg(key));
        return false;
    }
    *out = static_cast<qint64>(number);
    return true;
}

bool Router::readId(const QJsonObject &body, const QString &key, std::optional<int> *out, Response *error) {
    std::optional<qint64> number;
    if (!readInteger(body, key, &number, error)) return false;
    if (!number) return true;
    if (*number < std::numeric_limits<int>::min() || *number > std::numeric_limits<int>::max()) {
        *error = badRequest(QStringLiteral("\"%1\" is out of range").arg(key));
        return false;
    }
    *out = static_cast<int>(*number);
    return true;
}

bool Router::readString(const QJsonObject &body, const QString &key, std::optional<QString> *out, Response *error) {
    if (!present(body, key)) return true;
    const auto value = body.value(key);
    if (!value.isString()) {
        *error = badRequest(QStringLiteral("\"%1\" must be a string").arg(key));
        return false;
    }
    *out = value.toString();
    return true;
}

bool Router::readWait(const QJsonObject &body, bool defaultWait, int defaultTimeoutSec, WaitOptions *out, Response *error) {
    std::optional<bool> wait;
    std::optional<double> timeout;
    if (!readBool(body, QStringLiteral("wait"), &wait, error) || !readNumber(body, QStringLiteral("timeout"), &timeout, error)) {
        return false;
    }
    if (timeout && (*timeout < 1 || *timeout > RouterImpl::kMaxWaitSec)) {
        *error = badRequest(QStringLiteral("\"timeout\" must be from 1 to %1 seconds").arg(RouterImpl::kMaxWaitSec));
        return false;
    }
    out->wait = wait.value_or(defaultWait);
    out->timeoutMs = static_cast<int>(std::lround(timeout.value_or(defaultTimeoutSec) * 1000));
    return true;
}

bool Router::readNameFilter(const QString &pattern, QRegularExpression *out, Response *error) {
    QRegularExpression filter(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!filter.isValid()) {
        *error = badRequest(QStringLiteral("\"name\" is not a valid regular expression: %1").arg(filter.errorString()));
        return false;
    }
    *out = filter;
    return true;
}

bool Router::readSelection(const QJsonObject &body, QList<int> *ids, Response *error) const {
    const bool byList = present(body, QStringLiteral("profiles"));
    if (byList == present(body, QStringLiteral("group"))) {
        *error = badRequest(QStringLiteral("Select the profiles with either \"profiles\" or \"group\""));
        return false;
    }
    if (byList) {
        const auto list = body.value(QStringLiteral("profiles"));
        if (!list.isArray() || present(body, QStringLiteral("name"))) {
            *error = badRequest(QStringLiteral("\"profiles\" must be an array of profile ids, and \"name\" goes with \"group\""));
            return false;
        }
        QSet<int> seen;
        for (const auto &item: list.toArray()) {
            const double number = item.toDouble();
            if (!item.isDouble() || std::trunc(number) != number || number < 0 || number > std::numeric_limits<int>::max()) {
                *error = badRequest(QStringLiteral("\"profiles\" must be an array of profile ids"));
                return false;
            }
            const int id = static_cast<int>(number);
            if (seen.contains(id)) continue;
            seen.insert(id);
            ids->append(id);
        }
        QSet<int> known;
        for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(*ids)) known.insert(profile->id);
        for (const int id: *ids) {
            if (known.contains(id)) continue;
            *error = notFound(QStringLiteral("Unknown profile %1").arg(id));
            return false;
        }
        return true;
    }

    std::optional<int> gid;
    std::optional<QString> pattern;
    QRegularExpression nameFilter;
    if (!readId(body, QStringLiteral("group"), &gid, error) || !readString(body, QStringLiteral("name"), &pattern, error) ||
        !readNameFilter(pattern.value_or(QString()), &nameFilter, error)) {
        return false;
    }
    if (Configs::dataManager->groupsRepo->GetGroup(*gid) == nullptr) {
        *error = notFound(QStringLiteral("Unknown group %1").arg(*gid));
        return false;
    }
    for (const auto &profile: groupProfiles(*gid, nameFilter)) ids->append(profile->id);
    return true;
}

QList<std::shared_ptr<Configs::Profile>> Router::groupProfiles(int gid, const QRegularExpression &nameFilter) const {
    QList<std::shared_ptr<Configs::Profile>> profiles;
    const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) return profiles;
    QSet<int> seen;
    for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
        if (seen.contains(profile->id)) continue;
        seen.insert(profile->id);
        if (nameFilter.match(RouterImpl::profileDisplayName(*profile)).hasMatch()) profiles.append(profile);
    }
    return profiles;
}

bool Router::connectionBusy() const {
    const auto state = mw_->connection_state();
    return state == MainWindow::ConnectionState::Connecting || state == MainWindow::ConnectionState::Stopping;
}

QJsonObject Router::statusJson() const {
    const auto &settings = Configs::dataManager->settingsRepo;
    const auto running = mw_->running;
    const auto route = Configs::dataManager->routesRepo->GetRouteProfile(settings->current_route_id);
    return {
        {"state", RouterImpl::connectionStateName(mw_->connection_state())},
        {"profile", running != nullptr ? QJsonValue(profileJson(running)) : QJsonValue()},
        {"last_profile_id", mw_->resolve_last_profile()},
        {"tun", settings->spmode_vpn},
        {"system_proxy", settings->spmode_system_proxy},
        {"route", route != nullptr ? QJsonValue(routeJson(*route)) : QJsonValue()},
        {"kill_switch", RouterImpl::killSwitchName()},
        {"core", settings->core_running},
        {"inbound", QJsonObject{
                        {"enabled", !settings->disable_mixed_inbound},
                        {"address", settings->inbound_address},
                        {"port", settings->inbound_socks_port},
                    }},
        {"version", QStringLiteral(NKR_VERSION)},
    };
}

QJsonObject Router::profileJson(const std::shared_ptr<Configs::Profile> &profile) const {
    const auto running = mw_->running;
    int latency = profile->latency;
    if (latency < 0 && latency != Configs::kLatencyConnectOnly) latency = -1;
    return {
        {"id", profile->id},
        {"group", profile->gid},
        {"name", RouterImpl::profileDisplayName(*profile)},
        {"type", profile->type},
        {"address", Configs::DisplayEffectiveAddress(profile)},
        {"latency_ms", latency},
        {"latency_at", profile->latency_at},
        {"download_bps", RouterImpl::bitsPerSecond(profile->dl_speed)},
        {"upload_bps", RouterImpl::bitsPerSecond(profile->ul_speed)},
        {"country", profile->test_country},
        {"ip_out", profile->ip_out},
        {"traffic_up", profile->traffic_uplink},
        {"traffic_down", profile->traffic_downlink},
        {"running", running != nullptr && running->id == profile->id},
    };
}

QJsonObject Router::groupJson(const Configs::Group &group) {
    const bool subscription = !group.url.isEmpty();
    const auto ids = group.Profiles();
    return {
        {"id", group.id},
        {"name", group.name},
        {"subscription", subscription},
        {"archived", group.archive},
        {"profile_count", static_cast<qint64>(QSet<int>(ids.begin(), ids.end()).size())},
        {"updated_at", subscription ? group.sub_last_update : 0},
    };
}

QJsonObject Router::routeJson(const Configs::RouteProfile &route) {
    return {
        {"id", route.id},
        {"name", route.name},
        {"active", route.id == Configs::dataManager->settingsRepo->current_route_id},
    };
}

Response Router::accepted() const {
    return Response::Ok(statusJson(), 202);
}

Router::WaiterPtr Router::makeWaiter(const Responder &respond, int timeoutMs, std::function<Response()> onDeadline) {
    auto waiter = std::make_shared<Waiter>();
    waiter->respond = respond;
    waiter->onDeadline = std::move(onDeadline);
    waiter->deadline = new QTimer(this);
    waiter->deadline->setSingleShot(true);
    connect(waiter->deadline, &QTimer::timeout, this, [this, waiter] {
        if (!waiter->done) answer(waiter, waiter->onDeadline());
    });
    waiter->deadline->start(timeoutMs);
    return waiter;
}

void Router::answer(const WaiterPtr &waiter, const Response &response) {
    if (waiter->done) return;
    waiter->done = true;
    // Possibly from inside the timer's own slot; the waiter that slot holds is released with it.
    waiter->deadline->stop();
    waiter->deadline->deleteLater();
    waiter->deadline = nullptr;
    waiter->onDeadline = nullptr;
    waiter->respond(response);
}

void Router::fail(const WaiterPtr &waiter, const Response &error) {
    if (waiter != nullptr && !waiter->done) {
        answer(waiter, error);
    } else {
        MW_show_log(tr("[API] %1").arg(error.errorMessage));
    }
}

void Router::completeStart(const WaiterPtr &waiter, const std::optional<Response> &failure) {
    if (waiter->done) return;
    if (failure) {
        answer(waiter, *failure);
    } else if (mw_->connection_state() == MainWindow::ConnectionState::Connecting) {
        settling_.append(waiter);
    } else {
        answer(waiter, Response::Ok(statusJson()));
    }
}

void Router::applyWithRestart(const WaitOptions &options, const Responder &respond, const std::function<bool(quint64)> &apply) {
    const quint64 serial = mw_->next_start_serial();
    WaiterPtr waiter;
    if (options.wait) {
        waiter = makeWaiter(respond, options.timeoutMs, [this] { return accepted(); });
        // Before apply(): a start that fails at once reports before apply() returns.
        starts_.insert(serial, [this, waiter](const std::optional<Response> &failure) { completeStart(waiter, failure); });
    }
    if (apply(serial)) {
        if (waiter == nullptr) respond(accepted());
        return;
    }
    starts_.remove(serial);
    if (waiter != nullptr)
        answer(waiter, Response::Ok(statusJson()));
    else
        respond(accepted());
}

std::optional<Response> Router::launch(int profileId, bool restart, const WaiterPtr &waiter) {
    if (connectionBusy()) return RouterImpl::busyConnection();
    const auto running = mw_->running;
    if (!restart && running != nullptr && running->id == profileId) return Response::Ok(statusJson());
    const quint64 serial = mw_->next_start_serial();
    if (waiter != nullptr) {
        starts_.insert(serial, [this, waiter](const std::optional<Response> &failure) { completeStart(waiter, failure); });
    }
    mw_->profile_start(MainWindow::StartRequest{profileId, false, serial});
    return std::nullopt;
}

void Router::getStatus(const Request &, const Responder &respond, int) {
    respond(Response::Ok(statusJson()));
}

void Router::getGroups(const Request &, const Responder &respond, int) {
    QJsonArray groups;
    for (const int gid: RouterImpl::groupOrder()) {
        if (const auto group = Configs::dataManager->groupsRepo->GetGroup(gid)) groups.append(groupJson(*group));
    }
    respond(Response::Ok(QJsonObject{{"groups", groups}}));
}

void Router::getProfiles(const Request &request, const Responder &respond, int) {
    Response error;
    QRegularExpression nameFilter;
    if (!readNameFilter(request.query.value(QStringLiteral("name")), &nameFilter, &error)) {
        respond(error);
        return;
    }
    std::optional<int> gid;
    if (request.query.contains(QStringLiteral("group"))) {
        bool ok = false;
        gid = request.query.value(QStringLiteral("group")).toInt(&ok);
        if (!ok) {
            respond(badRequest(QStringLiteral("\"group\" must be a group id")));
            return;
        }
        if (Configs::dataManager->groupsRepo->GetGroup(*gid) == nullptr) {
            respond(notFound(QStringLiteral("Unknown group %1").arg(*gid)));
            return;
        }
    }
    std::optional<QSet<int>> wanted;
    QList<int> wantedOrder;
    if (request.query.contains(QStringLiteral("ids"))) {
        wanted.emplace();
        for (const auto &part: request.query.value(QStringLiteral("ids")).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const int id = part.trimmed().toInt(&ok);
            if (!ok) {
                respond(badRequest(QStringLiteral("\"ids\" must be comma-separated profile ids")));
                return;
            }
            if (wanted->contains(id)) continue;
            wanted->insert(id);
            wantedOrder.append(id);
        }
    }

    QList<std::shared_ptr<Configs::Profile>> profiles;
    if (gid) {
        profiles = groupProfiles(*gid, nameFilter);
        if (wanted) profiles.removeIf([&wanted](const auto &profile) { return !wanted->contains(profile->id); });
    } else if (wanted) {
        for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(wantedOrder)) {
            if (nameFilter.match(RouterImpl::profileDisplayName(*profile)).hasMatch()) profiles.append(profile);
        }
    } else {
        QSet<int> seen;
        for (const int groupId: RouterImpl::groupOrder()) {
            for (const auto &profile: groupProfiles(groupId, nameFilter)) {
                if (seen.contains(profile->id)) continue;
                seen.insert(profile->id);
                profiles.append(profile);
            }
        }
    }

    QJsonArray list;
    for (const auto &profile: profiles) list.append(profileJson(profile));
    respond(Response::Ok(QJsonObject{{"profiles", list}}));
}

void Router::getProfile(const Request &, const Responder &respond, int id) {
    const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
    if (profile == nullptr) {
        respond(notFound(QStringLiteral("Unknown profile %1").arg(id)));
        return;
    }
    respond(Response::Ok(profileJson(profile)));
}

void Router::putConnection(const Request &request, const Responder &respond, int) {
    QJsonObject body;
    Response error;
    std::optional<int> profileId;
    std::optional<QString> select;
    std::optional<bool> restart;
    WaitOptions options;
    if (!bodyObject(request, true, &body, &error) || !readId(body, QStringLiteral("profile"), &profileId, &error) ||
        !readString(body, QStringLiteral("select"), &select, &error) || !readBool(body, QStringLiteral("restart"), &restart, &error) ||
        !readWait(body, true, RouterImpl::kDefaultWaitSec, &options, &error)) {
        respond(error);
        return;
    }
    if (profileId.has_value() == select.has_value()) {
        respond(badRequest(QStringLiteral("Name the profile with either \"profile\" or \"select\"")));
        return;
    }
    if (select == QStringLiteral("lowest_latency")) {
        startLowestLatency(body, restart.value_or(false), options, respond);
        return;
    }

    int id = -1;
    if (profileId) {
        if (Configs::dataManager->profilesRepo->GetProfile(*profileId) == nullptr) {
            respond(notFound(QStringLiteral("Unknown profile %1").arg(*profileId)));
            return;
        }
        id = *profileId;
    } else if (select == QStringLiteral("last")) {
        id = mw_->resolve_last_profile();
        if (id < 0) {
            respond(notFound(QStringLiteral("Throne has no profile to resume")));
            return;
        }
    } else {
        respond(badRequest(QStringLiteral("Unknown \"select\": %1").arg(*select)));
        return;
    }

    const WaiterPtr waiter = options.wait ? makeWaiter(respond, options.timeoutMs, [this] { return accepted(); }) : nullptr;
    if (const auto now = launch(id, restart.value_or(false), waiter)) {
        if (waiter != nullptr)
            answer(waiter, *now);
        else
            respond(*now);
        return;
    }
    if (waiter == nullptr) respond(accepted());
}

void Router::startLowestLatency(const QJsonObject &body, bool restart, const WaitOptions &options, const Responder &respond) {
    std::optional<int> gid;
    std::optional<QString> pattern;
    std::optional<qint64> maxAge;
    QRegularExpression nameFilter;
    Response error;
    if (!readId(body, QStringLiteral("group"), &gid, &error) || !readString(body, QStringLiteral("name"), &pattern, &error) ||
        !readNameFilter(pattern.value_or(QString()), &nameFilter, &error) ||
        !readInteger(body, QStringLiteral("max_age"), &maxAge, &error)) {
        respond(error);
        return;
    }
    if (maxAge.value_or(0) < 0) {
        respond(badRequest(QStringLiteral("\"max_age\" must not be negative")));
        return;
    }
    const int groupId = gid.value_or(Configs::dataManager->settingsRepo->current_group);
    const auto group = Configs::dataManager->groupsRepo->GetGroup(groupId);
    if (group == nullptr) {
        respond(notFound(QStringLiteral("Unknown group %1").arg(groupId)));
        return;
    }
    if (group->archive) {
        respond(Response::Error(409, QStringLiteral("group_archived"), QStringLiteral("The group is archived")));
        return;
    }

    const qint64 requestedAt = QDateTime::currentSecsSinceEpoch();
    const qint64 age = maxAge.value_or(RouterImpl::kDefaultMaxAgeSec);
    QList<int> candidates;
    QList<int> untested;
    for (const auto &profile: groupProfiles(groupId, nameFilter)) {
        if (profile->type == QStringLiteral("autoselector")) continue;
        candidates.append(profile->id);
        if (!RouterImpl::resultCounts(*profile, requestedAt, age)) untested.append(profile->id);
    }
    if (candidates.isEmpty()) {
        respond(noProfiles());
        return;
    }
    if (connectionBusy()) {
        respond(RouterImpl::busyConnection());
        return;
    }

    const WaiterPtr waiter = options.wait ? makeWaiter(respond, options.timeoutMs, [this] { return accepted(); }) : nullptr;
    const auto pick = [this, candidates, requestedAt, age, restart, waiter] {
        startBestCandidate(candidates, requestedAt, age, restart, waiter);
    };
    if (untested.isEmpty()) {
        pick();
    } else {
        MW_show_log(tr("[API] Testing %n profile(s) to find the lowest latency", nullptr, static_cast<int>(untested.size())));
        // The callback comes on the runner's worker thread.
        mw_->testRunner->queueUrlTests(untested, [this, pick] { QMetaObject::invokeMethod(this, pick, Qt::QueuedConnection); });
    }
    if (waiter == nullptr) respond(accepted());
}

void Router::startBestCandidate(const QList<int> &candidates, qint64 requestedAt, qint64 maxAge, bool restart, const WaiterPtr &waiter) {
    std::shared_ptr<Configs::Profile> best;
    for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(candidates)) {
        if (profile->latency <= 0 || !RouterImpl::resultCounts(*profile, requestedAt, maxAge)) continue;
        if (best == nullptr || profile->latency < best->latency) best = profile;
    }
    if (best == nullptr) {
        fail(waiter, Response::Error(409, QStringLiteral("no_working_profile"), QStringLiteral("No candidate passed the latency test")));
        return;
    }
    MW_show_log(tr("[API] Lowest latency: %1 (%2 ms)").arg(RouterImpl::profileDisplayName(*best)).arg(best->latency));
    const auto now = launch(best->id, restart, waiter);
    if (!now) return;
    if (now->status >= 400)
        fail(waiter, *now);
    else if (waiter != nullptr && !waiter->done)
        answer(waiter, *now);
}

void Router::deleteConnection(const Request &request, const Responder &respond, int) {
    QJsonObject body;
    Response error;
    WaitOptions options;
    if (!bodyObject(request, false, &body, &error) || !readWait(body, true, RouterImpl::kDefaultWaitSec, &options, &error)) {
        respond(error);
        return;
    }
    switch (mw_->connection_state()) {
        case MainWindow::ConnectionState::Idle:
            respond(Response::Ok(statusJson()));
            return;
        case MainWindow::ConnectionState::Connecting:
        case MainWindow::ConnectionState::Stopping:
            respond(RouterImpl::busyConnection());
            return;
        case MainWindow::ConnectionState::Running:
            break;
    }
    if (options.wait) {
        const auto waiter = makeWaiter(respond, options.timeoutMs, [this] { return accepted(); });
        stops_.append([this, waiter] { answer(waiter, Response::Ok(statusJson())); });
    }
    mw_->profile_stop(false, false, true, false);
    if (!options.wait) respond(accepted());
}

void Router::putModes(const Request &request, const Responder &respond, int) {
    QJsonObject body;
    Response error;
    std::optional<bool> tun;
    std::optional<bool> systemProxy;
    WaitOptions options;
    if (!bodyObject(request, false, &body, &error) || !readBool(body, QStringLiteral("tun"), &tun, &error) ||
        !readBool(body, QStringLiteral("system_proxy"), &systemProxy, &error) ||
        !readWait(body, true, RouterImpl::kDefaultWaitSec, &options, &error)) {
        respond(error);
        return;
    }
    const auto &settings = Configs::dataManager->settingsRepo;
    const bool proxyChanges = systemProxy.has_value() && *systemProxy != settings->spmode_system_proxy;
    const bool tunChanges = tun.has_value() && *tun != settings->spmode_vpn;
    if (!proxyChanges && !tunChanges) {
        respond(Response::Ok(statusJson()));
        return;
    }
    if (proxyChanges && *systemProxy && settings->disable_mixed_inbound) {
        respond(Response::Error(409, QStringLiteral("mixed_inbound_disabled"),
                                QStringLiteral("The system proxy needs Throne's mixed inbound, which is turned off")));
        return;
    }
    if (tunChanges && *tun && !Configs::IsAdmin() && !settings->disable_privilege_req) {
        respond(Response::Error(412, QStringLiteral("needs_privileges"),
                                QStringLiteral("Tun needs privileges that Throne does not have; grant them in Throne once")));
        return;
    }
    if (connectionBusy()) {
        respond(RouterImpl::busyConnection());
        return;
    }
    // System proxy first so that a Tun restart picks it up; at most one of the two restarts.
    applyWithRestart(options, respond, [&](quint64 serial) {
        bool restarted = false;
        if (proxyChanges) {
            restarted = mw_->set_spmode_system_proxy(
                *systemProxy, MainWindow::ModeChange{.interactive = false, .restart = !tunChanges, .restartSerial = serial});
        }
        if (tunChanges) {
            restarted = mw_->set_spmode_vpn(*tun, MainWindow::ModeChange{.interactive = false, .restartSerial = serial}) || restarted;
        }
        return restarted;
    });
}

void Router::getRoutes(const Request &, const Responder &respond, int) {
    QJsonArray routes;
    for (const auto &route: Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
        if (route != nullptr) routes.append(routeJson(*route));
    }
    respond(Response::Ok(QJsonObject{{"routes", routes}}));
}

void Router::putActiveRoute(const Request &request, const Responder &respond, int) {
    QJsonObject body;
    Response error;
    std::optional<int> routeId;
    WaitOptions options;
    if (!bodyObject(request, true, &body, &error) || !readId(body, QStringLiteral("id"), &routeId, &error) ||
        !readWait(body, true, RouterImpl::kDefaultWaitSec, &options, &error)) {
        respond(error);
        return;
    }
    if (!routeId) {
        respond(badRequest(QStringLiteral("\"id\" is required")));
        return;
    }
    if (Configs::dataManager->routesRepo->GetRouteProfile(*routeId) == nullptr) {
        respond(notFound(QStringLiteral("Unknown routing profile %1").arg(*routeId)));
        return;
    }
    if (Configs::dataManager->settingsRepo->current_route_id == *routeId) {
        respond(Response::Ok(statusJson()));
        return;
    }
    if (connectionBusy()) {
        respond(RouterImpl::busyConnection());
        return;
    }
    applyWithRestart(options, respond, [&](quint64 serial) { return mw_->choose_route(*routeId, false, serial); });
}

void Router::getLiveStats(const Request &, const Responder &respond, int) {
    MainWindow::LiveRates rates;
    if (mw_->running != nullptr && !Configs::dataManager->settingsRepo->disable_traffic_stats && mw_->m_liveRatesAt.isValid() &&
        !mw_->m_liveRatesAt.hasExpired(RouterImpl::kLiveRatesFreshMs)) {
        rates = mw_->m_liveRates;
    }
    respond(Response::Ok(QJsonObject{
        {"proxy", QJsonObject{{"up", rates.proxyUp}, {"down", rates.proxyDown}}},
        {"direct", QJsonObject{{"up", rates.directUp}, {"down", rates.directDown}}},
    }));
}

void Router::getTrafficStats(const Request &request, const Responder &respond, int) {
    const auto by = request.query.value(QStringLiteral("by"), QStringLiteral("profile"));
    if (by != QStringLiteral("profile") && by != QStringLiteral("app")) {
        respond(badRequest(QStringLiteral("\"by\" must be profile or app")));
        return;
    }
    bool toOk = true;
    bool fromOk = true;
    const qint64 to = request.query.contains(QStringLiteral("to")) ? request.query.value(QStringLiteral("to")).toLongLong(&toOk)
                                                                   : QDateTime::currentSecsSinceEpoch();
    const qint64 from = request.query.contains(QStringLiteral("from")) ? request.query.value(QStringLiteral("from")).toLongLong(&fromOk)
                                                                       : to - RouterImpl::kTrafficWindowSec;
    if (!toOk || !fromOk || from > to) {
        respond(badRequest(QStringLiteral("\"from\" and \"to\" must be Unix seconds, \"from\" not after \"to\"")));
        return;
    }
    auto *repo = Configs::dataManager->trafficStatsRepo.get();
    if (Configs::dataManager->settingsRepo->disable_traffic_aggregation || repo == nullptr || repo->Disabled()) {
        respond(Response::Error(409, QStringLiteral("stats_disabled"), QStringLiteral("Traffic statistics are turned off in Throne")));
        return;
    }
    // The current minute stays in memory until flushed.
    Stats::trafficStatsManager->Flush();

    QJsonArray items;
    if (by == QStringLiteral("app")) {
        auto usage = repo->QueryAppUsage(from, to);
        std::stable_sort(usage.begin(), usage.end(), [](const Configs::AppUsage &a, const Configs::AppUsage &b) {
            return a.up + a.down > b.up + b.down;
        });
        for (const auto &item: usage) {
            items.append(QJsonObject{{"name", item.process_name}, {"up", item.up}, {"down", item.down}});
        }
    } else {
        auto usage = repo->QueryConfigUsage(from, to);
        std::stable_sort(usage.begin(), usage.end(), [](const Configs::ConfigUsage &a, const Configs::ConfigUsage &b) {
            return a.up + a.down > b.up + b.down;
        });
        QHash<int, QString> names;
        for (const auto &meta: repo->GetAllConfigMeta()) names.insert(meta.profile_id, meta.name);
        for (const auto &item: usage) {
            items.append(QJsonObject{
                {"id", item.profile_id},
                {"name", RouterImpl::trafficProfileName(item.profile_id, names)},
                {"up", item.up},
                {"down", item.down},
            });
        }
    }
    respond(Response::Ok(QJsonObject{{"by", by}, {"from", from}, {"to", to}, {"items", items}}));
}
} // namespace RemoteApi
