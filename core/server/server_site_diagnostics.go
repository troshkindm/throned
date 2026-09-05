package main

import (
	"ThroneCore/gen"
	"ThroneCore/internal/boxbox"
	"ThroneCore/test_utils"
	"context"
	"fmt"
	"net"
	"net/url"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"
	"github.com/sagernet/sing/service"
)

func (s *server) DiagnoseSite(ctx context.Context, in *gen.DiagnoseSiteRequest) (*gen.DiagnoseSiteResponse, error) {
	box := currentBox()
	if box == nil {
		return nil, errInstanceNotRunning
	}
	manager := service.FromContext[adapter.OutboundManager](box.Context())
	if manager == nil {
		return nil, fmt.Errorf("outbound manager unavailable")
	}
	outbound := manager.Default()
	if tag := in.GetOutboundTag(); tag != "" {
		var found bool
		outbound, found = manager.Outbound(tag)
		if !found {
			return nil, fmt.Errorf("outbound %q is no longer available", tag)
		}
	}
	if outbound == nil {
		return nil, fmt.Errorf("default outbound unavailable")
	}
	// Keep the running instance's services and stop cancellation in the dial context.
	probeCtx, cancel := context.WithCancel(box.Context())
	defer cancel()
	stop := context.AfterFunc(ctx, cancel)
	defer stop()
	result := test_utils.DiagnoseSite(probeCtx, outbound.DialContext, in.GetUrl())
	result.OutboundTag = To(outbound.Tag())
	annotateDNS(ctx, box, in.GetUrl(), result)
	return result, nil
}

// Resolution runs beside the probe, never in front of it: the dial still hands the
// name to the outbound so the proxy resolves it exactly as it does for real traffic.
func annotateDNS(ctx context.Context, box *boxbox.Box, rawURL string, result *gen.DiagnoseSiteResponse) {
	target, err := url.Parse(rawURL)
	if err != nil || target.Hostname() == "" || net.ParseIP(target.Hostname()) != nil {
		return
	}
	var core test_utils.CoreResolver
	if router := service.FromContext[adapter.DNSRouter](box.Context()); router != nil {
		core = coreLookup{router: router}
	}
	comparison := test_utils.CompareDNS(ctx, core, test_utils.DefaultSystemResolver(), target.Hostname())
	result.DnsMs = To(comparison.CoreMs)
	result.DnsCore = comparison.Core
	result.DnsSystem = comparison.System
	result.DnsAgrees = To(comparison.Agrees)
	result.DnsCompared = To(!comparison.Skipped && comparison.Error == "")
}

func (s *server) PreviewRoute(ctx context.Context, in *gen.PreviewRouteRequest) (*gen.PreviewRouteResponse, error) {
	box := currentBox()
	if box == nil {
		return nil, errInstanceNotRunning
	}
	router := service.FromContext[adapter.Router](box.Context())
	outbounds := service.FromContext[adapter.OutboundManager](box.Context())
	if router == nil || outbounds == nil {
		return nil, fmt.Errorf("router unavailable")
	}
	target, err := url.Parse(in.GetUrl())
	if err != nil || target.Hostname() == "" {
		return &gen.PreviewRouteResponse{Error: To("invalid URL")}, nil
	}
	port := target.Port()
	if port == "" {
		port = "443"
		if target.Scheme == "http" {
			port = "80"
		}
	}
	network := in.GetNetwork()
	if network == "" {
		network = N.NetworkTCP
	}
	destination := M.ParseSocksaddr(net.JoinHostPort(target.Hostname(), port))
	metadata := adapter.InboundContext{Network: network, Destination: destination}
	if destination.IsFqdn() {
		metadata.Domain = destination.Fqdn
	}
	if path := in.GetProcessPath(); path != "" {
		metadata.ProcessInfo = &adapter.ConnectionOwner{ProcessPath: path}
	}
	preview := test_utils.PreviewRoute(router.Rules(), &metadata)
	outboundTag := preview.Outbound
	if preview.Action == "" || (preview.Action == C.RuleActionTypeRoute && outboundTag == "") {
		if fallback := outbounds.Default(); fallback != nil {
			outboundTag = fallback.Tag()
		}
	}
	return &gen.PreviewRouteResponse{
		OutboundTag:     To(outboundTag),
		MatchedRule:     To(preview.Rule),
		Action:          To(preview.Action),
		AddressResolved: To(!destination.IsFqdn()),
	}, nil
}
