#include "include/ui/preview/MainWindowCapture.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>

#include "include/ui/mainwindow_interface.h"
#include "include/ui/stats/MiniChartWidget.h"
#include "include/ui/widget/StartStopButton.hpp"
#include "include/ui/widget/UpdateStatusWidget.h"

namespace UiPreview {
// Qt Test is not linked here, and one synthetic key press does not justify it.
void QTest_keyClick(QWidget *target, Qt::Key key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
}

void SavePopupComposite(QWidget *window, QWidget *popup, const QString &path) {
    if (window == nullptr || popup == nullptr) return;
    QPixmap composed = window->grab();
    QPainter painter(&composed);
    painter.drawPixmap(window->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0))), popup->grab());
    painter.end();
    composed.save(path, "PNG");
}

void CaptureUpdateStatusPreviews(MainWindow *window, const QString &prefix) {
    auto *status = window->findChild<UpdateStatusWidget *>(QStringLiteral("updateStatus"));
    if (status == nullptr) {
        qWarning() << "Update status widget is missing from the main-window preview";
        qApp->exit(2);
        return;
    }
    const QString asset = QStringLiteral("Throned-1.4.0-windows64.zip");
    status->showDownloading(asset, 19398656, 46137344);
    QTimer::singleShot(180, window, [window, status, prefix, asset] {
        window->grab().save(prefix + QStringLiteral("-update-downloading.png"), "PNG");
        status->showPreparing(asset);
        QTimer::singleShot(180, window, [window, status, prefix, asset] {
            window->grab().save(prefix + QStringLiteral("-update-preparing.png"), "PNG");
            status->showReady(asset);
            QTimer::singleShot(180, window, [window, status, prefix] {
                window->grab().save(prefix + QStringLiteral("-update-ready.png"), "PNG");
                status->showError(UpdateStatusWidget::tr("GitHub could not be reached through the current connection."));
                QTimer::singleShot(180, window, [window, status, prefix] {
                    window->grab().save(prefix + QStringLiteral("-update-error.png"), "PNG");
                    status->dismiss();
                    qApp->exit(0);
                });
            });
        });
    });
}

