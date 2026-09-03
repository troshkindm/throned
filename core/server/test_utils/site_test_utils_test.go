package test_utils

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestSiteProbeReportsWhatTheSiteAnswered(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusForbidden)
	}))
	defer server.Close()

	probe := siteProbe(context.Background(), server.Client(), SiteTarget{Name: "blocked", URL: server.URL})
	if probe.Error != nil {
		t.Fatalf("a refusal is an answer, not a failure: %v", probe.Error)
	}
	// A geo-block replies and a filter does not, so the status has to survive.
	if probe.Status != http.StatusForbidden {
		t.Fatalf("status = %d, want %d", probe.Status, http.StatusForbidden)
	}
}

func TestSiteProbeFallsBackToGetWhenHeadIsRejected(t *testing.T) {
	var methods []string
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		methods = append(methods, r.Method)
		if r.Method == http.MethodHead {
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}
		w.WriteHeader(http.StatusOK)
	}))
	defer server.Close()

	probe := siteProbe(context.Background(), server.Client(), SiteTarget{Name: "head-hostile", URL: server.URL})
	if probe.Status != http.StatusOK {
		t.Fatalf("status = %d, want %d", probe.Status, http.StatusOK)
	}
	if len(methods) != 2 || methods[0] != http.MethodHead || methods[1] != http.MethodGet {
		t.Fatalf("methods = %v, want HEAD then GET", methods)
	}
}

func TestSiteProbeReportsSilenceAsNoStatus(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {}))
	url := server.URL
	// Closed before the probe: nothing is listening, which is the "no answer" case.
	server.Close()

	client := &http.Client{Timeout: time.Second}
	probe := siteProbe(context.Background(), client, SiteTarget{Name: "silent", URL: url})
	if probe.Error == nil {
		t.Fatal("a dead endpoint has to come back as an error")
	}
	if probe.Status != 0 {
		t.Fatalf("status = %d, want 0 when nothing answered", probe.Status)
	}
}

func TestSiteProbeStopsWithItsContext(t *testing.T) {
	release := make(chan struct{})
	server := httptest.NewServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {
		<-release
	}))
	defer server.Close()
	defer close(release)

	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	probe := siteProbe(ctx, server.Client(), SiteTarget{Name: "hung", URL: server.URL})
	if probe.Error == nil {
		t.Fatal("a cancelled batch must not leave a probe waiting")
	}
}
