#include "include/ui/preview/MainWindowCapture.h"
#include "include/ui/mainwindow.h"
#include "include/ui/utils/DataViewHtmlGenerator.h"
#include "include/ui/widget/TestProgressWidget.h"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QProgressBar>
#include <QToolButton>
#include <QTimer>
#include <QTableView>
#include <QItemSelectionModel>
#include <QLabel>

namespace UiPreview {
void VerifyTestProgress(MainWindow *window, const QString &prefix) {
    auto *view = window->findChild<TestProgressWidget *>(QStringLiteral("testProgress"));
    auto *bar = view ? view->findChild<QProgressBar *>(QStringLiteral("testProgressBar")) : nullptr;
    auto *stop = view ? view->findChild<QToolButton *>(QStringLiteral("testProgressStop")) : nullptr;
    auto *status = window->findChild<QFrame *>(QStringLiteral("statusCard"));
    auto *action = window->findChild<QAction *>(QStringLiteral("simpleModeAction"));
    if (!view || !bar || !stop || !status || !action) {
        qWarning("Test progress: missing production status controls");
        qApp->exit(2);
        return;
    }
    auto *steps = new QTimer(window);
    steps->setInterval(250);
    QObject::connect(steps, &QTimer::timeout, window, [=, step = 0]() mutable {
        const auto require = [steps](bool ok, const char *reason) {
            if (ok) return true;
            qWarning("Test progress: %s", reason);
            steps->stop();
            qApp->exit(2);
            return false;
        };
        auto &generator = window->dataViewHtmlGenerator_;
        if (step == 0) {
            generator.seedLatencyTest(DataViewHtmlGenerator::LatencyTestPanelState::Kind::Url, 10);
            generator.addTestProgress(3);
            window->UpdateDataView(true);
        } else if (step == 1 || step == 3 || step == 5 || step == 7) {
            if (!require(view->isVisible() && bar->isVisible() && bar->width() > 100 &&
                             !bar->visibleRegion().isEmpty() && window->rect().contains(QRect(view->mapTo(window, QPoint()), view->size())),
                         "a running test must show its progress inside the window")) return;
            const int total = step == 1 ? 10 : step == 3 ? 10
                                           : step == 5   ? 5
                                                         : 0;
            const int completed = step == 1 ? 3 : step == 3 ? 4
                                              : step == 5   ? 2
                                                            : 0;
            if (!require(bar->maximum() == total && (total == 0 || bar->value() == completed),
                         "batch progress must match the completed count; a single probe must show activity")) return;
            if (!require(!view->findChild<QLabel *>(QStringLiteral("testProgressTitle"))->text().isEmpty(),
                         "tests must have a readable title")) return;
            if (!require(!status->isVisible(), "the transient status must replace the ordinary footer")) return;
            window->grab().save(prefix + QStringLiteral("-%1.png").arg(step));
            if (step == 7) {
                stop->click();
                if (!require(!stop->isEnabled() && view->isVisible(), "stopping must keep progress visible until the session finishes")) return;
                generator.clearTestSections();
                window->UpdateDataView(true);
            }
        } else if (step == 2) {
            generator.clearTestSections();
            generator.seedSpeedTest(10);
            generator.addTestProgress(4);
            libcore::SpeedTestResult result;
            result.dl_speed = "120 Mbps";
            result.ul_speed = "24 Mbps";
            result.server_name = "Example speed test server";
            result.server_country = "Germany";
            generator.setSpeedtestProgress(QStringLiteral("Example profile with a long name that must fit the compact footer"), result);
            window->UpdateDataView(true);
        } else if (step == 4) {
            action->setChecked(true);
            generator.clearTestSections();
            generator.seedLatencyTest(DataViewHtmlGenerator::LatencyTestPanelState::Kind::Ip, 5);
            generator.addTestProgress(2);
            window->UpdateDataView(true);
        } else if (step == 6) {
            auto *table = window->findChild<QTableView *>(QStringLiteral("profilesTableView"));
            table->clearSelection();
            for (int row: {0, 1})
                table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
            generator.clearTestSections();
            generator.seedSpeedTest(1);
            libcore::SpeedTestResult result;
            result.dl_speed = "120 Mbps";
            result.ul_speed = "24 Mbps";
            result.server_name = "Example speed test server";
            result.server_country = "Germany";
            generator.setSpeedtestProgress(QStringLiteral("Example profile with a long name"), result);
            window->UpdateDataView(true);
        } else {
            if (!require(!view->isVisible(), "finishing tests must hide their progress")) return;
            action->setChecked(false);
            auto *selection = window->findChild<QFrame *>(QStringLiteral("selectionCard"));
            auto *text = window->findChild<QLabel *>(QStringLiteral("selectionText"));
            if (!require(selection && selection->isVisible() && text && text->text() == MainWindow::tr("%n profiles selected", nullptr, 2),
                         "finishing tests must restore the current selection count")) return;
            window->findChild<QTableView *>(QStringLiteral("profilesTableView"))->clearSelection();
            if (!require(status->isVisible(), "clearing the selection must restore the normal footer")) return;
            generator.seedLatencyTest(DataViewHtmlGenerator::LatencyTestPanelState::Kind::Url, 0);
            window->UpdateDataView(true);
            if (!require(stop->isEnabled() && bar->maximum() == 0, "a new session must reset cancellation and support an unknown total")) return;
            generator.clearTestSections();
            window->UpdateDataView(true);
            qInfo("Test progress: latency and speed status passed in both interfaces");
            steps->stop();
            qApp->exit(0);
        }
        ++step;
    });
    steps->start();
}
} // namespace UiPreview
