#include "include/ui/preview/MainWindowCapture.h"

#include <QApplication>
#include <QEnterEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSysInfo>
#include <QListView>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolButton>

#include "include/database/SettingsRepo.h"
#include "include/database/GroupsRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/entities/AppRoutes.h"
#include "include/global/Configs.hpp"
#include "include/ui/mainwindow.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "include/ui/widget/UpdateStatusWidget.h"
#include "include/ui/widget/HoverMarqueeLabel.h"
#include "include/ui/widget/SimpleModeControls.h"
#include "include/ui/widget/SimpleRoutesPage.h"
#include "include/ui/widget/SimpleSheet.h"
#include "include/ui/widget/StartStopButton.hpp"
#include "include/ui/widget/SubscriptionPopover.hpp"

namespace UiPreview {
void VerifyNarrowLayout(MainWindow *window, const QString &prefix) {
    auto *table = window->findChild<QTableView *>(QStringLiteral("profilesTableView"));
    auto *nav = window->findChild<QToolButton *>(QStringLiteral("toolButton_program"));
    auto *footer = window->findChild<UpdateStatusWidget *>(QStringLiteral("updateStatus"));
    if (table == nullptr || nav == nullptr || footer == nullptr) {
        qApp->exit(2);
        return;
    }
    auto *settings = Configs::dataManager->settingsRepo.get();
    settings->profiles_show_ping = true;
    settings->profiles_show_speed = true;
    settings->profiles_show_traffic = true;
    settings->profile_rows_comfortable = true;
    window->refreshProfileRowStyle();
    table->clearSelection();
    window->setStatsPanelOpen(false, false);
    window->resize(560, 360);

    auto *steps = new QTimer(window);
    steps->setInterval(350);
    QObject::connect(steps, &QTimer::timeout, window,
                     [window, table, nav, footer, settings, steps, prefix, step = 0, closedHeight = 0,
                      idleMarquee = QImage()]() mutable {
                         const auto require = [steps](bool ok, const char *message) {
                             if (ok) return true;
                             qWarning("Narrow layout: %s", message);
                             steps->stop();
                             qApp->exit(2);
                             return false;
                         };
                         if (!require(table->viewport()->height() >= table->verticalHeader()->defaultSectionSize(),
                                      "the server list must retain a complete row")) return;
                         if (!require(settings->profiles_show_speed && settings->profiles_show_traffic,
                                      "resizing must preserve metric preferences")) return;
                         if (step == 0) {
                             if (!require(window->width() < 800 && nav->toolButtonStyle() == Qt::ToolButtonIconOnly,
                                          "the window must actually shrink into the narrow header")) return;
                             if (!require(table->isColumnHidden(ProfilesTableModel::ColcTraffic),
                                          "traffic must give way to the server column")) return;
                             closedHeight = window->minimumHeight();
                             window->grab().save(prefix + QStringLiteral("-closed.png"));
                             window->setStatsPanelOpen(true, false);
                         } else if (step == 1) {
                             window->grab().save(prefix + QStringLiteral("-open.png"));
                             table->selectRow(0);
                             footer->showReady(QStringLiteral("Throned-1.4.0-windows64.zip"));
                         } else if (step == 2) {
                             auto *action = footer->findChild<QPushButton *>(QStringLiteral("updatePrimaryButton"));
                             if (!require(action != nullptr && action->isVisible() && footer->rect().contains(action->geometry()),
                                          "the update action must remain accessible")) return;
                             window->grab().save(prefix + QStringLiteral("-selection-update.png"));
                             table->clearSelection();
                             footer->dismiss();
                             window->setStatsPanelOpen(false, false);
                         } else if (step == 3) {
                             if (!require(window->minimumHeight() == closedHeight,
                                          "closing panels must release the window minimum")) return;
                             window->resize(1180, 780);
                         } else if (step == 4) {
                             if (!require(nav->toolButtonStyle() == Qt::ToolButtonTextBesideIcon &&
                                              !table->isColumnHidden(ProfilesTableModel::ColcSpeed) &&
                                              !table->isColumnHidden(ProfilesTableModel::ColcTraffic),
                                          "widening must restore labels and enabled metrics")) return;
                             window->grab().save(prefix + QStringLiteral("-restored.png"));
                             window->resize(560, 440);
                         } else if (step == 5) {
                             auto *label = dynamic_cast<HoverMarqueeLabel *>(window->findChild<QLabel *>(QStringLiteral("routingStatus")));
                             if (!require(label != nullptr, "the route name must support hover scrolling")) return;
                             const QString name = QStringLiteral("Demo routing profile with a long descriptive name for the narrow window");
                             label->setProperty("statusFullText", name);
                             label->setText(label->fontMetrics().elidedText(name, Qt::ElideRight, label->width() - 2));
                             idleMarquee = label->grab().toImage();
                             QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), label->mapToGlobal(QPoint(1, 1)));
                             QApplication::sendEvent(label, &enter);
                         } else if (step == 9) {
                             auto *label = window->findChild<QLabel *>(QStringLiteral("routingStatus"));
                             if (!require(label->grab().toImage() != idleMarquee, "hovering must reveal the clipped route name")) return;
                             window->grab().save(prefix + QStringLiteral("-marquee.png"));
                             QEvent leave(QEvent::Leave);
                             QApplication::sendEvent(label, &leave);
                             if (!require(label->grab().toImage() == idleMarquee, "leaving must restore the static label")) return;
                             steps->stop();
                             qApp->exit(0);
                         }
                         ++step;
                     });
    steps->start();
}
void VerifySimpleMode(MainWindow *window, const QString &prefix) {
    auto *action = window->findChild<QAction *>(QStringLiteral("simpleModeAction"));
    auto *server = window->findChild<SimpleServerCard *>(QStringLiteral("simpleServer"));
    auto *modeRow = window->findChild<SimpleListRow *>(QStringLiteral("simpleModeRow"));
    auto *power = window->findChild<StartStopButton *>(QStringLiteral("simpleConnect"));
    auto *sheet = window->findChild<SimpleServerSheet *>();
    auto *modes = window->findChild<SimpleModeSheet *>();
    auto *tabs = window->findChild<QTabWidget *>(QStringLiteral("groupsCard"));
    if (!action || !server || !modeRow || !power || !sheet || !modes || !tabs) {
        qWarning("Simple mode: the simple screen is missing a control");
        qApp->exit(2);
        return;
    }
    const QSize fullSize = window->size();
    action->setChecked(true);
    const auto key = [](QWidget *target, Qt::Key value) {
        QKeyEvent press(QEvent::KeyPress, value, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, value, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QApplication::sendEvent(target, &release);
    };
    auto *steps = new QTimer(window);
    steps->setInterval(400);
    QObject::connect(steps, &QTimer::timeout, window,
                     [window, prefix, action, server, modeRow, power, sheet, modes, tabs, steps, fullSize, key, step = 0,
                      originalTab = tabs->currentIndex()]() mutable {
                         const auto require = [steps](bool ok, const char *reason) {
                             if (ok) return true;
                             qWarning("Simple mode: %s", reason);
                             steps->stop();
                             qApp->exit(2);
                             return false;
                         };
                         if (step == 0) {
                             if (!require(Configs::dataManager->settingsRepo->simple_mode && window->width() < 600 &&
                                              server->isVisible() && power->isEnabled() && window->get_profile_to_start() >= 0,
                                          "the portrait view must offer the current group's profile")) return;
                             auto *toggle = window->findChild<QToolButton *>(QStringLiteral("titleSimpleMode"));
                             if (!require(toggle != nullptr && toggle->toolTip() == MainWindow::tr("Full interface"),
                                          "the caption button must offer the way back to the full interface")) return;
                             window->grab().save(prefix + QStringLiteral("-idle.png"));
                             server->click();
                         } else if (step == 1) {
                             if (!require(sheet->isOpen() && sheet->isVisible() && sheet->list()->model()->rowCount() == 4,
                                          "the server card must open a sheet with the group's profiles")) return;
                             window->grab().save(prefix + QStringLiteral("-profiles.png"));
                             sheet->list()->setCurrentIndex(sheet->list()->model()->index(1, 0));
                             key(sheet->list(), Qt::Key_Return);
                             if (!require(!sheet->isOpen() && window->get_profile_to_start() == sheet->profileAt(1),
                                          "Enter must choose the highlighted profile and close the sheet")) return;
                             if (!require(server->badge() == MainWindow::tr("%1 ms").arg(57),
                                          "the server card must show the chosen profile's saved ping")) return;
                         } else if (step == 2) {
                             server->click();
                             key(sheet->list(), Qt::Key_Down);
                             key(sheet->list(), Qt::Key_Return);
                             if (!require(!sheet->isOpen() && window->get_profile_to_start() == sheet->profileAt(2),
                                          "arrow keys must move through the list before Enter chooses")) return;
                             server->click();
                             key(sheet->list(), Qt::Key_Escape);
                             if (!require(!sheet->isOpen(), "Escape must close the server sheet")) return;
                             modeRow->click();
                         } else if (step == 3) {
                             if (!require(modes->isOpen() && modes->isVisible(), "the mode row must open its sheet")) return;
                             window->grab().save(prefix + QStringLiteral("-mode.png"));
                             modes->dismiss();
                             auto empty = Configs::GroupsRepo::NewGroup();
                             empty->name = QStringLiteral("Empty demo group");
                             Configs::dataManager->groupsRepo->AddGroup(empty);
                             window->refresh_groups();
                             for (int i = 0; i < tabs->count(); ++i)
                                 if (tabs->tabText(i) == empty->name) tabs->setCurrentIndex(i);
                         } else if (step == 4) {
                             if (!require(!modes->isOpen() && window->get_profile_to_start() < 0 && !power->isEnabled(),
                                          "an empty group must disable Connect")) return;
                             window->grab().save(prefix + QStringLiteral("-empty.png"));
                             tabs->setCurrentIndex(originalTab);
                             auto *footer = window->findChild<UpdateStatusWidget *>(QStringLiteral("updateStatus"));
                             footer->showReady(QStringLiteral("Throned-1.4.0-windows64.zip"));
                         } else if (step == 5) {
                             auto *footer = window->findChild<UpdateStatusWidget *>(QStringLiteral("updateStatus"));
                             if (!require(footer->isVisible() && power->isEnabled() && window->get_profile_to_start() >= 0,
                                          "the shared update footer and restored group must be visible")) return;
                             window->grab().save(prefix + QStringLiteral("-update.png"));
                             footer->dismiss();
                             action->setChecked(false);
                         } else if (step == 6) {
                             if (!require(!Configs::dataManager->settingsRepo->simple_mode &&
                                              window->size() == fullSize && !server->isVisible(),
                                          "returning to full mode must restore its geometry")) return;
                             auto *toggle = window->findChild<QToolButton *>(QStringLiteral("titleSimpleMode"));
                             if (!require(toggle->toolTip() == MainWindow::tr("Simple mode"),
                                          "the caption button must offer Simple mode from the full interface")) return;
                             toggle->click();
                         } else if (step == 7) {
                             if (!require(window->width() < 600 && power->isEnabled(),
                                          "re-entering simple mode must restore the portrait view")) return;
                             Configs::SettingsRepo restored(Configs::dataManager->getDatabase());
                             if (!require(restored.simple_mode && !restored.simple_window_geometry.isEmpty() &&
                                              !restored.mainWindowGeometry.isEmpty(),
                                          "mode and separate window geometries must survive a settings reload")) return;
                             window->grab().save(prefix + QStringLiteral("-restored.png"));
                             server->click();
                         } else if (step == 8) {
                             auto *allowance = sheet->findChild<QPushButton *>(QStringLiteral("simpleSheetAllowance"));
                             if (!require(allowance != nullptr && allowance->isVisible(),
                                          "a subscription group must show its allowance in the server sheet")) return;
                             allowance->click();
                         } else if (step == 9) {
                             auto *card = window->findChild<SubscriptionPopover *>();
                             if (!require(card != nullptr && card->isVisible(),
                                          "the allowance must open the shared subscription card")) return;
                             card->grab().save(prefix + QStringLiteral("-subscription.png"));
                             card->hide();
                             sheet->dismiss();
                         } else if (step == 10) {
                             power->setState(StartStopButton::State::Idle);
                             window->findChild<SimpleListRow *>(QStringLiteral("simpleRoutesRow"))->click();
                         } else if (step == 11) {
                             auto *page = window->findChild<SimpleRoutesPage *>();
                             auto *telegram = page ? page->findChild<QAbstractButton *>(QStringLiteral("simpleRouteToggle_telegram")) : nullptr;
                             if (!require(page != nullptr && page->isVisible() && telegram != nullptr && !telegram->isChecked(),
                                          "the routing screen must list the apps found on the computer")) return;
                             window->grab().save(prefix + QStringLiteral("-routes.png"));
                             page->findChild<QPushButton *>(QStringLiteral("simpleRouteMore"))->click();
                         } else if (step == 12) {
                             auto *page = window->findChild<SimpleRoutesPage *>();
                             auto *area = qobject_cast<QScrollArea *>(page->scrollArea());
                             QWidget *third = nullptr;
                             for (auto *label: area->widget()->findChildren<QLabel *>(QStringLiteral("simpleRouteGroup"))) {
                                 if (label->text() == SimpleRoutesPage::tr("Social networks").toUpper()) third = label->parentWidget();
                             }
                             if (!require(third != nullptr, "the expanded catalog must be grouped by category")) return;
                             area->verticalScrollBar()->setValue(third->y() + 90);
                             QWidget *pinned = nullptr;
                             for (auto *child: area->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
                                 if (child->isVisible() && child->findChild<QLabel *>(QStringLiteral("simpleRouteGroup")) != nullptr) pinned = child;
                             bool titled = false;
                             for (auto *label: pinned ? pinned->findChildren<QLabel *>(QStringLiteral("simpleRouteGroup")) : QList<QLabel *>())
                                 titled = titled || label->text() == SimpleRoutesPage::tr("Social networks").toUpper();
                             if (!require(titled, "a scrolled group must keep its title pinned")) return;
                             window->grab().save(prefix + QStringLiteral("-routes-catalog.png"));
                             area->verticalScrollBar()->setValue(0);
                             page->findChild<QAbstractButton *>(QStringLiteral("simpleRouteToggle_telegram"))->click();
                         } else if (step == 13) {
                             auto *page = window->findChild<SimpleRoutesPage *>();
                             auto *add = page->findChild<QLineEdit *>(QStringLiteral("simpleRouteAdd"));
                             if (!require(page->isDirty() && add != nullptr && add->isVisible(),
                                          "switching an app on must stage it and open its details")) return;
                             add->setText(QStringLiteral("https://www.example.org/news"));
                             emit add->returnPressed();
                         } else if (step == 14) {
                             auto *page = window->findChild<SimpleRoutesPage *>();
                             bool chip = false;
                             for (auto *label: page->findChildren<QLabel *>(QStringLiteral("simpleRouteChipText"))) chip = chip || label->text() == QStringLiteral("example.org");
                             if (!require(chip, "a typed site must become a chip of the open app")) return;
                             window->grab().save(prefix + QStringLiteral("-routes-detail.png"));
                             page->findChild<QPushButton *>(QStringLiteral("simpleRouteApply"))->click();
                             const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
                             const auto routes = Configs::AppRoutes::Read(profile->Rules);
                             const bool saved = std::any_of(routes.begin(), routes.end(), [](const Configs::AppRoutes::Route &route) {
                                 return route.id == QStringLiteral("telegram") && route.domains.contains(QStringLiteral("example.org")) &&
                                        route.processes.contains(QStringLiteral("Telegram.exe"), Qt::CaseInsensitive) == (QSysInfo::productType() == QStringLiteral("windows"));
                             });
                             if (!require(saved && !page->isDirty(), "Apply must write the app's rules into the routing profile")) return;
                             window->findChild<QToolButton *>(QStringLiteral("simpleRoutesBack"))->click();
                             if (!require(!page->isVisible() && server->isVisible(), "Back must return to the connection screen")) return;
                             power->setState(StartStopButton::State::Connecting);
                             window->findChild<QLabel *>(QStringLiteral("simpleState"))->setText(MainWindow::tr("Connecting…"));
                         } else if (step == 15) {
                             window->grab().save(prefix + QStringLiteral("-connecting.png"));
                             power->setState(StartStopButton::State::Running);
                             window->findChild<QLabel *>(QStringLiteral("simpleState"))->setText(MainWindow::tr("Connected"));
                             auto *traffic = window->findChild<SimpleTrafficLine *>(QStringLiteral("simpleTraffic"));
                             for (int sample: {420000, 610000, 540000, 880000, 1280000, 960000, 1120000})
                                 traffic->addSample(sample, sample / 14);
                         } else {
                             window->grab().save(prefix + QStringLiteral("-connected.png"));
                             steps->stop();
                             qApp->exit(0);
                         }
                         ++step;
                     });
    steps->start();
}
} // namespace UiPreview
