#include "include/ui/widget/UpdateStatusWidget.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/WindowNotices.h"
#include "include/database/SettingsRepo.h"
#include "include/global/Logger.hpp"

#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

static bool micaAvailable = true;
void Logging::Write(Level, const QString &, const char *, int) {}
void PostPassiveWarning(const QString &, const QString &) {}

ThemeManager *themeManager() {
    static ThemeManager manager;
    return &manager;
}
ThronedThemeColors ThemeManager::Colors(const QString &) const { return {}; }
const ThronedSkin *ThemeManager::Skin(const QString &theme) const {
    static const ThronedSkin mica{.name = "Mica (Windows 11)"};
    return micaAvailable && (theme.isEmpty() ? current_theme : theme) == mica.name ? &mica : nullptr;
}
void ThemeManager::ApplyTheme(const QString &theme, bool) {
    current_theme = theme;
    emit themeChanged(theme);
}
QString ReadableSize(const qint64 &size) { return QLocale().formattedDataSize(size); }

class TestWindowNotices : public QObject {
    Q_OBJECT
private slots:
    void tipPersistsDismissalAndEnableAcrossSettingsReload() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Configs::Database db((dir.path() + "/settings.db").toStdString());
        Configs::SettingsRepo settings(db);
        themeManager()->ApplyTheme("Throned Midnight");
        {
            UpdateStatusWidget status;
            InstallWindowNotices(&status, settings);
            QTRY_COMPARE(status.activeNoticeId(), QString("windows11-mica-v1"));
            status.dismiss();
        }
        Configs::SettingsRepo reloaded(db);
        QVERIFY(reloaded.dismissed_notices.contains("windows11-mica-v1"));
        {
            UpdateStatusWidget status;
            InstallWindowNotices(&status, reloaded);
            QCoreApplication::processEvents();
            QCOMPARE(status.state(), UpdateStatusWidget::State::Hidden);
        }
        reloaded.dismissed_notices.clear();
        {
            UpdateStatusWidget status;
            InstallWindowNotices(&status, reloaded);
            QTRY_COMPARE(status.state(), UpdateStatusWidget::State::Notice);
            status.findChild<QPushButton *>("updatePrimaryButton")->click();
            QCOMPARE(status.state(), UpdateStatusWidget::State::Hidden);
            QCOMPARE(themeManager()->current_theme, QString("Mica (Windows 11)"));
        }
        Configs::SettingsRepo enabled(db);
        QCOMPARE(enabled.theme, QString("Mica (Windows 11)"));
        QVERIFY(enabled.dismissed_notices.contains("windows11-mica-v1"));
    }

    void unavailableSkinDoesNotOfferTip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Configs::Database db((dir.path() + "/settings.db").toStdString());
        Configs::SettingsRepo settings(db);
        micaAvailable = false;
        UpdateStatusWidget status;
        InstallWindowNotices(&status, settings);
        QCoreApplication::processEvents();
        QCOMPARE(status.state(), UpdateStatusWidget::State::Hidden);
        micaAvailable = true;
    }
    void updateKeepsSlotAndActionsUntilDismissed() {
        UpdateStatusWidget status;
        status.postNotice({"mica", "Try Mica", "Windows 11", "Enable"});
        status.showDownloading("Throned-1.4.3-windows64.zip", 50, 100);
        status.postNotice({"warning", "Warning", "Details", {}, {}, UpdateStatusWidget::Severity::Warning});
        QCOMPARE(status.state(), UpdateStatusWidget::State::Downloading);
        QVERIFY(status.activeNoticeId().isEmpty());
        QCOMPARE(status.findChild<QProgressBar *>()->value(), 500);
        status.showReady("Throned-1.4.3-windows64.zip");
        QSignalSpy restart(&status, &UpdateStatusWidget::restartRequested);
        QSignalSpy notice(&status, &UpdateStatusWidget::noticeActionRequested);
        status.findChild<QPushButton *>("updatePrimaryButton")->click();
        QCOMPARE(restart.size(), 1);
        QCOMPARE(notice.size(), 0);
        status.dismiss();
        QCOMPARE(status.activeNoticeId(), QString("warning"));
        QVERIFY(status.findChild<QPushButton *>("updatePrimaryButton")->isHidden());
        status.dismiss();
        QCOMPARE(status.activeNoticeId(), QString("mica"));
    }

    void replacementDismissalAndReentrantAction() {
        UpdateStatusWidget status;
        status.postNotice({"tip", "Old", {}, "Enable"});
        status.postNotice({"tip", "New", "<b>plain text</b>", "Enable"});
        QCOMPARE(status.findChild<QLabel *>("updateStatusTitle")->text(), QString("New"));
        QCOMPARE(status.findChild<QLabel *>("updateStatusDetail")->textFormat(), Qt::PlainText);
        QSignalSpy dismissed(&status, &UpdateStatusWidget::noticeDismissed);
        connect(&status, &UpdateStatusWidget::noticeActionRequested, &status, [&](const QString &id) {
            QCOMPARE(id, QString("tip"));
            status.removeNotice(id);
            status.postNotice({"next", "Next"});
        });
        status.findChild<QPushButton *>("updatePrimaryButton")->click();
        QCOMPARE(status.activeNoticeId(), QString("next"));
        QCOMPARE(dismissed.size(), 0);
        status.findChild<QPushButton *>("updateSecondaryButton")->click();
        QCOMPARE(dismissed.size(), 1);
        QCOMPARE(dismissed.first().first().toString(), QString("next"));
        QCOMPARE(status.state(), UpdateStatusWidget::State::Hidden);
    }

    void retryAndQueuedRemovalDoNotDismissError() {
        UpdateStatusWidget status;
        status.postNotice({"tip", "Tip"});
        status.showError("Offline");
        status.removeNotice("tip");
        QCOMPARE(status.state(), UpdateStatusWidget::State::Error);
        QSignalSpy retry(&status, &UpdateStatusWidget::retryRequested);
        status.findChild<QPushButton *>("updatePrimaryButton")->click();
        QCOMPARE(retry.size(), 1);
        status.dismiss();
        QVERIFY(status.isHidden());
    }

    void manualMicaSelectionRetiresTheTip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Configs::Database db((dir.path() + "/settings.db").toStdString());
        Configs::SettingsRepo settings(db);
        themeManager()->ApplyTheme("Throned Midnight");
        UpdateStatusWidget status;
        InstallWindowNotices(&status, settings);
        QTRY_COMPARE(status.state(), UpdateStatusWidget::State::Notice);
        themeManager()->ApplyTheme("Mica (Windows 11)");
        QCOMPARE(status.state(), UpdateStatusWidget::State::Hidden);
        themeManager()->ApplyTheme("Throned Graphite");
        QCOMPARE(status.state(), UpdateStatusWidget::State::Hidden);
        Configs::SettingsRepo reloaded(db);
        QVERIFY(reloaded.dismissed_notices.contains("windows11-mica-v1"));
    }
};

QTEST_MAIN(TestWindowNotices)
#include "test_window_notices.moc"
