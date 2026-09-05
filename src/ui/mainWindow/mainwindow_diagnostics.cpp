#include "include/ui/mainwindow.h"
#include "include/ui/stats/diagnostics_window.h"
#include "include/api/RPC.h"
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

// The verdict's action leaves the window: routing rules answer "why this outbound",
// the reachability matrix answers "is it only this server".
void MainWindow::openDiagnosticsHelper(bool routingRules) {
    if (routingRules) {
        on_menu_routing_settings_triggered();
        return;
    }
    const auto selection = get_now_selected_list();
    runSiteReachability(selection.isEmpty() ? QList<int>{} : selection);
}

void MainWindow::openDiagnostics(const QString &processKey) {
    if (!diagnosticsWindow) {
        diagnosticsWindow = new DiagnosticsWindow(this);
        diagnosticsWindow->setAttribute(Qt::WA_DeleteOnClose);
        auto *window = diagnosticsWindow.data();
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
        connect(window, &DiagnosticsWindow::reachabilityRequested, this, [this] {
            openDiagnosticsHelper(false);
        });
        connect(window, &DiagnosticsWindow::routingRulesRequested, this, [this] {
            openDiagnosticsHelper(true);
        });
        connect(window, &DiagnosticsWindow::connectionsRequested, window, [window] {
            background(window, [] {
                bool ok = false;
                libcore::QueryConnectionsResp result;
                if (API::defaultClient) result = API::defaultClient->QueryConnections(&ok);
                return std::make_pair(result, ok ? QString() : tr("The core is unavailable."));
            }, [window](const auto &result) { window->applyConnections(result.first, result.second); });
        });
    }
    if (!processKey.isEmpty()) diagnosticsWindow->showApplication(processKey);
    diagnosticsWindow->show();
    diagnosticsWindow->raise();
    diagnosticsWindow->activateWindow();
}
