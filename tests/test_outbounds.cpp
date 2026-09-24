#include "include/configs/outbounds/http.h"
#include "include/configs/outbounds/masque.h"
#include "include/configs/outbounds/naive.h"
#include "include/configs/outbounds/trusttunnel.h"
#include "include/database/DatabaseManager.h"
#include "include/database/GroupsRepo.h"
#include "include/database/MarkersRepo.h"
#include "include/database/OtpProfilesRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/TrafficStatsRepo.h"
#include "include/global/Logger.hpp"

#include <QJsonArray>
#include <QTest>

void Logging::Write(Level, const QString &, const char *, int) {}

// Outbound generators need real settings, but no profile repositories or persistent database.
Configs::DatabaseManager::DatabaseManager(const std::string &path) : db(path), statsDb(":memory:") {
    settingsRepo = std::make_unique<SettingsRepo>(db);
}

class TestOutbounds : public QObject {
    Q_OBJECT

    std::unique_ptr<Configs::DatabaseManager> database;

private slots:
    void init() {
        database = std::make_unique<Configs::DatabaseManager>(":memory:");
        Configs::dataManager = database.get();
    }

    void cleanup() {
        Configs::dataManager = nullptr;
        database.reset();
    }

    void masqueJsonRoundTrip_data() {
        QTest::addColumn<int>("version");
        QTest::addColumn<bool>("disableFallback");
        QTest::newRow("automatic") << 0 << false;
        QTest::newRow("http2") << 2 << false;
        QTest::newRow("http3-only") << 3 << true;
    }

