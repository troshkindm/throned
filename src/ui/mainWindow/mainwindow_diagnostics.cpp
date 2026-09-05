#include "include/ui/mainwindow.h"
#include "include/ui/stats/diagnostics_window.h"
#include "include/api/RPC.h"
#include "include/global/Configs.hpp"
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
    }
    diagnosticsWindow->applyLocalState(diagnosticsLocalState());
    if (!processKey.isEmpty()) diagnosticsWindow->showApplication(processKey);
    diagnosticsWindow->show();
    diagnosticsWindow->raise();
    diagnosticsWindow->activateWindow();
    emit diagnosticsWindow->healthRequested();
}
