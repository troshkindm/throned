package scan

import (
	"context"
	"errors"
	"fmt"
	"math"
	"net"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"github.com/sagernet/sing/common/control"
)

type unboundEgress struct{}

func (unboundEgress) Control() (control.Func, error) { return nil, nil }

func (unboundEgress) InterfaceFor(netip.Addr) *control.Interface { return nil }

type refusingEgress struct{}

func (refusingEgress) Control() (control.Func, error) { return nil, ErrNoEgress }

func (refusingEgress) InterfaceFor(netip.Addr) *control.Interface { return nil }

type panickingEgress struct{}

func (panickingEgress) Control() (control.Func, error) { panic("boom") }

func (panickingEgress) InterfaceFor(netip.Addr) *control.Interface { return nil }

type vanishingEgress struct {
	allowed atomic.Int32
}

func (e *vanishingEgress) Control() (control.Func, error) {
	if e.allowed.Add(-1) < 0 {
		return nil, ErrNoEgress
	}
	return nil, nil
}

func (e *vanishingEgress) InterfaceFor(netip.Addr) *control.Interface { return nil }

type unroutableEgress struct {
	errno syscall.Errno
}

func (e unroutableEgress) Control() (control.Func, error) {
	return func(string, string, syscall.RawConn) error { return e.errno }, nil
}

func (unroutableEgress) InterfaceFor(netip.Addr) *control.Interface { return nil }

var testSessionSeq atomic.Int64

func testSession(t *testing.T) *Session {
	t.Helper()
	// Stop leaves a tombstone, so a repeated run (-count) must not reuse an id.
	id := fmt.Sprintf("test-%s-%d", t.Name(), testSessionSeq.Add(1))
	session, release, err := Acquire(id)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		release()
		Stop(id)
	})
	return session
}

func loopbackSpec(ports ...int32) Spec {
	entries := make([]Entry, 0, len(ports))
	for _, port := range ports {
		entries = append(entries, Entry{CIDR: "127.0.0.1", Port: port})
	}
	return Spec{Entries: entries}
}

func closedPort(t *testing.T) int32 {
	t.Helper()
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := listener.Addr().(*net.TCPAddr).Port
	_ = listener.Close()
	return int32(port)
}

func TestProbeCountOnly(t *testing.T) {
	response, err := Probe(context.Background(), nil, unboundEgress{}, ProbeRequest{Spec: Spec{Entries: []Entry{{CIDR: "10.0.0.0/30"}, {CIDR: "bad"}}}, Cursor: 2})
	if err != nil {
		t.Fatal(err)
	}
	if response.Total != 4 || response.NextCursor != 2 || len(response.Results) != 0 || response.InvalidEntries != 1 {
		t.Fatalf("%+v", response)
	}
}

func TestProbeNoPhasesPassesThrough(t *testing.T) {
	session := testSession(t)
	response, err := Probe(session.Context(), session, refusingEgress{}, ProbeRequest{Spec: Spec{Entries: []Entry{{CIDR: "10.0.0.0/29"}}}, Cursor: 3, MaxTargets: 4})
	if err != nil {
		t.Fatal(err)
	}
	if len(response.Results) != 4 || response.NextCursor != 7 || response.Aborted {
		t.Fatalf("%+v", response)
	}
	for _, result := range response.Results {
		if !result.Passed {
			t.Fatalf("%+v", result)
		}
	}
	if counters := session.Counters(); counters.Probed != 4 || counters.ProbePassed != 4 {
		t.Fatalf("%+v", counters)
	}
}

func TestProbeTCP(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	go func() {
		for {
			conn, acceptErr := listener.Accept()
			if acceptErr != nil {
				return
			}
			_ = conn.Close()
		}
	}()
	open := int32(listener.Addr().(*net.TCPAddr).Port)
	closed := closedPort(t)

	session := testSession(t)
	response, err := Probe(session.Context(), session, unboundEgress{}, ProbeRequest{
		Spec:       loopbackSpec(open, closed),
		MaxTargets: 10,
		TCP:        TCPOptions{Enabled: true, Timeout: 2 * time.Second, Attempts: 2},
	})
	if err != nil {
		t.Fatal(err)
	}
	if len(response.Results) != 2 || response.NextCursor != 2 {
		t.Fatalf("%+v", response)
	}
	for _, result := range response.Results {
		switch int32(result.Port) {
		case open:
			if !result.Passed || int32(result.ProbePort) != open {
				t.Fatalf("open port: %+v", result)
			}
		case closed:
			if result.Passed || result.FailedPhase != PhaseTCP || result.Error == "" || result.LocalFailure {
				t.Fatalf("closed port: %+v", result)
			}
		}
	}
	events, last := session.Events(0)
	if last == 0 || len(events) == 0 {
		t.Fatal("no events")
	}
	if counters := session.Counters(); counters.Probed != 2 || counters.ProbePassed != 1 {
		t.Fatalf("%+v", counters)
	}
}

