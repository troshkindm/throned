#include "include/ui/widget/SimpleRoutesPage.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/entities/RouteProfile.h"

#include <QAbstractButton>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QToolButton>

ThemeManager *themeManager() {
    static ThemeManager manager;
    return &manager;
}
ThronedThemeColors ThemeManager::Colors(const QString &) const { return {}; }
const ThronedSkin *ThemeManager::Skin(const QString &) const { return nullptr; }
void ThemeManager::RegisterStyle(QWidget *widget, const QString &style) const { widget->setStyleSheet(style); }
QColor ProfileRowDelegate::latencyColor(int, const ThronedThemeColors &) { return Qt::gray; }
namespace Configs {
std::shared_ptr<Profile> ProfilesRepo::GetProfile(int) const { return nullptr; }
} // namespace Configs
// The missing-outbound path logs; nothing here reaches it.
void MW_show_log(const QString &) {}

class TestSimpleRoutesPage : public QObject {
    Q_OBJECT
private slots:
    void applyingKeepsTheDefaultTarget_data() {
        QTest::addColumn<int>("outbound");
        QTest::newRow("direct") << int(Configs::directID);
        QTest::newRow("main VPN") << int(Configs::proxyID);
        QTest::newRow("named server") << 77;
        QTest::newRow("block") << int(Configs::blockID);
    }

