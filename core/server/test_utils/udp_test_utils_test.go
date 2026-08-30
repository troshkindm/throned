package test_utils

import (
	"net"
	"os"
	"testing"
	"time"
)

type readDeadlineOnlyConn struct {
	net.Conn
}

func (c readDeadlineOnlyConn) SetDeadline(time.Time) error {
	return os.ErrInvalid
}

func (c readDeadlineOnlyConn) SetWriteDeadline(time.Time) error {
	return os.ErrInvalid
}

func TestUDPRoundTripSupportsHysteria2Deadlines(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()

	serverDone := make(chan error, 1)
	go func() {
		query := make([]byte, 512)
		n, err := server.Read(query)
		if err == nil && n >= 2 {
			_, err = server.Write(query[:2])
		}
		serverDone <- err
	}()

	if _, err := udpRoundTrip(readDeadlineOnlyConn{Conn: client}, time.Second); err != nil {
		t.Fatalf("UDP round trip failed for a read-deadline-only connection: %v", err)
	}
	if err := <-serverDone; err != nil {
		t.Fatalf("fake DNS responder failed: %v", err)
	}
}
