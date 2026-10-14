#include "include/ui/widget/SimpleSheet.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"

#include <QListView>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <algorithm>

ThemeManager *themeManager() {
    static ThemeManager manager;
    return &manager;
}
ThronedThemeColors ThemeManager::Colors(const QString &) const { return {}; }
const ThronedSkin *ThemeManager::Skin(const QString &) const { return nullptr; }
void ThemeManager::RegisterStyle(QWidget *, const QString &) const {}
QColor ProfileRowDelegate::latencyColor(int, const ThronedThemeColors &) { return Qt::gray; }

namespace {
QList<SimpleServerEntry> servers(int firstId = 1000, int currentRow = 0) {
    QList<SimpleServerEntry> entries;
    for (int row = 0; row < 100; ++row)
        entries.append({firstId + row, QStringLiteral("Example server %1").arg(row),
                        QStringLiteral("192.0.2.1:443"), QStringLiteral("42 ms"), 42, row == currentRow});
    return entries;
}

struct SheetFixture {
    QWidget host;
    SimpleServerSheet sheet{&host};
    QList<SimpleServerEntry> entries = servers();

    SheetFixture() {
        host.resize(500, 720);
        sheet.setGroups({{1, QStringLiteral("Example group")}, {2, QStringLiteral("Other group")}}, 1);
        sheet.setServers(entries);
        host.show();
        sheet.open();
    }
};
} // namespace

class TestSimpleServerSheet : public QObject {
    Q_OBJECT

private slots:
    void backgroundRefreshKeepsBrowsingPosition_data() {
        QTest::addColumn<bool>("changed");
        QTest::newRow("same entries") << false;
        QTest::newRow("updated ping, address and selection") << true;
    }

    void backgroundRefreshKeepsBrowsingPosition() {
        QFETCH(bool, changed);
        SheetFixture fixture;
        auto *list = fixture.sheet.list();
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        QVERIFY(list->verticalScrollBar()->maximum() > 0);
        list->setCurrentIndex(list->model()->index(35, 0));
        const QPoint point = list->viewport()->rect().center();
        QWheelEvent wheel(point, list->viewport()->mapToGlobal(point), QPoint(), QPoint(0, -480),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(list->viewport(), &wheel);
        const int position = list->verticalScrollBar()->value();
        QVERIFY(position > 0);
        const int hoveredId = fixture.sheet.profileAt(list->indexAt(point).row());
        QSignalSpy chosen(&fixture.sheet, &SimpleServerSheet::serverChosen);
        for (int tick = 0; tick < 5; ++tick) {
            if (changed) {
                fixture.entries[0].current = false;
                fixture.entries[10].current = true;
                fixture.entries[40].name = QStringLiteral("Renamed example %1").arg(tick);
                fixture.entries[40].address = QStringLiteral("198.51.100.1:443");
                fixture.entries[40].latencyText = QStringLiteral("%1 ms").arg(43 + tick);
                fixture.entries[40].latency = 43 + tick;
            }
            fixture.sheet.setGroups({{1, QStringLiteral("Example group")}, {2, QStringLiteral("Other group")}}, 1);
            fixture.sheet.setServers(fixture.entries);
            QCoreApplication::processEvents();
            QCOMPARE(list->verticalScrollBar()->value(), position);
            QCOMPARE(fixture.sheet.profileAt(list->currentIndex().row()), 1035);
            QCOMPARE(fixture.sheet.profileAt(list->indexAt(point).row()), hoveredId);
        }
        QCOMPARE(list->model()->index(40, 0).data().toString(), fixture.entries[40].name);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.first().first().toInt(), hoveredId);
    }

    void insertingServersKeepsTheVisibleServer() {
        SheetFixture fixture;
        auto *list = fixture.sheet.list();
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        list->setCurrentIndex(list->model()->index(60, 0));
        list->verticalScrollBar()->setValue(2107);
        const auto top = list->indexAt(QPoint(list->viewport()->width() / 2, 0));
        QVERIFY(top.isValid());
        const int topId = fixture.sheet.profileAt(top.row());
        const int topOffset = list->visualRect(top).top();
        fixture.entries.prepend({999, QStringLiteral("Added example"), QStringLiteral("203.0.113.1:443")});
        fixture.sheet.setServers(fixture.entries);
        QCoreApplication::processEvents();
        const auto restored = list->indexAt(QPoint(list->viewport()->width() / 2, 0));
        QVERIFY(restored.isValid());
        QCOMPARE(fixture.sheet.profileAt(restored.row()), topId);
        QCOMPARE(list->visualRect(restored).top(), topOffset);
        QCOMPARE(fixture.sheet.profileAt(list->currentIndex().row()), 1060);
    }

