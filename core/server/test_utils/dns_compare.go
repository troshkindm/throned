package test_utils

import (
	"context"
	"net"
	"net/netip"
	"sort"
	"time"
)

// CoreResolver is the slice of sing-box's DNS router this comparison needs, named
// separately so the comparison can be tested without standing up a box.
type CoreResolver interface {
	Lookup(ctx context.Context, domain string) ([]netip.Addr, error)
}

// SystemResolver is net.DefaultResolver in production. It answers through whatever
// the operating system is configured to use, which is the whole point: when TUN is
// up the two answers must agree, and when they disagree queries are leaving the tunnel.
type SystemResolver interface {
	LookupNetIP(ctx context.Context, network, host string) ([]netip.Addr, error)
}

type DNSComparison struct {
	Domain  string
	System  []string
	Core    []string
	CoreMs  int64
	Agrees  bool
	Error   string
	Skipped bool // no resolver was available, so nothing was compared
}

const dnsCompareTimeout = 4 * time.Second

// CompareDNS resolves one name both ways and reports whether the answers overlap.
// Overlap rather than equality: a CDN legitimately returns different subsets of a
// large address pool to two resolvers, and only a disjoint pair means a real split.
func CompareDNS(ctx context.Context, core CoreResolver, system SystemResolver, domain string) DNSComparison {
	result := DNSComparison{Domain: domain}
	if domain == "" || (core == nil && system == nil) {
		result.Skipped = true
		return result
	}
	ctx, cancel := context.WithTimeout(ctx, dnsCompareTimeout)
	defer cancel()

	if system != nil {
		if addrs, err := system.LookupNetIP(ctx, "ip", domain); err == nil {
			result.System = sortedStrings(addrs)
		} else {
			result.Error = "система: " + err.Error()
		}
	}
	if core != nil {
		begin := time.Now()
		addrs, err := core.Lookup(ctx, domain)
		result.CoreMs = time.Since(begin).Milliseconds()
		if err == nil {
			result.Core = sortedStrings(addrs)
		} else if result.Error == "" {
			result.Error = "ядро: " + err.Error()
		}
	}
	if len(result.System) == 0 || len(result.Core) == 0 {
		result.Skipped = result.Error == ""
		return result
	}
	seen := make(map[string]bool, len(result.Core))
	for _, addr := range result.Core {
		seen[addr] = true
	}
	for _, addr := range result.System {
		if seen[addr] {
			result.Agrees = true
			break
		}
	}
	return result
}

func sortedStrings(addrs []netip.Addr) []string {
	out := make([]string, 0, len(addrs))
	for _, addr := range addrs {
		out = append(out, addr.Unmap().String())
	}
	sort.Strings(out)
	return out
}

// DefaultSystemResolver is separate so a test can supply its own.
func DefaultSystemResolver() SystemResolver { return net.DefaultResolver }
