#pragma once
#include <climits>
#include <utility>

#include "include/database/entities/RouteRule.h"
#include <QUrl>
#include <QJsonArray>
#include <QStringList>

namespace Configs {
const int INVALID_ID = -99999;

// The "route local proxy traffic through proxy" quick option is stored as an
// ordinary rule carrying this marker name, so it survives sharing and raw
// editing like any other rule.
inline constexpr auto LocalProxyRuleName = "throned-local-proxy-traffic";

// True for that quick option's rule. It is the one profile rule that keeps
// applying when a profile's own rules are switched off, because it decides
// where the client's own local inbounds go rather than matching user traffic.
bool IsLocalProxyTrafficRule(const std::shared_ptr<RouteRule>& rule);

enum simpleAction { bypass,
                    block,
                    proxy,
                    warpBypass,
                    viaProfile };
inline QString simpleActionToString(simpleAction action) {
    if (action == bypass) return {"direct"};
    if (action == block) return {"block"};
    if (action == proxy) return {"proxy"};
    if (action == warpBypass) return {"warp-bypass"};
    return {"invalid"};
}

// One free-form line to a canonical "kind:value" rule, or empty when the
// line cannot be placed. Accepts the typed prefixes, their sing-box
// spellings, and bare values whose kind is unambiguous.
QString NormalizeRuleLine(const QString& line);

// A canonical "kind:value" line split into its two halves, both trimmed, or an empty kind when there is no usable separator.
std::pair<QString, QString> SplitRuleLine(const QString& line);

class RouteProfile {
public:
    int id = -1;
    QString name = "";
    QList<std::shared_ptr<RouteRule>> Rules;
    int defaultOutboundID = proxyID;

    // When false the profile's own rules are skipped while generating the
    // config, so everything falls through to defaultOutboundID. Throned's
    // injected plumbing (tun hijack, sniffing, peer guard, bridges) and the
    // local-proxy quick option are unaffected. Raw profiles ignore this.
    bool applyProfileRules = true;

    // Raw profiles carry a full sing-box `route` JSON object (as text) instead of
    // structured Rules. When preventModifications is set we use it verbatim (after
    // outbound-id translation); otherwise Throne still injects its internal plumbing.
    bool isRaw = false;
    QString rawRoute = "";
    bool preventModifications = false;

    // Remote profiles stay structured and user-editable; an update re-fetches remoteURL and overwrites Rules, keeping the local name.
    bool isRemote = false;
    QString remoteURL = "";
    bool autoUpdate = false;
    qint64 remoteLastUpdate = 0; // epoch seconds

    // Profile ids of openvpn/openconnect profiles run alongside this routing profile.
    QList<int> endpointProfileIDs;

    RouteProfile() = default;

    RouteProfile(const RouteProfile& other);

    static QList<std::shared_ptr<RouteRule>> parseJsonArray(const QJsonArray& arr, QString* parseError, QString* warnings = nullptr);

    QJsonArray get_route_rules(bool forView = false, std::map<int, QString> outboundMap = {});

    // Endpoints travel as whole configs with credentials cleared; one that cannot is skipped into *warnings.
    QJsonObject ToShareObject(QString* warnings = nullptr);
    // ToShareObject() compacted, base64url-encoded, wrapped as throne://route/<...>
    QString ToShareLink(QString* warnings = nullptr);
    // *wasOldArray = legacy bare rule array (no name / default outbound); materializeEndpoints=false matches shared endpoints without creating local ones.
    static std::shared_ptr<RouteProfile> FromShareInput(const QString& input, QString* fatalError, QString* warnings, bool* wasOldArray, bool materializeEndpoints = true);

    // *wasRemoteRouteLink is true even when the payload is invalid; a non-remoteRoute input returns {} with it false so callers can fall through.
    static QList<std::shared_ptr<RouteProfile>> FromRemoteRoutesLink(const QString& input, bool* wasRemoteRouteLink, QString* error);

    static QList<int> CollectRawOutboundIds(const QJsonObject& route);
    static QJsonObject TranslateRawOutbounds(const QJsonObject& route, const std::map<int, QString>& outboundMap);

    static std::shared_ptr<RouteProfile> GetDefaultChain();

    // The positional placeholder paired with an endpoint, correlated by type + outboundID.
    static std::shared_ptr<RouteRule> MakeEndpointRule(int endpointProfileID);

    // One endpointPreferredBy rule per listed endpoint; prunes orphans, appends missing. Raw: no-op.
    void SyncEndpointRules();

    std::shared_ptr<QList<int>> get_used_outbounds();

    std::shared_ptr<QStringList> get_used_rule_sets();

    QStringList get_direct_sites();

    QStringList get_proxy_sites();

    // Same collection keyed by any outbound, including a profile a rule aims at.
    QStringList get_sites(int outbound);

    struct ProcessSelectors {
        QStringList names;
        QStringList paths;
        QStringList pathRegexes;
        [[nodiscard]] bool isEmpty() const {
            return names.isEmpty() && paths.isEmpty() && pathRegexes.isEmpty();
        }
    };
    [[nodiscard]] ProcessSelectors get_process_selectors(int outbound) const;

    QStringList get_direct_ips();

    // CIDRs pulled away from a direct exit, so Tun knows which ranges it must carry; rule-sets aren't resolvable at build time and are left out.
    QStringList get_hijacked_ips();

    // True when any rule matches on the owning process. sing-box only looks
    // that up when route.find_process is on, so a profile that uses these
    // rules has to ask for it or they silently never match.
    bool UsesProcessRules() const;

    bool IsEmpty();

    void ResetRules();

    // Buckets that carry a target match on it too, so one profile's rules are
    // rewritten without touching another's.
    static constexpr int anyOutbound = INT_MIN;

    void ResetSimpleRule(ruleType type, int outbound = anyOutbound);

    QString GetSimpleRules(simpleAction action, int outbound = anyOutbound);

    QString UpdateSimpleRules(const QString& content, simpleAction action, int outbound = anyOutbound);

    static QList<ruleType> simple_rule_types(simpleAction action);

    // Via-profile buckets are keyed by the profile they aim at: the same three
    // rule types repeat once per chosen profile, so the choice persists with
    // the rules and needs no column of its own. In menu order, without dupes.
    [[nodiscard]] QList<int> GetSimpleViaProfileIDs() const;
    void RemoveSimpleViaProfile(int profileID);

    void FilterEmptyRules();

private:
    static bool add_simple_rule(const QString& content, const std::shared_ptr<RouteRule>& rule, ruleType type);

    static bool add_simple_address_rule(const QString& content, const std::shared_ptr<RouteRule>& rule);

    static bool add_simple_process_rule(const QString& content, const std::shared_ptr<RouteRule>& rule);

    std::shared_ptr<RouteRule> get_simple_rule_by_type(ruleType type, int outbound = anyOutbound);

    static ruleType get_rule_type(const QString& content, simpleAction action);

    static QList<std::shared_ptr<RouteRule>> get_simple_rules();

    static void reset_simple_rule(std::shared_ptr<RouteRule>& rule);
};
} // namespace Configs
