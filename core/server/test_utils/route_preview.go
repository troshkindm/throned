package test_utils

import (
	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	R "github.com/sagernet/sing-box/route/rule"
)

type RoutePreview struct {
	Rule     string // rule.String(); empty when nothing matched
	Action   string // route, bypass, reject, hijack-dns; empty when nothing matched
	Outbound string // tag the action names, empty for reject and hijack-dns
}

// PreviewRoute replays the router's terminal decisions over the same rule list in
// the same order, but performs none of matchRule's side effects: no sniffing, no
// DNS resolve, no metadata overrides. A rule whose action is not terminal (sniff,
// resolve, route-options) is passed over exactly as the router passes over it.
func PreviewRoute(rules []adapter.Rule, metadata *adapter.InboundContext) RoutePreview {
	for _, rule := range rules {
		metadata.ResetRuleCache()
		if !rule.Match(metadata) {
			continue
		}
		action := rule.Action()
		switch action.Type() {
		case C.RuleActionTypeRoute:
			outbound := ""
			if route, ok := action.(*R.RuleActionRoute); ok {
				outbound = route.Outbound
			}
			return RoutePreview{Rule: rule.String(), Action: C.RuleActionTypeRoute, Outbound: outbound}
		case C.RuleActionTypeBypass:
			// A bypass without an outbound falls through to the following rules.
			bypass, ok := action.(*R.RuleActionBypass)
			if !ok || bypass.Outbound == "" {
				continue
			}
			return RoutePreview{Rule: rule.String(), Action: C.RuleActionTypeBypass, Outbound: bypass.Outbound}
		case C.RuleActionTypeReject, C.RuleActionTypeHijackDNS:
			return RoutePreview{Rule: rule.String(), Action: action.Type()}
		}
	}
	return RoutePreview{}
}
