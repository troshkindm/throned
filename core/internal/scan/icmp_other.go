//go:build !windows && !darwin && !linux

package scan

import (
	"context"
	"errors"
	"net/netip"
	"time"
)

var errICMPUnsupported = errors.New("ICMP probes are not supported on this platform")

func icmpAvailable(bool) error {
	return errICMPUnsupported
}

func ping(ctx context.Context, egress Egress, addr netip.Addr, timeout time.Duration, count int) (time.Duration, error) {
	return 0, errICMPUnsupported
}
