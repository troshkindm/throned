package test_utils

import (
	"context"
	"errors"
	"net/netip"
	"strings"
	"testing"
	"time"
)

type stubCore struct {
	addrs []string
	err   error
}

func (s stubCore) Lookup(context.Context, string) ([]netip.Addr, error) {
	if s.err != nil {
		return nil, s.err
	}
	return parseAll(s.addrs), nil
}

type stubSystem struct {
	addrs []string
	err   error
}

type contextCore struct{}

func (contextCore) Lookup(ctx context.Context, _ string) ([]netip.Addr, error) {
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	default:
		return parseAll([]string{"1.1.1.1"}), nil
	}
}

type blockedSystem struct{}

func (blockedSystem) LookupNetIP(ctx context.Context, _, _ string) ([]netip.Addr, error) {
	<-ctx.Done()
	return nil, ctx.Err()
}

func (s stubSystem) LookupNetIP(context.Context, string, string) ([]netip.Addr, error) {
	if s.err != nil {
		return nil, s.err
	}
	return parseAll(s.addrs), nil
}

func parseAll(in []string) []netip.Addr {
	out := make([]netip.Addr, 0, len(in))
	for _, raw := range in {
		out = append(out, netip.MustParseAddr(raw))
	}
	return out
}

func TestCompareDNSAgreesOnAnyOverlap(t *testing.T) {
	// A CDN hands each resolver a different slice of the same pool; one shared
	// address is enough to prove the queries are not going to different places.
	result := CompareDNS(context.Background(),
		stubCore{addrs: []string{"104.18.32.47", "172.64.155.209"}},
		stubSystem{addrs: []string{"172.64.155.209"}}, "example.org")
	if !result.Agrees || result.Error != "" || result.Skipped {
		t.Fatalf("%+v", result)
	}
}

func TestCompareDNSFlagsDisjointAnswers(t *testing.T) {
	result := CompareDNS(context.Background(),
		stubCore{addrs: []string{"172.64.155.209"}},
		stubSystem{addrs: []string{"10.0.0.5"}}, "example.org")
	if result.Agrees || result.Skipped {
		t.Fatalf("a split answer was not flagged: %+v", result)
	}
	if len(result.Core) != 1 || len(result.System) != 1 {
		t.Fatalf("both answers must be reported so the user can see them: %+v", result)
	}
}

func TestCompareDNSReportsAResolverFailureWithoutClaimingASplit(t *testing.T) {
	result := CompareDNS(context.Background(),
		stubCore{err: errors.New("no transport")},
		stubSystem{addrs: []string{"10.0.0.5"}}, "example.org")
	if result.Agrees || result.Error == "" || result.Skipped {
		t.Fatalf("%+v", result)
	}
}

func TestCompareDNSDoesNotLetOneResolverStarveTheOther(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 75*time.Millisecond)
	defer cancel()
	result := CompareDNS(ctx, contextCore{}, blockedSystem{}, "example.org")
	if len(result.Core) != 1 || result.Core[0] != "1.1.1.1" {
		t.Fatalf("the core lookup was starved by the system resolver: %+v", result)
	}
	if !strings.Contains(result.Error, "система:") {
		t.Fatalf("the blocked resolver was not reported: %+v", result)
	}
}

func TestCompareDNSSkipsWhenThereIsNothingToCompare(t *testing.T) {
	for _, comparison := range []DNSComparison{
		CompareDNS(context.Background(), nil, nil, "example.org"),
		CompareDNS(context.Background(), stubCore{addrs: []string{"1.1.1.1"}}, nil, "example.org"),
		CompareDNS(context.Background(), stubCore{addrs: []string{"1.1.1.1"}}, stubSystem{}, ""),
	} {
		if !comparison.Skipped || comparison.Agrees {
			t.Fatalf("%+v", comparison)
		}
	}
}
