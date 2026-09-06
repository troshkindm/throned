#include "include/database/entities/RouteRule.h"
#include "include/database/ProfilesRepo.h"

#include <QJsonDocument>
#include <QTest>

// Link seam: get_rule_json only consults the repo for profile-backed outbounds,
// which none of these rules use.
namespace Configs {
std::shared_ptr<Profile> ProfilesRepo::GetProfile(int) const { return nullptr; }
} // namespace Configs

using Configs::RouteRule;

class TestRouteRule : public QObject {
    Q_OBJECT

private slots:
    void rejectUsesTheCoresMethodKey();
    void spoofNeedsItsSNI();
    void spoofRidesOnRouteOptionsOnly();
    void onlyOneFragmentModeIsEmitted();
    void fallbackDelayFollowsFragment();
};

void TestRouteRule::rejectUsesTheCoresMethodKey() {
    RouteRule rule;
    rule.action = "reject";
    rule.rejectMethod = "drop";
    const auto json = rule.get_rule_json();
    QVERIFY(json.contains("method"));
    QCOMPARE(json.value("method").toString(), QStringLiteral("drop"));
    QVERIFY(!json.contains("reject_method"));
}

void TestRouteRule::spoofNeedsItsSNI() {
    RouteRule rule;
    rule.action = "route-options";
    rule.tls_spoof_method = "wrong-ack";
    QVERIFY(!rule.get_rule_json().contains("tls_spoof_method"));

    rule.tls_spoof = "api-maps.yandex.ru";
    const auto json = rule.get_rule_json();
    QCOMPARE(json.value("tls_spoof").toString(), QStringLiteral("api-maps.yandex.ru"));
    QCOMPARE(json.value("tls_spoof_method").toString(), QStringLiteral("wrong-ack"));
}

void TestRouteRule::spoofRidesOnRouteOptionsOnly() {
    RouteRule rule;
    rule.action = "route";
    rule.outboundID = Configs::directID;
    rule.tls_spoof = "api-maps.yandex.ru";
    rule.tls_fragment = true;
    const auto json = rule.get_rule_json();
    QVERIFY(!json.contains("tls_spoof"));
    QVERIFY(!json.contains("tls_fragment"));
}

void TestRouteRule::onlyOneFragmentModeIsEmitted() {
    RouteRule rule;
    rule.action = "route-options";
    rule.tls_fragment = true;
    rule.tls_record_fragment = true;
    const auto json = rule.get_rule_json();
    QVERIFY(json.value("tls_fragment").toBool());
    QVERIFY(!json.contains("tls_record_fragment"));
}

void TestRouteRule::fallbackDelayFollowsFragment() {
    RouteRule rule;
    rule.action = "route-options";
    rule.tls_fragment_fallback_delay = "500ms";
    QVERIFY(!rule.get_rule_json().contains("tls_fragment_fallback_delay"));

    rule.tls_fragment = true;
    QCOMPARE(rule.get_rule_json().value("tls_fragment_fallback_delay").toString(), QStringLiteral("500ms"));
}

QTEST_GUILESS_MAIN(TestRouteRule)
#include "test_route_rule.moc"