func TestProbeHTTP(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		switch {
		case request.Host != "probe.example":
			writer.WriteHeader(http.StatusNotFound)
		case request.URL.Path == "/empty":
			writer.WriteHeader(http.StatusNoContent)
		case request.URL.Path == "/health":
			_, _ = writer.Write([]byte("status: alive"))
		default:
			writer.WriteHeader(http.StatusNotFound)
		}
	}))
	defer server.Close()
	port := int32(server.Listener.Addr().(*net.TCPAddr).Port)

	base := HTTPOptions{Enabled: true, Host: "probe.example", Path: "health", Method: "GET", Version: "1.1", Timeout: 2 * time.Second}
	for _, tcp := range []bool{false, true} {
		session := testSession(t)
		response, err := Probe(session.Context(), session, unboundEgress{}, ProbeRequest{
			Spec:       loopbackSpec(port),
			MaxTargets: 1,
			TCP:        TCPOptions{Enabled: tcp},
			HTTP:       base,
		})
		if err != nil {
			t.Fatal(err)
		}
		if len(response.Results) != 1 || !response.Results[0].Passed || response.Results[0].HTTPStatus != 200 {
			t.Fatalf("tcp=%v: %+v", tcp, response)
		}
	}

	// Only 200 passes: another success code is as much a miss as a 404.
	for host, path := range map[string]string{"other.example": "health", "probe.example": "empty"} {
		wrongStatus := base
		wrongStatus.Host, wrongStatus.Path = host, path
		session := testSession(t)
		response, err := Probe(session.Context(), session, unboundEgress{}, ProbeRequest{Spec: loopbackSpec(port), MaxTargets: 1, HTTP: wrongStatus})
		if err != nil {
			t.Fatal(err)
		}
		result := response.Results[0]
		if want := map[string]int{"health": 404, "empty": 204}[path]; result.Passed || result.FailedPhase != PhaseHTTP ||
			result.HTTPStatus != want || !strings.Contains(result.Error, strconv.Itoa(want)) {
			t.Fatalf("%s%s: %+v", host, path, result)
		}
	}
}

func TestProbeRejectsBadOptions(t *testing.T) {
	session := testSession(t)
	for _, options := range []HTTPOptions{
		{Enabled: true, Method: "NONE"},
		{Enabled: true, Version: "3"},
		{Enabled: true, Method: "POST"},
	} {
		response, err := Probe(session.Context(), session, unboundEgress{}, ProbeRequest{Spec: loopbackSpec(80), MaxTargets: 1, HTTP: options})
		if err == nil || response.Total != 1 {
			t.Fatalf("%+v accepted", options)
		}
	}
}

func TestProbeAbortKeepsCursor(t *testing.T) {
	session := testSession(t)
	ctx, cancel := context.WithCancel(session.Context())
	cancel()
	response, err := Probe(ctx, session, unboundEgress{}, ProbeRequest{
		Spec:       Spec{Entries: []Entry{{CIDR: "127.0.0.0/30", Port: closedPort(t)}}},
		Cursor:     1,
		MaxTargets: 3,
		TCP:        TCPOptions{Enabled: true},
	})
	if err != nil {
		t.Fatal(err)
	}
	if !response.Aborted || response.NextCursor != 1 || len(response.Results) != 0 {
		t.Fatalf("%+v", response)
	}
}

func TestProbeRefusingEgress(t *testing.T) {
	session := testSession(t)
	response, err := Probe(session.Context(), session, refusingEgress{}, ProbeRequest{
		Spec:       Spec{Entries: []Entry{{CIDR: "127.0.0.0/29", Port: 443}}},
		Cursor:     2,
		MaxTargets: 4,
		TCP:        TCPOptions{Enabled: true},
	})
	if !errors.Is(err, ErrNoEgress) || !strings.Contains(err.Error(), "paused") {
		t.Fatalf("err %v", err)
	}
	if !response.Aborted || response.NextCursor != 2 || len(response.Results) != 0 || response.Total != 8 {
		t.Fatalf("%+v", response)
	}
	if counters := session.Counters(); counters.Probed != 0 {
		t.Fatalf("%+v", counters)
	}
	events, _ := session.Events(0)
	for _, event := range events {
		if event.Kind == EventFail {
			t.Fatalf("a refused target was reported as failed: %+v", event)
		}
	}
}

func TestProbeEgressLostMidCall(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	go func() {
		for {
			conn, acceptErr := listener.Accept()
			if acceptErr != nil {
				return
			}
			_ = conn.Close()
		}
	}()
	open := int32(listener.Addr().(*net.TCPAddr).Port)

	session := testSession(t)
	egress := &vanishingEgress{}
	egress.allowed.Store(3)
	response, err := Probe(session.Context(), session, egress, ProbeRequest{
		Spec:        loopbackSpec(open, open, open, open, open, open, open, open),
		MaxTargets:  8,
		TCP:         TCPOptions{Enabled: true, Attempts: 1},
		Concurrency: 1,
	})
	if !errors.Is(err, ErrNoEgress) {
		t.Fatalf("err %v", err)
	}
	if !response.Aborted || response.NextCursor != 3 || len(response.Results) != 3 {
		t.Fatalf("%+v", response)
	}
	for _, result := range response.Results {
		if !result.Passed {
			t.Fatalf("%+v", result)
		}
	}
	if counters := session.Counters(); counters.Probed != 3 || counters.ProbePassed != 3 {
		t.Fatalf("%+v", counters)
	}
}

