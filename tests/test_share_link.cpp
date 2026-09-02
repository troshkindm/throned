#include "include/global/ShareLinkB64.hpp"

#include <QTest>

class TestShareLink : public QObject {
    Q_OBJECT

private slots:
    void urlAlphabetRoundTrips();
    void standardAlphabetStillDecodes();
    void unpaddedInputDecodes();
    void garbageIsRejected();

private:
    // The last two bytes land on the two sextets the alphabets disagree on (62 and
    // 63) and leave one '=' of padding, so one payload exercises the whole gap. A
    // real profile link hits the same characters once it is more than a few bytes.
    static QByteArray payload() {
        return QByteArray("throne") + QByteArray::fromHex("fbf0");
    }
};

// The regression: ExportJsonLink() writes base64url, and decoding that as standard
// base64 aborted on the first '-' or '_', so the profile silently imported as nothing.
void TestShareLink::urlAlphabetRoundTrips() {
    const QString link = QString::fromLatin1(
        payload().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    QVERIFY(link.contains('-'));
    QVERIFY(link.contains('_'));
    QCOMPARE(DecodeShareLinkB64(link), payload());
}

void TestShareLink::standardAlphabetStillDecodes() {
    const QString link = QString::fromLatin1(payload().toBase64());

    QVERIFY(link.contains('+'));
    QVERIFY(link.contains('/'));
    QCOMPARE(DecodeShareLinkB64(link), payload());
}

void TestShareLink::unpaddedInputDecodes() {
    const QString padded = QString::fromLatin1(payload().toBase64());
    const QString unpadded = QString::fromLatin1(payload().toBase64(QByteArray::OmitTrailingEquals));

    QVERIFY(padded.endsWith('='));
    QVERIFY(!unpadded.endsWith('='));
    QCOMPARE(DecodeShareLinkB64(padded), payload());
    QCOMPARE(DecodeShareLinkB64(unpadded), payload());
}

void TestShareLink::garbageIsRejected() {
    QVERIFY(DecodeShareLinkB64("not base64 at all!").isEmpty());
    // Mixing the two alphabets is valid under neither.
    QVERIFY(DecodeShareLinkB64("ab+cd_ef").isEmpty());
}

QTEST_GUILESS_MAIN(TestShareLink)
#include "test_share_link.moc"