void CaptureConnectionsPreview(MainWindow *window, const QString &prefix) {
    window->grab().save(prefix + QStringLiteral("-window.png"), "PNG");
    auto *table = window->findChild<QTableView *>(QStringLiteral("connections"));
    if (table == nullptr || table->model() == nullptr || table->model()->rowCount() == 0) {
        qApp->exit(2);
        return;
    }
    const QPoint point = table->visualRect(table->model()->index(0, 0)).center();
    QTimer::singleShot(400, window, [prefix, window] {
        auto *popup = QApplication::activePopupWidget();
        if (popup == nullptr) {
            qApp->exit(2);
            return;
        }
        popup->grab().save(prefix + QStringLiteral("-menu.png"), "PNG");
        SavePopupComposite(window, popup, prefix + QStringLiteral("-menu-in-place.png"));
        QTest_keyClick(popup, Qt::Key_Down);
        QTest_keyClick(popup, Qt::Key_Down);
        QTest_keyClick(popup, Qt::Key_Right);
        QTimer::singleShot(300, popup, [prefix, popup, window] {
            if (auto *submenu = QApplication::activePopupWidget(); submenu && submenu != popup)
                submenu->grab().save(prefix + QStringLiteral("-submenu.png"), "PNG");
            popup->close();
            auto *startStop = window->findChild<StartStopButton *>(QStringLiteral("toolButton_startstop"));
            if (startStop == nullptr) {
                qApp->exit(2);
                return;
            }
            startStop->setState(StartStopButton::State::Running);
            // The status strip's test button follows the running profile, which the
            // preview has none of, so the running shot shows it deliberately.
            auto *connectionTest = window->findChild<QToolButton *>(QStringLiteral("statusCellButton"));
            if (connectionTest == nullptr) {
                qWarning() << "The connection test button is missing from the status strip";
                qApp->exit(2);
                return;
            }
            connectionTest->show();
            QTimer::singleShot(260, window, [prefix, window, startStop, connectionTest] {
                window->grab().save(prefix + QStringLiteral("-stop-button.png"), "PNG");
                connectionTest->hide();
                startStop->setState(StartStopButton::State::Idle);
                CaptureUpdateStatusPreviews(window, prefix);
            });
        });
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, table->viewport()->mapToGlobal(point));
    QApplication::sendEvent(table->viewport(), &event);
}

void CaptureGraphPreview(MainWindow *window, QTabWidget *statsTabs, const QString &prefix) {
    // Deterministic synthetic samples exercise both graph columns. No live
    // traffic or endpoint data is consulted in preview mode.
    for (int i = 0; i < 72; ++i) {
        const int proxyDown = 420000 + (i % 13) * 76000 + ((i / 9) % 3) * 180000;
        const int proxyUp = 90000 + (i % 8) * 23000;
        const int directDown = 24000 + (i % 11) * 6500;
        const int directUp = 7000 + (i % 6) * 2400;
        window->update_traffic_graph(proxyDown, proxyUp, directDown, directUp);
    }
    if (auto *pingWidget = window->findChild<QWidget *>(QStringLiteral("pingChart"))) {
        auto *ping = static_cast<MiniChartWidget *>(pingWidget);
        ping->setColors(QColor(QStringLiteral("#35D07F")), QColor(QStringLiteral("#8295A6")));
        for (int i = 0; i < 72; ++i) {
            const double proxyMs = i == 49 ? 168.0 : 41.0 + (i % 9) * 2.0;
            const double directMs = 24.0 + (i % 6);
            ping->push(proxyMs, directMs);
        }
    }
    if (auto *graphPage = statsTabs->findChild<QWidget *>(QStringLiteral("graph_tab")))
        statsTabs->setCurrentWidget(graphPage);
    QTimer::singleShot(350, window, [window, statsTabs, prefix] {
        window->grab().save(prefix + QStringLiteral("-graph.png"), "PNG");
        if (auto *connectionsPage = statsTabs->findChild<QWidget *>(QStringLiteral("connections_tab")))
            statsTabs->setCurrentWidget(connectionsPage);
        QTimer::singleShot(300, window, [window, prefix] {
            CaptureConnectionsPreview(window, prefix);
        });
    });
}

void CaptureLogsPreview(MainWindow *window, QTabWidget *statsTabs, const QString &prefix) {
    window->grab().save(prefix + QStringLiteral("-logs.png"), "PNG");
    auto *logTools = statsTabs->findChild<QWidget *>(QStringLiteral("logTools"));
    auto *logMenuButton = logTools == nullptr ? nullptr : logTools->findChild<QToolButton *>();
    if (logMenuButton == nullptr) {
        qApp->exit(2);
        return;
    }
    // showMenu() runs a nested event loop on Windows, so arm the capture and
    // close timer before entering it.
    QTimer::singleShot(250, window, [window, prefix, statsTabs] {
        auto *popup = QApplication::activePopupWidget();
        if (popup == nullptr) {
            qApp->exit(2);
            return;
        }
        popup->grab().save(prefix + QStringLiteral("-logs-menu.png"), "PNG");
        SavePopupComposite(window, popup, prefix + QStringLiteral("-logs-menu-in-place.png"));
        popup->close();
        QTimer::singleShot(350, window, [window, prefix] {
            QTabWidget *statsTabs = nullptr;
            for (auto *tabs: window->findChildren<QTabWidget *>())
                if (tabs->findChild<QWidget *>(QStringLiteral("graph_tab")) != nullptr) {
                    statsTabs = tabs;
                    break;
                }
            if (statsTabs == nullptr) {
                qApp->exit(2);
                return;
            }
            CaptureGraphPreview(window, statsTabs, prefix);
        });
    });
    logMenuButton->showMenu();
}

void BeginMainWindowCapture(MainWindow *window, const QString &prefix) {
    // Let the whole production shell complete one open layout pass before
    // capturing its shipped-closed state. This keeps the status strip's
    // child geometry deterministic in off-screen screenshot runs.
    window->setStatsPanelOpen(true, false);
    QTimer::singleShot(350, window, [window, prefix] {
        window->setStatsPanelOpen(false, false);
        QTimer::singleShot(300, window, [window, prefix] {
            window->grab().save(prefix + QStringLiteral("-closed.png"), "PNG");
            window->setStatsPanelOpen(true, false);
            QTabWidget *statsTabs = nullptr;
            for (auto *tabs: window->findChildren<QTabWidget *>()) {
                for (int tab = 0; tab < tabs->count(); ++tab)
                    if (tabs->widget(tab)->findChild<QTableView *>(QStringLiteral("connections")) != nullptr) {
                        statsTabs = tabs;
                        break;
                    }
                if (statsTabs != nullptr) break;
            }
            if (statsTabs == nullptr) {
                qApp->exit(2);
                return;
            }
            if (auto *logsPage = statsTabs->findChild<QWidget *>(QStringLiteral("Logs")))
                statsTabs->setCurrentWidget(logsPage);
            QTimer::singleShot(350, window, [window, prefix, statsTabs] {
                CaptureLogsPreview(window, statsTabs, prefix);
            });
        });
    });
}

void VerifyProfileFiltersThenCapture(MainWindow *window, const QString &prefix) {
    auto *table = window->findChild<QTableView *>(QStringLiteral("profilesTableView"));
    auto *search = window->findChild<QLineEdit *>(QStringLiteral("serverSearch"));
    if (table == nullptr || table->model() == nullptr || search == nullptr) {
        qApp->exit(2);
        return;
    }
    const int unfilteredRows = table->model()->rowCount();
    search->setText(QStringLiteral("Demo North"));
    QTimer::singleShot(140, window, [window, prefix, table, search, unfilteredRows] {
        if (table->model()->rowCount() != 1) {
            qWarning() << "Global profile search preview check failed" << table->model()->rowCount();
            qApp->exit(2);
            return;
        }
        window->grab().save(prefix + QStringLiteral("-search-filtered.png"), "PNG");
        search->clear();
        QTimer::singleShot(120, window, [window, prefix, table, unfilteredRows] {
            if (table->model()->rowCount() != unfilteredRows) {
                qApp->exit(2);
                return;
            }
            BeginMainWindowCapture(window, prefix);
        });
    });
}

void VerifyStatsPanelAndCapture(MainWindow *window, const QString &prefix) {
    auto *panel = window->findChild<QWidget *>(QStringLiteral("statsPanelHost"));
    auto *strip = window->findChild<QWidget *>(QStringLiteral("logsStrip"));
    if (panel == nullptr || strip == nullptr) {
        qApp->exit(2);
        return;
    }

    window->setStatsPanelOpen(false, false);
    QTimer::singleShot(0, window, [window, panel, strip, prefix] {
        window->setStatsPanelOpen(true, true);
        QTimer::singleShot(95, window, [window, panel, strip, prefix] {
            const bool opening = panel->isVisible() && strip->isVisible() && panel->maximumHeight() > 0 && panel->maximumHeight() < QWIDGETSIZE_MAX && strip->maximumHeight() >= 0 && strip->maximumHeight() < 39;
            if (!opening) {
                qWarning() << "Stats panel opening animation preview check failed"
                           << panel->isVisible() << strip->isVisible()
                           << panel->maximumHeight() << strip->maximumHeight();
                qApp->exit(2);
                return;
            }
            window->grab().save(prefix + QStringLiteral("-panel-opening.png"), "PNG");
            QTimer::singleShot(145, window, [window, panel, strip, prefix] {
                if (!panel->isVisible() || strip->isVisible()) {
                    qWarning() << "Stats panel open state preview check failed";
                    qApp->exit(2);
                    return;
                }
                window->setStatsPanelOpen(false, true);
                QTimer::singleShot(95, window, [window, panel, strip, prefix] {
                    const bool closing = panel->isVisible() && strip->isVisible() && panel->maximumHeight() > 0 && panel->maximumHeight() < QWIDGETSIZE_MAX && strip->maximumHeight() > 0 && strip->maximumHeight() <= 39;
                    if (!closing) {
                        qWarning() << "Stats panel closing animation preview check failed"
                                   << panel->isVisible() << strip->isVisible()
                                   << panel->maximumHeight() << strip->maximumHeight();
                        qApp->exit(2);
                        return;
                    }
                    window->grab().save(prefix + QStringLiteral("-panel-closing.png"), "PNG");
                    QTimer::singleShot(145, window, [window, panel, strip, prefix] {
                        if (panel->isVisible() || !strip->isVisible()) {
                            qWarning() << "Stats panel closed state preview check failed";
                            qApp->exit(2);
                            return;
                        }
                        window->setStatsPanelOpen(true, false);
                        QTimer::singleShot(0, window, [window, prefix] {
                            VerifyProfileFiltersThenCapture(window, prefix);
                        });
                    });
                });
            });
        });
    });
}

void CaptureQuickAdd(MainWindow *window, const QString &prefix, bool fromEmptyState) {
    if (fromEmptyState)
        window->grab().save(prefix + QStringLiteral("-empty-group.png"), "PNG");

    QAbstractButton *trigger = fromEmptyState
                                   ? static_cast<QAbstractButton *>(window->findChild<QPushButton *>(QStringLiteral("emptyStateAction")))
                                   : static_cast<QAbstractButton *>(window->findChild<QToolButton *>(QStringLiteral("groupAddButton")));
    if (trigger == nullptr) {
        qWarning() << "Quick-add preview trigger is missing" << fromEmptyState;
        qApp->exit(2);
        return;
    }
    trigger->click();
    QTimer::singleShot(220, window, [window, prefix] {
        auto *overlay = window->findChild<QWidget *>(QStringLiteral("quickAddOverlay"));
        auto *link = window->findChild<QLineEdit *>(QStringLiteral("quickAddLinkInput"));
        auto *close = window->findChild<QToolButton *>(QStringLiteral("quickAddCloseButton"));
        if (overlay == nullptr || !overlay->isVisible() || link == nullptr || close == nullptr || !close->isVisible() || window->findChild<QLabel *>(QStringLiteral("quickAddEscape")) != nullptr) {
            qWarning() << "Quick-add overlay did not open";
            qApp->exit(2);
            return;
        }
        window->grab().save(prefix + QStringLiteral("-quick-add.png"), "PNG");
        link->setText(QStringLiteral("https://subscription.example/profiles"));
        QTimer::singleShot(120, window, [window, prefix] {
            window->grab().save(prefix + QStringLiteral("-quick-add-detected.png"), "PNG");
            auto *manual = window->findChild<QPushButton *>(QStringLiteral("quickAddManualButton"));
            if (manual == nullptr) {
                qApp->exit(2);
                return;
            }
            manual->click();
            QTimer::singleShot(140, window, [window, prefix] {
                window->grab().save(prefix + QStringLiteral("-quick-add-manual-profile.png"), "PNG");
                const auto tabs = window->findChildren<QToolButton *>(QStringLiteral("quickAddManualTab"));
                if (tabs.size() < 2) {
                    qApp->exit(2);
                    return;
                }
                tabs.at(1)->click();
                QTimer::singleShot(140, window, [window, prefix] {
                    window->grab().save(prefix + QStringLiteral("-quick-add-manual-group.png"), "PNG");
                    qApp->exit(0);
                });
            });
        });
    });
}

} // namespace UiPreview
