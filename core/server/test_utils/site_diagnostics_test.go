package test_utils

import (
	"context"
	"errors"
	"net"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"

	M "github.com/sagernet/sing/common/metadata"
)

func diagnosticDial(ctx context.Context, network string, address M.Socksaddr) (net.Conn, error) {
	return (&net.Dialer{}).DialContext(ctx, network, address.String())
}

func TestSiteDiagnosticsHTTPResponses(t *testing.T) {
	for _, status := range []int{204, 302, 403, 405} {
		t.Run(http.StatusText(status), func(t *testing.T) {
			var calls atomic.Int32
			s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				calls.Add(1)
				if r.Method != http.MethodHead {
					t.Errorf("method = %s", r.Method)
				}
				w.Header().Set("Location", "/redirect")
				w.WriteHeader(status)
			}))
			defer s.Close()
			r := DiagnoseSite(context.Background(), diagnosticDial, s.URL)
			if r.GetError() != "" || r.GetStatus() != int32(status) || r.GetConnectMs() < 0 || r.GetHttpMs() < 0 || r.GetTlsMs() != -1 || calls.Load() != 1 {
				t.Fatalf("unexpected result: %v (requests: %d)", r, calls.Load())
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
