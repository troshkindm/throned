package main

import (
	"context"
	"fmt"
	"net/netip"
	"sync"
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

	domain := in.GetDnsProbeDomain()
	if domain == "" {
		domain = healthProbeDomain
	}
	var core test_utils.CoreResolver
	if router := service.FromContext[adapter.DNSRouter](box.Context()); router != nil {
		core = coreLookup{router: router}
	}

	def := outbounds.Default()
	var info test_utils.IPInfo
	var served time.Time
	var externalErr error
	var udp *test_utils.UDPTestResult
	var comparison test_utils.DNSComparison
	var probes sync.WaitGroup
	if def != nil {
		out.OutboundTag = To(def.Tag())
		probes.Add(2)
		go func() {
			defer probes.Done()
			client := test_utils.OutboundHTTPClient(ctx, def, healthTimeout)
			info, served, externalErr = test_utils.ExternalAddress(ctx, client)
		}()
		go func() {
			defer probes.Done()
			// A DNS round trip over UDP: the cheapest honest answer to "will calls work".
			udp = test_utils.ProbeUDP(ctx, def, "", 3, 0)
		}()
	}
	probes.Add(1)
	go func() {
		defer probes.Done()
		comparison = test_utils.CompareDNS(ctx, core, test_utils.DefaultSystemResolver(), domain)
	}()
	probes.Wait()

	if def != nil {
		if externalErr != nil {
			out.ExternalError = To(externalErr.Error())
		} else {
			out.ExternalIp = To(info.IP)
			out.ExternalCountry = To(info.CountryCode)
		}
		if !served.IsZero() {
			out.ClockKnown = To(true)
			out.ClockSkewMs = To(time.Since(served).Milliseconds())
		}
		out.UdpChecked = To(true)
		out.UdpOk = To(udp != nil && udp.Received > 0)
		if udp != nil && udp.Received > 0 {
			out.UdpRttMs = To(udp.Avg.Milliseconds())
		} else if udp != nil && udp.Error != nil {
			out.UdpError = To(udp.Error.Error())
		}
	}
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
