package test_utils

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"time"

	"ThroneCore/internal/boxbox"

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

func BatchIPTest(ctx context.Context, i *boxbox.Box, outboundTags []string, maxConcurrency int, timeout time.Duration) []*IPTestResult {
	if timeout <= 0 {
		timeout = IPTestTimeout
	}

	results := runBatch(ctx, i, outboundTags, maxConcurrency, batchProbe[IPTestResult]{
		run: func(ctx context.Context, tag string, outbound adapter.Outbound) *IPTestResult {
			client := outboundHTTPClient(ctx, outbound, timeout)
			info, err := ipTest(ctx, client)
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

func ipTest(ctx context.Context, client *http.Client) (IPInfo, error) {
	info, _, err := ExternalAddress(ctx, client)
	return info, err
}

// OutboundHTTPClient exposes the batch tests' dialer so a single one-off probe does
// not have to stand up its own transport.
func OutboundHTTPClient(ctx context.Context, outbound adapter.Outbound, timeout time.Duration) *http.Client {
	return outboundHTTPClient(ctx, outbound, timeout)
}

// ExternalAddress also returns the server's Date header: the same round trip that
// says what the world sees says what time the world thinks it is, and a clock more
// than a few minutes out breaks every TLS handshake with no other symptom.
func ExternalAddress(ctx context.Context, client *http.Client) (IPInfo, time.Time, error) {
	var res IPInfo
	var served time.Time
	req, err := http.NewRequestWithContext(ctx, "GET", ipInfoAPI, nil)
	if err != nil {
		return res, served, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return res, served, err
	}
	defer resp.Body.Close()
	if parsed, parseErr := http.ParseTime(resp.Header.Get("Date")); parseErr == nil {
		served = parsed
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return res, served, err
	}
	err = json.Unmarshal(data, &res)
	if err != nil {
		return res, served, err
	}
	return res, served, nil
}
