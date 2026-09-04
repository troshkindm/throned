#include "include/ui/stats/dialog_site_reachability.h"
#include "include/ui/setting/ThemeManager.hpp"

#include <QLabel>
#include <QTableWidget>
#include <QTest>

// Use deterministic colours without loading app settings or a database.
ThemeManager *themeManager() {
    static ThemeManager manager;
    return &manager;
}
ThronedThemeColors ThemeManager::Colors(const QString &) const {
    ThronedThemeColors colors;
    colors.success = Qt::green;
    colors.warning = Qt::yellow;
    colors.danger = Qt::red;
    colors.textSubtle = Qt::gray;
    return colors;
}
void ThemeManager::RegisterStyle(QWidget *, const QString &) const {}

class TestSiteReachability : public QObject {
    Q_OBJECT
private slots:
    void coreFailureEndsWaiting() {
        SiteReachabilityDialog dialog;
        dialog.beginRun({{1, "Profile"}}, {"Google"});
        TestRunner::SiteReport report;
        report.errors.insert(1, "unknown method: SiteTest");
        dialog.applyReport(report);
        const auto *table = dialog.findChild<QTableWidget *>();
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Error"));
        QCOMPARE(table->item(0, 1)->toolTip(), report.errors.value(1));
        QVERIFY(dialog.findChild<QLabel *>()->text().contains(report.errors.value(1)));
    }
    void missingResultsAndSelectorsEndWaiting() {
        SiteReachabilityDialog dialog;
        dialog.beginRun({{1, "Missing"}, {2, "Auto"}}, {"Google"});
        TestRunner::SiteReport report;
        report.skipped << 2;
        dialog.applyReport(report);
        const auto *table = dialog.findChild<QTableWidget *>();
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Error"));
        QCOMPARE(table->item(1, 1)->text(), QStringLiteral("Skipped"));
        QVERIFY(table->item(1, 1)->toolTip().contains("Auto-selectors"));
    }
    void httpFailuresRemainDistinctFromTestFailures() {
        SiteReachabilityDialog dialog;
        dialog.beginRun({{1, "Profile"}}, {"Available", "Denied", "Silent"});
        TestRunner::SiteReport report;
        report.rows.insert(1, {{204, 12, {}}, {403, 15, {}}, {0, 0, "timeout"}});
        dialog.applyReport(report);
        const auto *table = dialog.findChild<QTableWidget *>();
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("12 ms"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("403"));
        QCOMPARE(table->item(0, 3)->text(), QStringLiteral("—"));
        QCOMPARE(table->item(0, 3)->toolTip(), QStringLiteral("timeout"));
        QVERIFY(dialog.findChild<QLabel *>()->text().contains("Checked: 1. Failed to run: 0."));
    }
    void cancellationExplainsMissingRows() {
        SiteReachabilityDialog dialog;
        dialog.beginRun({{1, "Profile"}}, {"Google"});
        TestRunner::SiteReport report;
        report.error = "Test cancelled.";
        dialog.applyReport(report);
        const auto *table = dialog.findChild<QTableWidget *>();
        QCOMPARE(table->item(0, 1)->toolTip(), report.error);
        QVERIFY(table->item(0, 1)->text() != QStringLiteral("…"));
    }
};

QTEST_MAIN(TestSiteReachability)
#include "test_site_reachability.moc"
