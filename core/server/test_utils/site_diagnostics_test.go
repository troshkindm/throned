package test_utils

import (
	"context"
	"errors"
	"net"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"

	M "github.com/sagernet/sing/common/metadata"
)

func diagnosticDial(ctx context.Context, network string, address M.Socksaddr) (net.Conn, error) {
	return (&net.Dialer{}).DialContext(ctx, network, address.String())
}

func TestSiteDiagnosticsRetriesGetAfterHeadClosesTheConnection(t *testing.T) {
	var calls atomic.Int32
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		if r.Method == http.MethodHead {
			w.Header().Set("Connection", "close")
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}
		w.WriteHeader(http.StatusOK)
	}))
	defer s.Close()
	r := DiagnoseSite(context.Background(), diagnosticDial, s.URL)
	if r.GetStatus() != http.StatusOK || calls.Load() != 2 {
		t.Fatalf("result = %v, requests = %d; want GET 200 after the closed HEAD connection", r, calls.Load())
	}
}

func TestSiteDiagnosticsReturnsAfterStreamingGetHeaders(t *testing.T) {
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodHead {
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}
		w.WriteHeader(http.StatusOK)
		w.(http.Flusher).Flush()
		<-r.Context().Done()
	}))
	defer s.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	begin := time.Now()
	r := DiagnoseSite(ctx, diagnosticDial, s.URL)
	if elapsed := time.Since(begin); r.GetStatus() != http.StatusOK || elapsed > time.Second {
		t.Fatalf("result = %v after %s; headers were already available", r, elapsed)
	}
}

func TestSiteDiagnosticsHTTPResponses(t *testing.T) {
	// A status that describes the request rather than the site costs one more round trip,
	// asked the way a browser asks it; anything else is answered by the first HEAD.
	for _, status := range []int{204, 302, 403, 405, 500} {
		t.Run(http.StatusText(status), func(t *testing.T) {
			var calls atomic.Int32
			s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				want := http.MethodHead
				if calls.Add(1) > 1 {
					want = http.MethodGet
				}
				if r.Method != want {
					t.Errorf("request %d: method = %s, want %s", calls.Load(), r.Method, want)
				}
				if r.Header.Get("User-Agent") != ProbeUserAgent {
					t.Errorf("user agent = %q", r.Header.Get("User-Agent"))
				}
				w.Header().Set("Location", "/redirect")
				w.WriteHeader(status)
			}))
			defer s.Close()
			r := DiagnoseSite(context.Background(), diagnosticDial, s.URL)
			wantCalls := int32(1)
			if ShouldRetryWithGet(status) {
				wantCalls = 2
			}
			if r.GetError() != "" || r.GetStatus() != int32(status) || r.GetConnectMs() < 0 || r.GetHttpMs() < 0 || r.GetTlsMs() != -1 || calls.Load() != wantCalls {
				t.Fatalf("unexpected result: %v (requests: %d, want %d)", r, calls.Load(), wantCalls)
			}
		})
	}
}

func TestSiteDiagnosticsFailingStages(t *testing.T) {
	r := DiagnoseSite(context.Background(), func(context.Context, string, M.Socksaddr) (net.Conn, error) { return nil, errors.New("unreachable") }, "https://example.org")
	if r.GetErrorStage() != "connect" || r.GetTlsMs() != -1 || r.GetHttpMs() != -1 {
		t.Fatalf("%v", r)
	}
	s := httptest.NewTLSServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) { t.Error("untrusted TLS must not reach HTTP") }))
	defer s.Close()
	r = DiagnoseSite(context.Background(), diagnosticDial, s.URL)
	if r.GetErrorStage() != "tls" || r.GetTlsMs() < 0 || r.GetHttpMs() != -1 {
		t.Fatalf("%v", r)
	}
}

func TestSiteDiagnosticsRejectsInvalidInputBeforeDial(t *testing.T) {
	for _, raw := range []string{"", "file:///etc/hosts", "https://user:secret@example.org", "https://example.org/#fragment", "https://example.org:0", "https://example.org:70000"} {
		r := DiagnoseSite(context.Background(), func(context.Context, string, M.Socksaddr) (net.Conn, error) {
			t.Fatal("invalid input reached dial")
			return nil, nil
		}, raw)
		if r.GetErrorStage() != "input" || r.GetConnectMs() != -1 {
			t.Fatalf("%s: %v", raw, r)
		}
	}
}