    void applyingKeepsTheDefaultTarget() {
        QFETCH(int, outbound);
        SimpleRoutesPage page(nullptr);
        page.load({}, outbound, 0, true);
        page.addEntries({QStringLiteral("example.org")});
        int emitted = 0;
        int savedDefault = 0;
        QList<Configs::AppRoutes::Route> saved;
        connect(&page, &SimpleRoutesPage::applyRequested, &page, [&](const auto &routes, int rest) {
            ++emitted;
            saved = routes;
            savedDefault = rest;
        });
        QCOMPARE(emitted, 0);
        auto *apply = page.findChild<QPushButton *>(QStringLiteral("simpleRouteApply"));
        QVERIFY(apply);
        apply->click();
        QCOMPARE(emitted, 1);
        QCOMPARE(savedDefault, outbound);
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().domains, QStringList{QStringLiteral("example.org")});
    }

    void backgroundRefreshKeepsTheInputDraft() {
        SimpleRoutesPage page(nullptr);
        page.resize(420, 650);
        page.load({}, Configs::directID, 0, true);
        page.addEntries({QStringLiteral("example.org")});
        page.show();
        QCoreApplication::processEvents();
        auto *input = page.findChild<QLineEdit *>(QStringLiteral("simpleRouteAdd"));
        QVERIFY(input);
        input->setText(QStringLiteral("unfinished.example"));
        input->setCursorPosition(5);
        input->setFocus();
        page.setTargets({Configs::proxyID, QStringLiteral("Example main"), QStringLiteral("42 ms"), 42}, {});
        QCoreApplication::processEvents();
        input = page.findChild<QLineEdit *>(QStringLiteral("simpleRouteAdd"));
        QVERIFY(input);
        QCOMPARE(input->text(), QStringLiteral("unfinished.example"));
        QCOMPARE(input->cursorPosition(), 5);
        QVERIFY(input->hasFocus());
        QVERIFY(page.isDirty());
    }

    void backgroundRefreshKeepsTheScrollPosition() {
        SimpleRoutesPage page(nullptr);
        page.resize(420, 650);
        page.load({}, Configs::directID, 0, true);
        page.show();
        auto *more = page.findChild<QPushButton *>(QStringLiteral("simpleRouteMore"));
        QVERIFY(more);
        more->click();
        auto *area = qobject_cast<QScrollArea *>(page.scrollArea());
        QTRY_VERIFY(area->verticalScrollBar()->maximum() > 2000);
        area->verticalScrollBar()->setValue(1500);
        page.setTargets({Configs::proxyID, QStringLiteral("Example main"), QStringLiteral("42 ms"), 42}, {});
        QCoreApplication::processEvents();
        QCOMPARE(area->verticalScrollBar()->value(), 1500);
    }

    void disabledRulesCanBeEnabledByApplying() {
        SimpleRoutesPage page(nullptr);
        page.load({}, Configs::directID, 0, true, false);
        auto *apply = page.findChild<QPushButton *>(QStringLiteral("simpleRouteApply"));
        QVERIFY(page.isDirty());
        QVERIFY(apply->isEnabled());
        page.markApplied({}, Configs::directID);
        QVERIFY(!page.isDirty());
        QVERIFY(!apply->isEnabled());
    }

    void applyingAnEarlierSnapshotKeepsLaterEdits() {
        SimpleRoutesPage page(nullptr);
        page.load({}, Configs::directID, 0, true);
        page.addEntries({QStringLiteral("first.example")});
        const auto snapshot = page.stagedRoutes();
        page.addEntries({QStringLiteral("later.example")});
        page.markApplied(snapshot, Configs::directID);
        QVERIFY(page.isDirty());
        QCOMPARE(page.stagedRoutes().first().domains, QStringList({"first.example", "later.example"}));
    }

    void searchReturnsToTheFirstResult() {
        SimpleRoutesPage page(nullptr);
        page.resize(420, 650);
        page.show();
        page.findChild<QPushButton *>(QStringLiteral("simpleRouteMore"))->click();
        auto *area = qobject_cast<QScrollArea *>(page.scrollArea());
        QTRY_VERIFY(area->verticalScrollBar()->maximum() > 2000);
        area->verticalScrollBar()->setValue(1500);
        auto *search = page.findChild<QLineEdit *>(QStringLiteral("simpleRoutesSearch"));
        QVERIFY(search);
        search->setText(QStringLiteral("example.org"));
        QCoreApplication::processEvents();
        QCOMPARE(area->verticalScrollBar()->value(), 0);
    }

    void longTargetsAndPathsFitTheCompactViewport() {
        SimpleRoutesPage page(nullptr);
        page.resize(340, 600);
        page.load({}, 77, 1, true);
        page.setTargets({Configs::proxyID, QStringLiteral("Example main"), {}, 0},
                        {{77, QStringLiteral("A very long named server that must not widen the routing page"), {}, 0}});
        page.show();
        auto *area = qobject_cast<QScrollArea *>(page.scrollArea());
        QTRY_VERIFY(area->widget()->width() <= area->viewport()->width());
        auto *more = area->widget()->findChild<QPushButton *>(QStringLiteral("simpleRouteMore"));
        QVERIFY(more);
        QFont largeFont = more->font();
        largeFont.setPixelSize(24);
        more->setFont(largeFont);
        QCoreApplication::processEvents();
        QTRY_VERIFY(area->widget()->width() <= area->viewport()->width());
        page.addEntries({QStringLiteral("processPath:/opt/example/a-very-long-application-directory/another-directory/client")});
        QTRY_VERIFY(area->widget()->width() <= area->viewport()->width());
    }

    void longSearchActionsFitTheCompactViewport() {
        SimpleRoutesPage page(nullptr);
        page.resize(340, 600);
        page.load({}, Configs::directID, 0, true);
        page.show();
        auto *search = page.findChild<QLineEdit *>(QStringLiteral("simpleRoutesSearch"));
        const QString domain = QStringLiteral("a-very-long-domain-name.another-long-subdomain.example.org");
        search->setText(domain);
        auto *area = qobject_cast<QScrollArea *>(page.scrollArea());
        QTRY_VERIFY(area->widget()->width() <= area->viewport()->width());
        auto *add = area->widget()->findChild<QPushButton *>(QStringLiteral("simpleRouteAddRow"));
        QVERIFY(add);
        QVERIFY(add->text().contains(domain));
        QCOMPARE(add->toolTip(), add->text());
        add->click();
        QTRY_COMPARE(page.stagedRoutes().size(), 1);
        QCOMPARE(page.stagedRoutes().first().domains, QStringList{domain});
    }

    void embeddedHandsEachChangeOverAtOnce() {
        SimpleRoutesPage page(nullptr);
        page.setEmbedded(true);
        page.load({}, Configs::directID, 0, true, false, true);
        QList<Configs::AppRoutes::Route> handed;
        int handedDefault = 0;
        int emitted = 0;
        connect(&page, &SimpleRoutesPage::stagedChanged, &page, [&](const auto &routes, int rest) {
            ++emitted;
            handed = routes;
            handedDefault = rest;
        });
        QVERIFY(page.findChild<QPushButton *>(QStringLiteral("simpleRouteApply"))->isHidden());
        QVERIFY(page.findChild<QToolButton *>(QStringLiteral("simpleRoutesBack"))->isHidden());
        page.addEntries({QStringLiteral("example.org")});
        QCOMPARE(emitted, 1);
        QCOMPARE(handedDefault, int(Configs::directID));
        QCOMPARE(handed.size(), 1);
        QCOMPARE(handed.first().domains, QStringList{QStringLiteral("example.org")});
        // Refreshes that change nothing staged hand nothing over.
        page.setTargets({Configs::proxyID, QStringLiteral("Example main"), {}, 0}, {});
        page.setWholeComputer(false);
        QCOMPARE(emitted, 1);
        page.load({}, Configs::proxyID, 0, true, true, true);
        QCOMPARE(emitted, 1);
    }

    void switchingAnAppOnUpdatesItsRowInPlace() {
        SimpleRoutesPage page(nullptr);
        page.resize(420, 650);
        page.load({}, Configs::directID, 0, true);
        page.setDetected({{QStringLiteral("telegram"), true}});
        page.show();
        auto *toggle = page.findChild<QAbstractButton *>(QStringLiteral("simpleRouteToggle_telegram"));
        QVERIFY(toggle);
        toggle->click();
        QTRY_VERIFY(page.findChild<QLineEdit *>(QStringLiteral("simpleRouteAdd")) != nullptr);
        QCOMPARE(page.findChild<QAbstractButton *>(QStringLiteral("simpleRouteToggle_telegram")), toggle);
        QVERIFY(toggle->isChecked());
        QCOMPARE(page.stagedRoutes().size(), 1);
    }

    void rebuildingTheListNeverMovesTheScrollPosition() {
        SimpleRoutesPage page(nullptr);
        page.resize(420, 650);
        page.load({}, Configs::directID, 0, true);
        page.addEntries({QStringLiteral("processPath:/opt/example/client")});
        page.show();
        page.findChild<QPushButton *>(QStringLiteral("simpleRouteMore"))->click();
        auto *area = qobject_cast<QScrollArea *>(page.scrollArea());
        QTRY_VERIFY(area->verticalScrollBar()->maximum() > 2000);
        area->verticalScrollBar()->setValue(1500);
        int lowest = 1500;
        connect(area->verticalScrollBar(), &QScrollBar::valueChanged, &page, [&lowest](int value) { lowest = qMin(lowest, value); });
        // A warning banner above the list changes its structure, so the whole list is rebuilt.
        page.setWholeComputer(false);
        QTest::qWait(50);
        QCOMPARE(lowest, 1500);
        QCOMPARE(area->verticalScrollBar()->value(), 1500);
    }

    void interruptedCardAnimationsSettle() {
        auto *page = new SimpleRoutesPage(nullptr);
        page->setAnimated(true);
        page->resize(420, 650);
        page->load({}, Configs::directID, 0, true);
        page->show();
        auto *header = page->findChild<QAbstractButton *>(QStringLiteral("simpleRouteRow_custom"));
        QVERIFY(header);
        header->click();
        QTest::qWait(40);
        header->click();
        header->click();
        QTRY_VERIFY(page->findChild<QLineEdit *>(QStringLiteral("simpleRouteAdd")) != nullptr);
        header->click();
        // Gone with its card still closing: the animation goes with it and nothing fires afterwards.
        delete page;
        QTest::qWait(300);
    }

    void readOnlyProfilesCannotBeApplied() {
        SimpleRoutesPage page(nullptr);
        page.load({}, Configs::directID, 0, false);
        auto *apply = page.findChild<QPushButton *>(QStringLiteral("simpleRouteApply"));
        QVERIFY(apply);
        QVERIFY(!apply->isEnabled());
    }
};

QTEST_MAIN(TestSimpleRoutesPage)
#include "test_simple_routes_page.moc"