    void masqueJsonRoundTrip() {
        QFETCH(int, version);
        QFETCH(bool, disableFallback);
        const QJsonObject source{
            {"type", "masque"},
            {"server", "proxy.example"},
            {"server_port", 443},
            {"private_key", "test-private-key"},
            {"peer_public_key", "test-peer-key"},
            {"address", "198.51.100.2/32"},
            {"mtu", 1400},
            {"http_version", version},
            {"disable_version_fallback", disableFallback},
            {"initial_packet_size", 1250},
            {"tls", QJsonObject{{"enabled", true}, {"server_name", "proxy.example"}}},
        };
        Configs::masque imported;
        QVERIFY(imported.ParseFromJson(source));
        Configs::masque restored;
        QVERIFY(restored.ParseFromJson(imported.ExportToJson()));
        const auto result = restored.Build();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.object["type"].toString(), QString("masque"));
        QCOMPARE(result.object["server"].toString(), QString("proxy.example"));
        QCOMPARE(result.object["private_key"].toString(), QString("test-private-key"));
        QCOMPARE(result.object["peer_public_key"].toString(), QString("test-peer-key"));
        QCOMPARE(result.object["address"].toArray(), QJsonArray{"198.51.100.2/32"});
        QCOMPARE(result.object["http_version"].toInt(), version);
        QCOMPARE(result.object["disable_version_fallback"].toBool(), disableFallback);
        QCOMPARE(result.object["mtu"].toInt(), 1400);
        QCOMPARE(result.object["initial_packet_size"].toInt(), 1250);
        QCOMPARE(result.object["tls"].toObject()["server_name"].toString(), QString("proxy.example"));
        QVERIFY(restored.IsEndpoint());
    }

    void masqueClashRoundTrip_data() {
        QTest::addColumn<QString>("network");
        QTest::addColumn<int>("version");
        QTest::newRow("http2") << "h2" << 2;
        QTest::newRow("http3") << "h3" << 3;
    }

    void masqueClashRoundTrip() {
        QFETCH(QString, network);
        QFETCH(int, version);
        clash::Proxies source;
        source.type = "masque";
        source.server = "proxy.example";
        source.private_key = "test-private-key";
        source.public_key = "test-peer-key";
        source.ip = "198.51.100.2";
        source.ipv6 = "2001:db8::2";
        source.network = network.toStdString();
        source.sni = "proxy.example";
        Configs::masque imported;
        QVERIFY(imported.ParseFromClash(source));
        Configs::masque restored;
        QVERIFY(restored.ParseFromJson(imported.ExportToJson()));
        const auto result = restored.Build();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.object["server_port"].toInt(), 443);
        QCOMPARE(result.object["http_version"].toInt(), version);
        QCOMPARE(result.object["disable_version_fallback"].toBool(), version == 3);
        QCOMPARE(result.object["address"].toArray(), (QJsonArray{"198.51.100.2/32", "2001:db8::2/128"}));
    }

    void masqueRequiresIdentityAndSeparatesDevices() {
        Configs::masque outbound;
        outbound.server = "proxy.example";
        QVERIFY(!outbound.Build().error.isEmpty());
        outbound.private_key = "first-test-key";
        QVERIFY(!outbound.Build().error.isEmpty());
        outbound.address = {"198.51.100.2/32"};
        QVERIFY(outbound.Build().error.isEmpty());
        const auto first = outbound.ExportIdentity();
        outbound.private_key = "second-test-key";
        QVERIFY(first != outbound.ExportIdentity());
    }

    void naiveLinkKeepsCredentialsAndHeaders() {
        Configs::naive outbound;
        QVERIFY(outbound.ParseFromLink("naive+https://user@example.com:p@ss@proxy.example?extra-headers=X-Test%3Aa%2Bb+c&insecure-concurrency=4#Example"));
        QCOMPARE(outbound.username, QString("user@example.com"));
        QCOMPARE(outbound.password, QString("p@ss"));
        QCOMPARE(outbound.server_port, 443);
        Configs::naive restored;
        QVERIFY(restored.ParseFromLink(outbound.ExportToLink()));
        QCOMPARE(restored.username, outbound.username);
        QCOMPARE(restored.password, outbound.password);
        const auto result = restored.Build();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.object["extra_headers"].toObject()["X-Test"].toString(), QString("a+b c"));
        QCOMPARE(result.object["insecure_concurrency"].toInt(), 4);
    }

    void naiveFiltersUnsupportedTlsAndKeepsCustomFragment() {
        database->settingsRepo->skip_cert = true;
        database->settingsRepo->fragment_default_on = true;
        database->settingsRepo->fragment_implementation = "custom";
        Configs::naive outbound;
        QVERIFY(outbound.ParseFromJson({
            {"type", "naive"},
            {"server", "proxy.example"},
            {"server_port", 443},
            {"tcp_fast_open", true},
            {"tls", QJsonObject{{"enabled", true}, {"server_name", "proxy.example"}, {"insecure", true}, {"alpn", QJsonArray{"h2"}}, {"utls", QJsonObject{{"enabled", true}, {"fingerprint", "chrome"}}}}},
        }));
        const auto result = outbound.Build();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto tls = result.object["tls"].toObject();
        QVERIFY(tls["enabled"].toBool());
        QCOMPARE(tls["server_name"].toString(), QString("proxy.example"));
        QVERIFY(!tls.contains("insecure"));
        QVERIFY(!tls.contains("alpn"));
        QVERIFY(!tls.contains("utls"));
        QVERIFY(!tls.contains("fragment"));
        QVERIFY(result.object["tls_fragment"].toObject()["enabled"].toBool());
        QVERIFY(!result.object.contains("tcp_fast_open"));
    }

    void trustTunnelQuicFiltersIncompatibleTls() {
        Configs::trusttunnel outbound;
        QVERIFY(outbound.ParseFromJson({
            {"type", "trusttunnel"},
            {"server", "proxy.example"},
            {"quic", true},
            {"quic_congestion_control", "bbr"},
            {"tls", QJsonObject{{"enabled", true}, {"server_name", "proxy.example"}, {"utls", QJsonObject{{"enabled", true}, {"fingerprint", "chrome"}}}, {"reality", QJsonObject{{"enabled", true}, {"public_key", "test-peer-key"}}}}},
        }));
        Configs::trusttunnel restored;
        QVERIFY(restored.ParseFromJson(outbound.ExportToJson()));
        const auto result = restored.Build();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.object["quic_congestion_control"].toString(), QString("bbr"));
        QVERIFY(!result.object["tls"].toObject().contains("utls"));
        QVERIFY(!result.object["tls"].toObject().contains("reality"));
    }

    void httpUsesSchemeDefaultPort() {
        Configs::http plain;
        QVERIFY(plain.ParseFromLink("http://proxy.example"));
        QCOMPARE(plain.Build().object["server_port"].toInt(), 80);
        Configs::http secure;
        QVERIFY(secure.ParseFromLink("https://proxy.example"));
        QCOMPARE(secure.Build().object["server_port"].toInt(), 443);
    }
};

QTEST_GUILESS_MAIN(TestOutbounds)
#include "test_outbounds.moc"
