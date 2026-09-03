package test_utils

import (
	"context"
	"net/http"
	"sync"
	"time"

	"ThroneCore/internal/boxbox"

	"github.com/sagernet/sing-box/adapter"
)

var SiteReporter resultBuffer[SiteTestResult]

const SiteTestTimeout = 5 * time.Second

type SiteTarget struct {
	Name string
	URL  string
}

// Status is the HTTP code the site answered with, or 0 when nothing answered.
// A refusal is a different answer from silence: a geo-block replies, a filter does not.
type SiteProbe struct {
	Name     string
	Status   int
	Duration time.Duration
	Error    error
}

type SiteTestResult struct {
	Tag    string
	Probes []SiteProbe
	Error  error
}

func BatchSiteTest(ctx context.Context, i *boxbox.Box, outboundTags []string, targets []SiteTarget,
	maxConcurrency int, timeout time.Duration) []*SiteTestResult {
	if timeout <= 0 {
		timeout = SiteTestTimeout
	}

	results := runBatch(ctx, i, outboundTags, maxConcurrency, batchProbe[SiteTestResult]{
		run: func(ctx context.Context, tag string, outbound adapter.Outbound) *SiteTestResult {
			client := outboundHTTPClient(ctx, outbound, timeout)
			probes := make([]SiteProbe, len(targets))

			// One client, every target at once: a node with eight sites to check would
			// otherwise cost eight timeouts back to back.
			wg := &sync.WaitGroup{}
			wg.Add(len(targets))
			for idx, target := range targets {
				go func(slot int, t SiteTarget) {
					defer wg.Done()
					probes[slot] = siteProbe(ctx, client, t)
				}(idx, target)
			}
			wg.Wait()

			return &SiteTestResult{Tag: tag, Probes: probes}
		},
		fail: func(tag string, err error) *SiteTestResult {
			return &SiteTestResult{Tag: tag, Error: err}
		},
		publish: SiteReporter.AddResult,
	})
	SiteReporter.Reclaim(results)
	return results
}

func siteProbe(ctx context.Context, client *http.Client, target SiteTarget) SiteProbe {
	begin := time.Now()
	// HEAD keeps the body off the wire; sites that reject it are retried with GET below.
	req, err := http.NewRequestWithContext(ctx, http.MethodHead, target.URL, nil)
	if err != nil {
		return SiteProbe{Name: target.Name, Error: err}
	}
	resp, err := client.Do(req)
	if err == nil && resp.StatusCode == http.StatusMethodNotAllowed {
		_ = resp.Body.Close()
		if getReq, reqErr := http.NewRequestWithContext(ctx, http.MethodGet, target.URL, nil); reqErr == nil {
			resp, err = client.Do(getReq)
		}
	}
	if err != nil {
		return SiteProbe{Name: target.Name, Duration: time.Since(begin), Error: err}
	}
	status := resp.StatusCode
	_ = resp.Body.Close()
	return SiteProbe{Name: target.Name, Status: status, Duration: time.Since(begin)}
}
