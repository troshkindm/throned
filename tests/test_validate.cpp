#include "include/configs/validate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using Configs::FindDanglingReferences;

namespace {
QJsonObject parse(const char *json) {
    return QJsonDocument::fromJson(QByteArray(json)).object();
}
} // namespace

class TestValidate : public QObject {
    Q_OBJECT

private slots:
    void wholeConfigIsClean();
    void dnsRuleWithoutItsServer();
    void dnsServerDetouringNowhere();
    void routeRuleKeptAsRawID();
    void chainDnsResolvesThroughItsChain();
};

void TestValidate::wholeConfigIsClean() {
    const auto config = parse(R"({
        "dns": {
            "servers": [
                {"tag": "dns-remote", "type": "https", "server": "8.8.8.8", "detour": "proxy",
                 "domain_resolver": "dns-local"},
                {"tag": "dns-local", "type": "local"}
            ],
            "rules": [{"action": "route", "server": "dns-remote"}]
        },
        "outbounds": [{"tag": "proxy", "type": "vless"}, {"tag": "direct", "type": "direct"}],
        "route": {
            "rules": [{"action": "route", "outbound": "direct"}],
            "final": "proxy",
            "default_domain_resolver": {"server": "dns-local"}
        }
    })");
    QVERIFY(FindDanglingReferences(config).isEmpty());
}

// The regression this checker exists for: test configs never build dns-remote,
// yet the final rule kept routing to it, and every lookup died as a bare timeout.
void TestValidate::dnsRuleWithoutItsServer() {
    const auto config = parse(R"({
        "dns": {
            "servers": [{"tag": "dns-direct", "type": "local"}],
            "rules": [
                {"action": "route", "domain": ["example.com"], "server": "dns-direct"},
                {"action": "route", "server": "dns-remote"}
            ]
        },
        "outbounds": [{"tag": "proxy", "type": "vless"}]
    })");
    const auto problems = FindDanglingReferences(config);
    QCOMPARE(problems.size(), 1);
    QVERIFY(problems.first().contains("dns rule[1]"));
    QVERIFY(problems.first().contains("dns-remote"));
}

void TestValidate::dnsServerDetouringNowhere() {
    const auto config = parse(R"({
        "dns": {
            "servers": [{"tag": "dns-remote", "type": "udp", "server": "8.8.8.8",
                         "detour": "route-2", "domain_resolver": "dns-local"}],
            "rules": []
        },
        "outbounds": [{"tag": "proxy", "type": "vless"}]
    })");
    const auto problems = FindDanglingReferences(config);
    QCOMPARE(problems.size(), 2);
    QVERIFY(problems.at(0).contains("route-2"));
    QVERIFY(problems.at(1).contains("dns-local"));
}

// A rule aimed at a profile whose id never reached outboundMap serialises as a
// number instead of a tag, which the core rejects just like an unknown tag.
void TestValidate::routeRuleKeptAsRawID() {
    const auto config = parse(R"({
        "outbounds": [{"tag": "proxy", "type": "vless"}],
        "route": {"rules": [{"action": "route", "process_name": ["chrome.exe"], "outbound": 17}]}
    })");
    const auto problems = FindDanglingReferences(config);
    QCOMPARE(problems.size(), 1);
    QVERIFY(problems.first().contains("unmapped outbound id 17"));
}

// Shape of what per-profile routing generates: the chain, a remote server
// detoured through it, and the rule that sends that app's lookups there.
void TestValidate::chainDnsResolvesThroughItsChain() {
    const auto config = parse(R"({
        "dns": {
            "servers": [
                {"tag": "dns-remote", "type": "https", "server": "8.8.8.8", "detour": "proxy"},
                {"tag": "dns-remote-route-1", "type": "https", "server": "8.8.8.8", "detour": "route-1"},
                {"tag": "dns-local", "type": "local"}
            ],
            "rules": [
                {"action": "route", "process_name": ["chrome.exe"], "server": "dns-remote-route-1"},
                {"action": "route", "server": "dns-remote"}
            ]
        },
        "outbounds": [
            {"tag": "proxy", "type": "vless"},
            {"tag": "route-1", "type": "vless"},
            {"tag": "direct", "type": "direct"}
        ],
        "route": {
            "rules": [{"action": "route", "process_name": ["chrome.exe"], "outbound": "route-1"}],
            "final": "proxy"
        }
    })");
    QVERIFY(FindDanglingReferences(config).isEmpty());
}

QTEST_GUILESS_MAIN(TestValidate)
#include "test_validate.moc"
