package test_utils

import (
	"context"
	"errors"
	"io"
	"net"
	"testing"
	"time"
)

func TestProbeDialerClosesConnectionsAndRejectsNewDials(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()
	dials := 0
	probe := &probeDialer{dial: func(context.Context, string, string) (net.Conn, error) {
		dials++
		return client, nil
	}}
	if _, err := probe.DialContext(context.Background(), "tcp", "probe.example:443"); err != nil {
		t.Fatal(err)
	}
	probe.Close()
	probe.Close()
	if err := server.SetReadDeadline(time.Now().Add(time.Second)); err != nil && !errors.Is(err, io.ErrClosedPipe) {
		t.Fatal(err)
	}
	if _, err := server.Read(make([]byte, 1)); !errors.Is(err, io.EOF) {
		t.Fatalf("probe left its connection open: %v", err)
	}
	if _, err := probe.DialContext(context.Background(), "tcp", "probe.example:443"); !errors.Is(err, net.ErrClosed) {
		t.Fatalf("dial after close = %v", err)
	}
	if dials != 1 {
		t.Fatalf("closed probe performed %d dials", dials)
	}
}

func TestProbeDialerClosesConnectionThatArrivesAfterShutdown(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()
	started := make(chan struct{})
	release := make(chan struct{})
	probe := &probeDialer{dial: func(context.Context, string, string) (net.Conn, error) {
		close(started)
		<-release
		return client, nil
	}}
	done := make(chan error, 1)
	go func() {
		_, err := probe.DialContext(context.Background(), "tcp", "probe.example:443")
		done <- err
	}()
	<-started
	probe.Close()
	close(release)
	select {
	case err := <-done:
		if !errors.Is(err, net.ErrClosed) {
			t.Fatalf("dial completed after close: %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("in-flight dial did not finish")
	}
	if err := server.SetReadDeadline(time.Now().Add(time.Second)); err != nil && !errors.Is(err, io.ErrClosedPipe) {
		t.Fatal(err)
	}
	if _, err := server.Read(make([]byte, 1)); !errors.Is(err, io.EOF) {
		t.Fatalf("connection arriving after close was leaked: %v", err)
	}
}
