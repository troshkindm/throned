#include "include/ui/widget/SimpleRoutesPage.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/entities/RouteProfile.h"

#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>

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