    void switchingGroupUsesItsSelectedServer() {
        SheetFixture fixture;
        auto *list = fixture.sheet.list();
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        list->setCurrentIndex(list->model()->index(60, 0));
        fixture.sheet.setGroups({{1, QStringLiteral("Example group")}, {2, QStringLiteral("Other group")}}, 2);
        fixture.sheet.setServers(servers(2000, 5));
        QCoreApplication::processEvents();
        QCOMPARE(fixture.sheet.profileAt(list->currentIndex().row()), 2005);
        QVERIFY(list->viewport()->rect().intersects(list->visualRect(list->currentIndex())));
    }

    void removingServersKeepsBrowsingAndHandlesAnEmptyList() {
        SheetFixture fixture;
        auto *list = fixture.sheet.list();
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        list->setCurrentIndex(list->model()->index(60, 0));
        list->verticalScrollBar()->setValue(2107);
        const int position = list->verticalScrollBar()->value();
        fixture.entries.removeAt(60);
        fixture.sheet.setServers(fixture.entries);
        QCoreApplication::processEvents();
        QCOMPARE(list->verticalScrollBar()->value(), position);
        QCOMPARE(fixture.sheet.profileAt(list->currentIndex().row()), 1061);
        fixture.sheet.setServers({});
        QCoreApplication::processEvents();
        QCOMPARE(list->model()->rowCount(), 0);
        QVERIFY(!list->currentIndex().isValid());
        QVERIFY(list->isHidden());
        fixture.sheet.setServers(servers(2000, 80));
        QCoreApplication::processEvents();
        QCOMPARE(fixture.sheet.profileAt(list->currentIndex().row()), 2080);
        QVERIFY(list->viewport()->rect().intersects(list->visualRect(list->currentIndex())));
    }

    void backgroundRefreshKeepsTheGroupTabsPosition() {
        SheetFixture fixture;
        QList<QPair<int, QString>> groups;
        for (int id = 1; id < 30; ++id) groups.append({id, QStringLiteral("Example group %1").arg(id)});
        fixture.sheet.setGroups(groups, 1);
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        auto *area = fixture.sheet.findChild<QScrollArea *>();
        QVERIFY(area);
        QTRY_VERIFY(area->horizontalScrollBar()->maximum() > 500);
        area->horizontalScrollBar()->setValue(400);
        const auto buttons = fixture.sheet.findChildren<QPushButton *>(QStringLiteral("simpleSheetTab"));
        fixture.sheet.setGroups(groups, 1);
        QCoreApplication::processEvents();
        QCOMPARE(area->horizontalScrollBar()->value(), 400);
        QCOMPARE(fixture.sheet.findChildren<QPushButton *>(QStringLiteral("simpleSheetTab")), buttons);
    }

    void sortingKeepsTheVisibleServerAndSelection() {
        SheetFixture fixture;
        auto *list = fixture.sheet.list();
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        list->setCurrentIndex(list->model()->index(60, 0));
        list->verticalScrollBar()->setValue(2107);
        const auto top = list->indexAt(QPoint(list->viewport()->width() / 2, 0));
        const int id = fixture.sheet.profileAt(top.row());
        const int offset = list->visualRect(top).top();
        std::reverse(fixture.entries.begin(), fixture.entries.end());
        fixture.sheet.setServers(fixture.entries);
        QCoreApplication::processEvents();
        const auto restored = list->indexAt(QPoint(list->viewport()->width() / 2, 0));
        QCOMPARE(fixture.sheet.profileAt(restored.row()), id);
        QCOMPARE(list->visualRect(restored).top(), offset);
        QCOMPARE(fixture.sheet.profileAt(list->currentIndex().row()), 1060);
    }

    void reopeningUsesTheSelectedServer() {
        SheetFixture fixture;
        auto *list = fixture.sheet.list();
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        list->setCurrentIndex(list->model()->index(35, 0));
        fixture.sheet.dismiss();
        QTRY_VERIFY(!fixture.sheet.isVisible());
        fixture.sheet.setServers(servers(1000, 80));
        fixture.sheet.open();
        QTRY_COMPARE(fixture.sheet.progress(), 1.0);
        QCOMPARE(fixture.sheet.profileAt(list->currentIndex().row()), 1080);
        QVERIFY(list->viewport()->rect().intersects(list->visualRect(list->currentIndex())));
    }
};

QTEST_MAIN(TestSimpleServerSheet)

#include "test_simple_server_sheet.moc"
