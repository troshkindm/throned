package main

import (
	"context"
	"fmt"
	"net/netip"
	"time"

	"ThroneCore/gen"
	"ThroneCore/test_utils"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing/service"
)

// coreLookup adapts sing-box's DNS router to the narrow resolver the comparison wants.
type coreLookup struct {
	router adapter.DNSRouter
}

func (c coreLookup) Lookup(ctx context.Context, domain string) ([]netip.Addr, error) {
	return c.router.Lookup(ctx, domain, adapter.DNSQueryOptions{})
}

const healthProbeDomain = "example.org"
const healthTimeout = 8 * time.Second

// Health answers the questions about a running instance that cannot be answered
// without touching the network: what the world sees, whether DNS leaves the tunnel,
// and how far the system clock has drifted from real time.
func (s *server) Health(ctx context.Context, in *gen.HealthRequest) (*gen.HealthResponse, error) {
	box := currentBox()
	if box == nil {
		return nil, errInstanceNotRunning
	}
	outbounds := service.FromContext[adapter.OutboundManager](box.Context())
	if outbounds == nil {
		return nil, fmt.Errorf("outbound manager unavailable")
	}
	out := &gen.HealthResponse{}
	for _, outbound := range outbounds.Outbounds() {
		if tag := outbound.Tag(); tag != "" {
			out.Outbounds = append(out.Outbounds, tag)
		}
	}

	ctx, cancel := context.WithTimeout(ctx, healthTimeout)
	defer cancel()

	if def := outbounds.Default(); def != nil {
		out.OutboundTag = To(def.Tag())
		client := test_utils.OutboundHTTPClient(box.Context(), def, healthTimeout)
		info, served, err := test_utils.ExternalAddress(ctx, client)
		if err != nil {
			out.ExternalError = To(err.Error())
		} else {
			out.ExternalIp = To(info.IP)
			out.ExternalCountry = To(info.CountryCode)
		}
		if !served.IsZero() {
			out.ClockKnown = To(true)
			out.ClockSkewMs = To(time.Since(served).Milliseconds())
		}
		// A DNS round trip over UDP: the cheapest honest answer to "will calls work".
		udp := test_utils.ProbeUDP(ctx, def, "", 3, 0)
		out.UdpChecked = To(true)
		out.UdpOk = To(udp.Received > 0)
		if udp.Received > 0 {
			out.UdpRttMs = To(udp.Avg.Milliseconds())
		} else if udp.Error != nil {
			out.UdpError = To(udp.Error.Error())
		}
	}

	domain := in.GetDnsProbeDomain()
	if domain == "" {
		domain = healthProbeDomain
	}
	var core test_utils.CoreResolver
	if router := service.FromContext[adapter.DNSRouter](box.Context()); router != nil {
		core = coreLookup{router: router}
	}
	comparison := test_utils.CompareDNS(ctx, core, test_utils.DefaultSystemResolver(), domain)
	out.DnsDomain = To(comparison.Domain)
	out.DnsSystem = comparison.System
	out.DnsCore = comparison.Core
	out.DnsAgrees = To(comparison.Agrees)
	out.DnsCompared = To(!comparison.Skipped && comparison.Error == "")
	if comparison.Error != "" {
		out.DnsError = To(comparison.Error)
	}
	return out, nil
}