func TestLocalFailure(t *testing.T) {
	loopback := netip.MustParseAddr("127.0.0.1")
	routing := &net.OpError{Op: "dial", Net: "tcp", Err: os.NewSyscallError("connect", routingErrnos[0])}
	for _, err := range []error{errICMPNotPermitted, errNoIPv4Source, fmt.Errorf("ping: %w", errNoIPv6Source)} {
		if !localFailure(unboundEgress{}, loopback, err) {
			t.Fatalf("%v is local", err)
		}
	}
	if localFailure(unboundEgress{}, loopback, routing) {
		t.Fatal("an unreachable with a local route is the target's")
	}
	if !localFailure(unroutableEgress{errno: routingErrnos[0]}, loopback, routing) {
		t.Fatal("an unreachable without a local route is local")
	}
	if !localFailure(refusingEgress{}, loopback, routing) {
		t.Fatal("an unreachable without an egress is local")
	}
	if localFailure(unroutableEgress{errno: routingErrnos[0]}, loopback, errors.New("connection refused")) {
		t.Fatal("a refusal is the target's")
	}
}

func TestProbeLocalFailure(t *testing.T) {
	session := testSession(t)
	response, err := Probe(session.Context(), session, unroutableEgress{errno: routingErrnos[0]}, ProbeRequest{
		Spec:       loopbackSpec(closedPort(t)),
		MaxTargets: 1,
		TCP:        TCPOptions{Enabled: true, Attempts: 1},
	})
	if err != nil {
		t.Fatal(err)
	}
	if len(response.Results) != 1 || response.Aborted {
		t.Fatalf("%+v", response)
	}
	if result := response.Results[0]; result.Passed || !result.LocalFailure || result.FailedPhase != PhaseTCP {
		t.Fatalf("%+v", result)
	}
}

func TestProbeRecoversPanic(t *testing.T) {
	session := testSession(t)
	response, err := Probe(session.Context(), session, panickingEgress{}, ProbeRequest{Spec: loopbackSpec(443), MaxTargets: 1, TCP: TCPOptions{Enabled: true}})
	if err != nil {
		t.Fatal(err)
	}
	if len(response.Results) != 1 || response.Aborted {
		t.Fatalf("%+v", response)
	}
	if result := response.Results[0]; result.Passed || result.FailedPhase != PhaseTCP || !strings.Contains(result.Error, "boom") {
		t.Fatalf("%+v", result)
	}
}

func TestSessionStopAndEvents(t *testing.T) {
	id := fmt.Sprintf("stop-%d", testSessionSeq.Add(1))
	session, release, err := Acquire(id)
	if err != nil {
		t.Fatal(err)
	}
	for i := range 300 {
		session.emit(EventOK, PhaseTCP, fmt.Sprint(i), time.Millisecond, "")
	}
	events, last := session.Events(0)
	if last != 300 || len(events) != eventPageSize || events[0].Seq != 300-eventPageSize+1 || events[len(events)-1].Seq != 300 {
		t.Fatalf("last %d, %d events from %d", last, len(events), events[0].Seq)
	}
	if events, _ = session.Events(298); len(events) != 2 || events[0].Target != "298" {
		t.Fatalf("%+v", events)
	}
	if events, _ = session.Events(math.MaxInt64); len(events) != 0 {
		t.Fatalf("a counters-only query got %d events", len(events))
	}
	release()
	Stop(id)
	if session.Context().Err() == nil {
		t.Fatal("stop did not cancel")
	}
	if _, _, err = Acquire(id); !errors.Is(err, ErrStopped) {
		t.Fatalf("acquire after stop: %v", err)
	}
	if Lookup(id) != session {
		t.Fatal("a stopped session must stay readable")
	}
	Stop("never-started")
	if _, _, err = Acquire("never-started"); !errors.Is(err, ErrStopped) {
		t.Fatalf("acquire after an early stop: %v", err)
	}
}

func TestCheckNetwork(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	closed := fmt.Sprintf("127.0.0.1:%d", closedPort(t))
	up, err := CheckNetwork(context.Background(), unboundEgress{}, []string{closed, listener.Addr().String()}, time.Second)
	if !up || err != nil {
		t.Fatalf("up %v err %v", up, err)
	}
	up, err = CheckNetwork(context.Background(), unboundEgress{}, []string{closed}, time.Second)
	if up || err == nil {
		t.Fatalf("closed: up %v err %v", up, err)
	}
	if up, err = CheckNetwork(context.Background(), refusingEgress{}, []string{listener.Addr().String()}, time.Second); up || err == nil {
		t.Fatal("refusing egress reported up")
	}
}
