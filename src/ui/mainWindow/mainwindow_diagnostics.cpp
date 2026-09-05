#include "include/ui/mainwindow.h"
#include "include/ui/stats/diagnostics_window.h"
#include "include/api/RPC.h"
#include "include/global/Configs.hpp"
#include "include/database/DatabaseManager.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/TrafficStatsRepo.h"
#include "include/database/entities/Profile.h"
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/configs/generate.h"
#include <QFileInfo>
#include <QHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QThread>

namespace {
// Completion is delivered on the receiver's GUI thread and disconnected if the
// window closes. A running RPC never dereferences a QWidget.
template<class Work, class Done>
void background(QObject *receiver, Work work, Done done) {
    using Result = decltype(work());
    auto result = std::make_shared<Result>();
    auto *thread = QThread::create([result, work] { *result = work(); });
    QObject::connect(thread, &QThread::finished, receiver, [result, done] { done(*result); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
}

DiagnosticsWindow::LocalState MainWindow::diagnosticsLocalState() {
    DiagnosticsWindow::LocalState state;
    const auto &settings = *Configs::dataManager->settingsRepo;
    state.coreRunning = running != nullptr;
    // The core prints its own version into the log on start; nothing else reports it.
    static const QRegularExpression coreLine(QStringLiteral(R"(sing-box:\s*(\S+))"));
    const auto match = coreLine.match(ui->masterLogBrowser->toPlainText());
    state.coreVersion = match.hasMatch() ? QStringLiteral("sing-box %1").arg(match.captured(1))
                                         : QStringLiteral("sing-box");
    state.coreUptimeMs = coreStartedAt > 0 ? QDateTime::currentMSecsSinceEpoch() - coreStartedAt : -1;
    if (running != nullptr) {
        state.profileName = running->name;
        if (running->outbound != nullptr) state.profileType = running->outbound->DisplayType();
        state.profileLatencyMs = running->latency > 0 ? running->latency : -1;
    }
    state.tun = settings.spmode_vpn;
    state.systemProxy = settings.spmode_system_proxy;
    state.environmentReport = collectDiagnostics();
    return state;
}

// Reads the traffic history the application already collects and hands the window a
// finished picture, so the window itself never touches the database.
DiagnosticsWindow::Usage MainWindow::diagnosticsUsage(int rangeDays) {
    DiagnosticsWindow::Usage usage;
    auto *repo = Configs::dataManager ? Configs::dataManager->trafficStatsRepo.get() : nullptr;
    if (repo == nullptr) { usage.available = false; return usage; }
    usage.recording = !Configs::dataManager->settingsRepo->disable_traffic_stats
        && !Configs::dataManager->settingsRepo->disable_traffic_aggregation;
    // The minute in progress is still in memory; without this the last bar is empty.
    if (Stats::trafficStatsManager) Stats::trafficStatsManager->Flush();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 retention = qMax(1, Configs::dataManager->settingsRepo->traffic_stats_retention_days);
    const qint64 days = rangeDays > 0 ? rangeDays : retention;
    const qint64 from = now - days * 86400LL;
    usage.bucketSecs = days <= 1 ? 3600LL : 86400LL;
    usage.daysStored = days;
    const qint64 tzOffset = QDateTime::currentDateTime().offsetFromUtc();

    QHash<QString, QString> appPaths;
    for (const auto &meta : repo->GetAllAppMeta()) appPaths.insert(meta.process_name, meta.last_path);
    for (const auto &row : repo->QueryAppUsage(from, now)) {
        const auto name = row.process_name.isEmpty() ? DiagnosticsWindow::tr("Unknown") : row.process_name;
        usage.apps.append({name, appPaths.value(row.process_name), row.up, row.down});
    }

    QHash<int, Configs::ConfigMetaRow> configMeta;
    for (const auto &meta : repo->GetAllConfigMeta()) configMeta.insert(meta.profile_id, meta);
    for (const auto &row : repo->QueryConfigUsage(from, now)) {
        QString name, group;
        if (const auto it = configMeta.constFind(row.profile_id); it != configMeta.constEnd()) {
            name = it->name;
            group = it->group_name;
        }
        if (name.isEmpty()) {
            if (row.profile_id == Stats::DIRECT_STAT_PROFILE_ID) name = DiagnosticsWindow::tr("Direct");
            else if (const auto profile = Configs::dataManager->profilesRepo->GetProfile(row.profile_id)) name = profile->name;
            else if (row.profile_id == Configs::warpProfileID) name = QStringLiteral("built-in warp");
            else name = DiagnosticsWindow::tr("Profile #%1 (deleted)").arg(row.profile_id);
        }
        usage.servers.append({name, group, row.up, row.down});
    }
    auto byTotal = [](const DiagnosticsWindow::UsageRow &a, const DiagnosticsWindow::UsageRow &b) {
        return (a.up + a.down) > (b.up + b.down);
    };
    std::sort(usage.apps.begin(), usage.apps.end(), byTotal);
    std::sort(usage.servers.begin(), usage.servers.end(), byTotal);
    // Beyond a handful of rows the list stops being readable; the rest is one entry.
    auto collapse = [](QList<DiagnosticsWindow::UsageRow> &rows, const QString &label) {
        constexpr int keep = 6;
        if (rows.size() <= keep) return;
        DiagnosticsWindow::UsageRow other{label, {}, 0, 0};
        for (int i = keep; i < rows.size(); ++i) { other.up += rows[i].up; other.down += rows[i].down; }
        other.detail = DiagnosticsWindow::tr("%n more", "", rows.size() - keep);
        rows = rows.mid(0, keep) << other;
    };
    collapse(usage.apps, DiagnosticsWindow::tr("The rest"));
    collapse(usage.servers, DiagnosticsWindow::tr("The rest"));

    const auto series = repo->QueryAppSeries(from, now, usage.bucketSecs, tzOffset);
    QHash<qint64, Configs::TrafficSeriesPoint> byBucket;
    for (const auto &point : series) byBucket.insert(point.bucket_start, point);
    // Aligned with the same offset the query used, or a bar's key never matches a point.
    const qint64 alignedFrom = ((from + tzOffset) / usage.bucketSecs) * usage.bucketSecs - tzOffset;
    for (qint64 bucket = alignedFrom; bucket < now; bucket += usage.bucketSecs) {
        const auto point = byBucket.value(bucket);
        usage.series.append({bucket, point.up, point.down,
            usage.bucketSecs >= 86400LL ? QDateTime::fromSecsSinceEpoch(bucket).toString(QStringLiteral("dd.MM"))
                                        : QDateTime::fromSecsSinceEpoch(bucket).toString(QStringLiteral("HH:mm"))});
    }
    // The stats database sits beside the main one; its size is what the user is consenting to.
    usage.databaseBytes = QFileInfo(Configs::dataManager->StatsDatabasePath()).size();
    return usage;
}

// The verdict's actions leave the window rather than changing anything themselves:
// a diagnostic that silently edits the configuration stops being a source of truth.
void MainWindow::openDiagnosticsTarget(const QString &target) {
    if (target == QLatin1String("rules")) { on_menu_routing_settings_triggered(); return; }
    if (target == QLatin1String("log")) { openLogSettings(); return; }
    if (target == QLatin1String("reachability")) { runSiteReachability(get_now_selected_list()); return; }
    if (target == QLatin1String("profile") || target == QLatin1String("interception")) {
        show(); raise(); activateWindow();
        return;
    }
    // DNS and the DPI bypass both live in the settings dialog.
    on_menu_basic_settings_triggered();
}

void MainWindow::openDiagnostics(const QString &processKey) {
    if (!diagnosticsWindow) {
        diagnosticsWindow = new DiagnosticsWindow(this);
        diagnosticsWindow->setAttribute(Qt::WA_DeleteOnClose);
        auto *window = diagnosticsWindow.data();
        connect(window, &DiagnosticsWindow::healthRequested, window, [window] {
            background(window, [] {
                bool ok = false;
                QString error;
                libcore::HealthResponse result;
                libcore::HealthRequest request;
                if (API::defaultClient) result = API::defaultClient->Health(&ok, request, &error);
                if (!ok && error.isEmpty()) error = tr("The core is unavailable. Start a connection and try again.");
                return std::make_pair(result, ok ? QString() : error);
            }, [window](const auto &result) { window->applyHealth(result.first, result.second); });
        });
        connect(window, &DiagnosticsWindow::routeRequested, window, [window](const libcore::PreviewRouteRequest &request) {
            background(window, [request] {
                bool ok = false;
                QString error;
                libcore::PreviewRouteResponse result;
                if (API::defaultClient) result = API::defaultClient->PreviewRoute(&ok, request, &error);
                if (!ok && error.isEmpty()) error = tr("The core is unavailable. Start a connection and try again.");
                return std::make_pair(result, ok ? QString() : error);
            }, [window](const auto &result) { window->applyRoutePreview(result.first, result.second); });
        });
        connect(window, &DiagnosticsWindow::siteRequested, window, [window](const libcore::DiagnoseSiteRequest &request) {
            background(window, [request] {
                bool ok = false;
                QString error;
                libcore::DiagnoseSiteResponse result;
                if (API::defaultClient) result = API::defaultClient->DiagnoseSite(&ok, request, &error);
                if (!ok && error.isEmpty()) error = tr("The core is unavailable. Start a connection and try again.");
                return std::make_pair(result, error);
            }, [window](const auto &result) { window->applySiteResult(result.first, result.second); });
        });
        connect(window, &DiagnosticsWindow::matrixRequested, window, [window](const QString &url, const QStringList &outbounds) {
            for (const auto &tag : outbounds) {
                background(window, [url, tag] {
                    bool ok = false;
                    QString error;
                    libcore::DiagnoseSiteRequest request;
                    request.url = url.toStdString();
                    request.outbound_tag = tag.toStdString();
                    libcore::DiagnoseSiteResponse result;
                    if (API::defaultClient) result = API::defaultClient->DiagnoseSite(&ok, request, &error);
                    return std::make_tuple(tag, result, ok ? QString() : error);
                }, [window](const auto &result) {
                    window->applyMatrixResult(std::get<0>(result), std::get<1>(result), std::get<2>(result));
                });
            }
        });
        connect(window, &DiagnosticsWindow::connectionsRequested, window, [window] {
            background(window, [] {
                bool ok = false;
                libcore::QueryConnectionsResp result;
                if (API::defaultClient) result = API::defaultClient->QueryConnections(&ok);
                return std::make_pair(result, ok ? QString() : tr("The core is unavailable."));
            }, [window](const auto &result) { window->applyConnections(result.first, result.second); });
        });
        connect(window, &DiagnosticsWindow::closeConnectionsRequested, window, [](const QStringList &ids) {
            if (!API::defaultClient || ids.isEmpty()) return;
            bool ok = false;
            API::defaultClient->CloseConnections(&ok, ids);
        });
        connect(window, &DiagnosticsWindow::navigateRequested, this, &MainWindow::openDiagnosticsTarget);
        connect(window, &DiagnosticsWindow::ruleRequested, this, &MainWindow::addRuleFromConnection);
        connect(window, &DiagnosticsWindow::usageRequested, this, [this, window](int rangeDays) {
            window->applyUsage(diagnosticsUsage(rangeDays));
        });
    }
    diagnosticsWindow->applyLocalState(diagnosticsLocalState());
    if (!processKey.isEmpty()) diagnosticsWindow->showApplication(processKey);
    diagnosticsWindow->show();
    diagnosticsWindow->raise();
    diagnosticsWindow->activateWindow();
    emit diagnosticsWindow->healthRequested();
}
