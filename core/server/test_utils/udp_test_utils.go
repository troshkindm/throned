package test_utils

import (
	"context"
	"encoding/binary"
	"math/rand"
	"net"
	"time"

	"ThroneCore/internal/boxbox"

	"github.com/sagernet/sing-box/adapter"
	metadata "github.com/sagernet/sing/common/metadata"
)

var UDPReporter resultBuffer[UDPTestResult]

const (
	UDPTestTimeout      = 3 * time.Second
	UDPTestProbeCount   = 5
	UDPTestProbeSpacing = 120 * time.Millisecond
	UDPTestTarget       = "1.1.1.1:53"
)

// A DNS-over-UDP round trip through the outbound. TCP latency says nothing about
// this path, but neither does this probe claim to cover every possible UDP
// service or port: it measures reachability of the selected DNS target.
type UDPTestResult struct {
	Tag      string
	Sent     int
	Received int
	Min      time.Duration
	Avg      time.Duration
	Jitter   time.Duration
	Error    error
}

func BatchUDPTest(ctx context.Context, i *boxbox.Box, outboundTags []string, target string, probeCount int, maxConcurrency int, timeout time.Duration) []*UDPTestResult {
	if timeout <= 0 {
		timeout = UDPTestTimeout
	}
	if probeCount <= 0 {
		probeCount = UDPTestProbeCount
	}
	if target == "" {
		target = UDPTestTarget
	}

	results := runBatch(ctx, i, outboundTags, maxConcurrency, batchProbe[UDPTestResult]{
		run: func(ctx context.Context, tag string, outbound adapter.Outbound) *UDPTestResult {
			result := udpTest(ctx, outbound, target, probeCount, timeout)
			result.Tag = tag
			return result
		},
		fail: func(tag string, err error) *UDPTestResult {
			return &UDPTestResult{Tag: tag, Error: err}
		},
		publish: UDPReporter.AddResult,
	})
	UDPReporter.Reclaim(results)
	return results
}

func udpTest(ctx context.Context, outbound adapter.Outbound, target string, probeCount int, timeout time.Duration) *UDPTestResult {
	conn, err := outbound.DialContext(ctx, "udp", metadata.ParseSocksaddr(target))
	if err != nil {
		return &UDPTestResult{Error: err}
	}
	defer conn.Close()
	stop := context.AfterFunc(ctx, func() { _ = conn.Close() })
	defer stop()

	result := &UDPTestResult{Sent: probeCount}
	var samples []time.Duration
	var lastErr error
	for probe := 0; probe < probeCount; probe++ {
		if probe > 0 {
			select {
			case <-ctx.Done():
				result.Sent = probe
				return finishUDPResult(result, samples, lastErr)
			case <-time.After(UDPTestProbeSpacing):
			}
		}
		rtt, err := udpRoundTrip(conn, timeout)
		if err != nil {
			lastErr = err
			continue
		}
		samples = append(samples, rtt)
	}
	return finishUDPResult(result, samples, lastErr)
}

func udpRoundTrip(conn net.Conn, timeout time.Duration) (time.Duration, error) {
	id := uint16(rand.Uint32())
	query := buildDNSQuery(id)
	deadline := time.Now().Add(timeout)
	// Hysteria2's packet connection deliberately does not implement the combined
	// SetDeadline method (or write deadlines), but it does implement read
	// deadlines. The only blocking operation after the datagram write is Read, so
	// using the narrower deadline also works for regular UDP sockets and avoids
	// reporting every healthy Hysteria2 outbound as packet loss.
	if err := conn.SetReadDeadline(deadline); err != nil {
		return 0, err
	}
	begin := time.Now()
	if _, err := conn.Write(query); err != nil {
		return 0, err
	}
	buf := make([]byte, 512)
	// A reply to an earlier probe can still be in flight, so read until the id
	// matches rather than counting the stray as a loss.
	for {
		n, err := conn.Read(buf)
		if err != nil {
			return 0, err
		}
		if n >= 2 && binary.BigEndian.Uint16(buf[:2]) == id {
			return time.Since(begin), nil
		}
		if time.Now().After(deadline) {
			return 0, context.DeadlineExceeded
		}
	}
}

func finishUDPResult(result *UDPTestResult, samples []time.Duration, lastErr error) *UDPTestResult {
	result.Received = len(samples)
	if len(samples) == 0 {
		if lastErr == nil {
			lastErr = context.DeadlineExceeded
		}
		result.Error = lastErr
		return result
	}
	result.Min = samples[0]
	var total time.Duration
	for _, sample := range samples {
		if sample < result.Min {
			result.Min = sample
		}
		total += sample
	}
	result.Avg = total / time.Duration(len(samples))
	// Mean absolute difference between consecutive round trips: what a game or a
	// QUIC stream actually feels, unlike the spread around the average.
	if len(samples) > 1 {
		var drift time.Duration
		for index := 1; index < len(samples); index++ {
			delta := samples[index] - samples[index-1]
			if delta < 0 {
				delta = -delta
			}
			drift += delta
		}
		result.Jitter = drift / time.Duration(len(samples)-1)
	}
	return result
}

// A minimal DNS A query for "example.com", hand-rolled to keep the indirect DNS
// libraries out of this module's direct dependencies.
func buildDNSQuery(id uint16) []byte {
	name := []string{"example", "com"}
	query := make([]byte, 0, 32)
	header := make([]byte, 12)
	binary.BigEndian.PutUint16(header[0:], id)
	binary.BigEndian.PutUint16(header[2:], 0x0100) // standard query, recursion desired
	binary.BigEndian.PutUint16(header[4:], 1)      // one question
	query = append(query, header...)
	for _, label := range name {
		query = append(query, byte(len(label)))
		query = append(query, label...)
	}
	query = append(query, 0)          // root label
	query = append(query, 0x00, 0x01) // type A
	query = append(query, 0x00, 0x01) // class IN
	return query
}

// ProbeUDP is the single-outbound form the health snapshot needs: one dial through
// an outbound that is already running, rather than a batch over a throwaway box.
func ProbeUDP(ctx context.Context, outbound adapter.Outbound, target string, probeCount int, timeout time.Duration) *UDPTestResult {
	if target == "" {
		target = UDPTestTarget
	}
	if probeCount <= 0 {
		probeCount = UDPTestProbeCount
	}
	if timeout <= 0 {
		timeout = UDPTestTimeout
	}
	return udpTest(ctx, outbound, target, probeCount, timeout)
}
