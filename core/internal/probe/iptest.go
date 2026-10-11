package probe

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"time"

	"github.com/sagernet/sing-box/adapter"
)

type IPInfo struct {
	IP          string `json:"ip"`
	CountryCode string `json:"country_code"`
}

var IPReporter resultBuffer[IPTestResult]

const IPTestTimeout = 3 * time.Second
const ipInfoAPI = "https://api.ip2location.io/"

type IPTestResult struct {
	Result IPInfo
	Tag    string
	Error  error
}

func BatchIPTest(ctx context.Context, i Box, outboundTags []string, maxConcurrency int, cold bool, timeout time.Duration) []*IPTestResult {
	if timeout <= 0 {
		timeout = IPTestTimeout
	}

	results := runBatch(ctx, i, outboundTags, maxConcurrency, batchProbe[IPTestResult]{
		run: func(ctx context.Context, tag string, outbound adapter.Outbound) *IPTestResult {
			if err := awaitTunnels(ctx, i, tag); err != nil {
				return &IPTestResult{Tag: tag, Error: err}
			}
			client, closeClient := outboundHTTPClient(ctx, i, tag, outbound)
			defer closeClient()
			info, err := ipTest(ctx, client, firstRequestTimeout(i, tag, cold, timeout))
			return &IPTestResult{Result: info, Tag: tag, Error: err}
		},
		fail: func(tag string, err error) *IPTestResult {
			return &IPTestResult{Tag: tag, Error: err}
		},
		publish: IPReporter.AddResult,
	})
	IPReporter.Reclaim(results)
	return results
}

func ipTest(ctx context.Context, client *http.Client, timeout time.Duration) (IPInfo, error) {
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	info, _, err := ExternalAddress(ctx, client)
	return info, err
}

// ClockReading is everything a Date header can honestly say about the system clock. The
// header carries whole seconds and arrives after a round trip, so the instant it names is
// an interval and so is ours; subtracting two points out of those intervals invents a
// number that moves by a second between two identical checks.
type ClockReading struct {
	Served time.Time // start of the second the server named
	Sent   time.Time // local clock before the request left
	Got    time.Time // local clock after the response came back
}

// SkewMs reports how far the system clock lies outside what the reading can account for,
// and 0 while the two intervals still overlap: that is the whole truthful answer.
func (r ClockReading) SkewMs() (int64, bool) {
	if r.Served.IsZero() || r.Sent.IsZero() || r.Got.IsZero() {
		return 0, false
	}
	behind := r.Got.Sub(r.Served)
	ahead := r.Sent.Sub(r.Served.Add(time.Second))
	switch {
	case ahead > 0:
		return ahead.Milliseconds(), true
	case behind < 0:
		return behind.Milliseconds(), true
	default:
		return 0, true
	}
}

// OutboundHTTPClient exposes the batch tests' dialer so a single one-off probe does
// not have to stand up its own transport.
func OutboundHTTPClient(ctx context.Context, i Box, outbound adapter.Outbound, timeout time.Duration) (*http.Client, func()) {
	client, closeClient := outboundHTTPClient(ctx, i, outbound.Tag(), outbound)
	client.Timeout = timeout
	return client, closeClient
}

// ExternalAddress also returns what the response said about the time: the same round trip
// that says what the world sees says what time the world thinks it is, and a clock more
// than a few minutes out breaks every TLS handshake with no other symptom.
func ExternalAddress(ctx context.Context, client *http.Client) (IPInfo, ClockReading, error) {
	var res IPInfo
	var clock ClockReading
	req, err := http.NewRequestWithContext(ctx, "GET", ipInfoAPI, nil)
	if err != nil {
		return res, clock, err
	}
	clock.Sent = time.Now()
	resp, err := client.Do(req)
	if err != nil {
		return res, clock, err
	}
	defer resp.Body.Close()
	clock.Got = time.Now()
	if parsed, parseErr := http.ParseTime(resp.Header.Get("Date")); parseErr == nil {
		clock.Served = parsed
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return res, clock, err
	}
	err = json.Unmarshal(data, &res)
	if err != nil {
		return res, clock, err
	}
	return res, clock, nil
}
