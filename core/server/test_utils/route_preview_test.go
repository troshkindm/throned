package test_utils

import (
	"testing"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	R "github.com/sagernet/sing-box/route/rule"
	M "github.com/sagernet/sing/common/metadata"
)

type stubRule struct {
	description string
	action      adapter.RuleAction
	matches     func(*adapter.InboundContext) bool
	matchCalls  *int
}

func (r *stubRule) Match(metadata *adapter.InboundContext) bool {
	if r.matchCalls != nil {
		*r.matchCalls++
	}
	return r.matches(metadata)
}
func (r *stubRule) String() string             { return r.description }
func (r *stubRule) Type() string               { return "default" }
func (r *stubRule) Action() adapter.RuleAction { return r.action }
func (r *stubRule) Start() error               { return nil }
func (r *stubRule) Close() error               { return nil }

func always(*adapter.InboundContext) bool { return true }
func never(*adapter.InboundContext) bool  { return false }

func TestPreviewRouteStopsAtTheFirstTerminalRule(t *testing.T) {
	var calls int
	rules := []adapter.Rule{
		&stubRule{description: "domain=other.example", action: &R.RuleActionRoute{Outbound: "wrong"}, matches: never, matchCalls: &calls},
		&stubRule{description: "domain=example.org", action: &R.RuleActionRoute{Outbound: "chosen"}, matches: always, matchCalls: &calls},
		&stubRule{description: "never reached", action: &R.RuleActionRoute{Outbound: "later"}, matches: always, matchCalls: &calls},
	}
	preview := PreviewRoute(rules, &adapter.InboundContext{})
	if preview.Outbound != "chosen" || preview.Action != C.RuleActionTypeRoute || preview.Rule != "domain=example.org" {
		t.Fatalf("%+v", preview)
	}
	if calls != 2 {
		t.Fatalf("rules after the match were evaluated: %d calls", calls)
	}
}

func TestPreviewRouteSkipsNonTerminalActions(t *testing.T) {
	rules := []adapter.Rule{
		&stubRule{description: "sniff", action: &R.RuleActionSniff{}, matches: always},
		&stubRule{description: "bypass without outbound", action: &R.RuleActionBypass{}, matches: always},
		&stubRule{description: "domain=example.org", action: &R.RuleActionRoute{Outbound: "chosen"}, matches: always},
	}
	preview := PreviewRoute(rules, &adapter.InboundContext{})
	if preview.Outbound != "chosen" || preview.Rule != "domain=example.org" {
		t.Fatalf("%+v", preview)
	}
}

func TestPreviewRouteReportsRejectWithoutAnOutbound(t *testing.T) {
	rules := []adapter.Rule{
		&stubRule{description: "geosite=ads", action: &R.RuleActionReject{}, matches: always},
		&stubRule{description: "unreachable", action: &R.RuleActionRoute{Outbound: "proxy"}, matches: always},
	}
	preview := PreviewRoute(rules, &adapter.InboundContext{})
	if preview.Action != C.RuleActionTypeReject || preview.Outbound != "" || preview.Rule != "geosite=ads" {
		t.Fatalf("%+v", preview)
	}
}

func TestPreviewRouteFallsThroughWhenNothingMatches(t *testing.T) {
	rules := []adapter.Rule{
		&stubRule{description: "no", action: &R.RuleActionRoute{Outbound: "proxy"}, matches: never},
	}
	preview := PreviewRoute(rules, &adapter.InboundContext{})
	if preview != (RoutePreview{}) {
		t.Fatalf("%+v", preview)
	}
}

func TestPreviewRouteBypassWithOutboundIsTerminal(t *testing.T) {
	rules := []adapter.Rule{
		&stubRule{description: "ip_is_private", action: &R.RuleActionBypass{Outbound: "direct"}, matches: always},
		&stubRule{description: "unreachable", action: &R.RuleActionRoute{Outbound: "proxy"}, matches: always},
	}
	preview := PreviewRoute(rules, &adapter.InboundContext{})
	if preview.Action != C.RuleActionTypeBypass || preview.Outbound != "direct" {
		t.Fatalf("%+v", preview)
	}
}

// The rule cache carries per-rule IP-match state; the router clears it between
// rules and a preview that does not would leak one rule's result into the next.
func TestPreviewRouteResetsTheRuleCacheBetweenRules(t *testing.T) {
	seen := []bool{}
	rules := []adapter.Rule{
		&stubRule{description: "dirties the cache", action: &R.RuleActionSniff{}, matches: func(m *adapter.InboundContext) bool {
			m.DestinationAddressMatch = true
			return true
		}},
		&stubRule{description: "observer", action: &R.RuleActionRoute{Outbound: "chosen"}, matches: func(m *adapter.InboundContext) bool {
			seen = append(seen, m.DestinationAddressMatch)
			return true
		}},
	}
	metadata := adapter.InboundContext{Destination: M.ParseSocksaddr("example.org:443")}
	PreviewRoute(rules, &metadata)
	if len(seen) != 1 || seen[0] {
		t.Fatalf("rule cache leaked between rules: %v", seen)
	}
}
