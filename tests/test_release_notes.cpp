#include "include/global/ReleaseNotes.hpp"

#include <QTest>

using ReleaseNotes::LocalizedMarkdown;

class TestReleaseNotes : public QObject {
    Q_OBJECT

private slots:
    void keepsLegacyReleaseBodiesUnchanged();
    void selectsRussianInsideGithubDetails();
    void normalizesRegionalLocaleNames();
    void fallsBackToEnglish();
    void fallsBackToFirstAvailableLanguage();
    void skipsEmptyLanguageBlocks();
    void keepsMalformedMarkedBodiesVisible();
    void joinsRepeatedLanguageBlocks();
};

void TestReleaseNotes::keepsLegacyReleaseBodiesUnchanged() {
    const QString body = QStringLiteral("## Changes\n\n- Existing release note");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("ru_RU")), body);
}

void TestReleaseNotes::selectsRussianInsideGithubDetails() {
    const QString body = QStringLiteral(
        "<!-- throned:lang=en -->\n"
        "## Changes\n\n- English text\n"
        "<!-- throned:lang=end -->\n\n"
        "<details>\n<summary>Русский</summary>\n\n"
        "<!-- throned:lang=ru -->\n"
        "## Изменения\n\n- Русский текст\n"
        "<!-- throned:lang=end -->\n"
        "</details>");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("ru_RU")),
             QStringLiteral("## Изменения\n\n- Русский текст"));
}

void TestReleaseNotes::normalizesRegionalLocaleNames() {
    const QString body = QStringLiteral(
        "<!-- THRONED:LANG=zh_Hans -->\n简体中文\n<!-- throned:lang=end -->\n"
        "<!-- throned:lang=en -->\nEnglish\n<!-- throned:lang=end -->");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("zh_Hans_CN.UTF-8")), QStringLiteral("简体中文"));
}

void TestReleaseNotes::fallsBackToEnglish() {
    const QString body = QStringLiteral(
        "<!-- throned:lang=ru -->\nРусский\n<!-- throned:lang=end -->\n"
        "<!-- throned:lang=en -->\nEnglish\n<!-- throned:lang=end -->");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("fa_IR")), QStringLiteral("English"));
}

void TestReleaseNotes::fallsBackToFirstAvailableLanguage() {
    const QString body = QStringLiteral(
        "<!-- throned:lang=de -->\nDeutsch\n<!-- throned:lang=end -->\n"
        "<!-- throned:lang=ru -->\nРусский\n<!-- throned:lang=end -->");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("fa_IR")), QStringLiteral("Deutsch"));
}

void TestReleaseNotes::skipsEmptyLanguageBlocks() {
    const QString body = QStringLiteral(
        "<!-- throned:lang=de -->\n\n<!-- throned:lang=end -->\n"
        "<!-- throned:lang=ru -->\nРусский\n<!-- throned:lang=end -->");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("fa_IR")), QStringLiteral("Русский"));
}

void TestReleaseNotes::keepsMalformedMarkedBodiesVisible() {
    const QString body = QStringLiteral("<!-- throned:lang=ru -->\nНезакрытый раздел");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("ru_RU")), body);
}

void TestReleaseNotes::joinsRepeatedLanguageBlocks() {
    const QString body = QStringLiteral(
        "<!-- throned:lang=en -->\nFirst\n<!-- throned:lang=end -->\n"
        "<!-- throned:lang=en -->\nSecond\n<!-- throned:lang=end -->");
    QCOMPARE(LocalizedMarkdown(body, QStringLiteral("en_US")), QStringLiteral("First\n\nSecond"));
}

QTEST_GUILESS_MAIN(TestReleaseNotes)

#include "test_release_notes.moc"
