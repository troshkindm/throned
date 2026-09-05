package test_utils

import (
	"bufio"
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"syscall"
	"time"

	"ThroneCore/gen"
	M "github.com/sagernet/sing/common/metadata"
	"google.golang.org/protobuf/proto"
)

// DiagnoseSite measures only observable phases. DNS can happen on a remote proxy,
// so it is included in establishment, never presented as a separate successful lookup.
func DiagnoseSite(ctx context.Context, dial func(context.Context, string, M.Socksaddr) (net.Conn, error), rawURL string) *gen.DiagnoseSiteResponse {
	r := &gen.DiagnoseSiteResponse{ConnectMs: proto.Int64(-1), TlsMs: proto.Int64(-1), HttpMs: proto.Int64(-1)}
	fail := func(stage string, err error) *gen.DiagnoseSiteResponse {
		r.ErrorStage = proto.String(stage)
		r.Error = proto.String(err.Error())
		return r
	}
	u, err := url.Parse(rawURL)
	if err != nil {
		return fail("input", fmt.Errorf("invalid URL"))
	}
	if (u.Scheme != "http" && u.Scheme != "https") || u.Hostname() == "" || u.User != nil || u.Fragment != "" {
		return fail("input", fmt.Errorf("enter an HTTP or HTTPS URL without credentials or fragment"))
	}
	port := u.Port()
	if port == "" {
		if u.Scheme == "https" {
			port = "443"
		} else {
			port = "80"
		}
	}
	destination := M.ParseSocksaddr(net.JoinHostPort(u.Hostname(), port))
	parsedPort, portErr := strconv.ParseUint(port, 10, 16)
	if portErr != nil || parsedPort == 0 || !destination.IsValid() {
		return fail("input", fmt.Errorf("invalid destination port"))
	}
	ctx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	begin := time.Now()
	conn, err := dial(ctx, "tcp", destination)
	r.ConnectMs = proto.Int64(time.Since(begin).Milliseconds())
	if err != nil {
		return fail("connect", err)
	}
	defer conn.Close()
	stop := context.AfterFunc(ctx, func() { _ = conn.Close() })
	defer stop()
	deadline, _ := ctx.Deadline()
	_ = conn.SetDeadline(deadline)
	var stream net.Conn = conn
	if u.Scheme == "https" {
		begin = time.Now()
		tlsConn := tls.Client(conn, &tls.Config{ServerName: u.Hostname(), MinVersion: tls.VersionTLS12, NextProtos: []string{"http/1.1"}})
		err = tlsConn.HandshakeContext(ctx)
		r.TlsMs = proto.Int64(time.Since(begin).Milliseconds())
		if err != nil {
			// A refused certificate and a severed handshake are different diagnoses:
			// the first is the server's answer, the second means something in between
			// dropped the connection once it saw the name being requested.
			r.TlsCut = proto.Bool(handshakeWasCut(err))
			return fail("tls", err)
		}
		state := tlsConn.ConnectionState()
		r.TlsVersion = proto.String(tls.VersionName(state.Version))
		r.TlsAlpn = proto.String(state.NegotiatedProtocol)
		if len(state.PeerCertificates) > 0 {
			leaf := state.PeerCertificates[0]
			r.TlsIssuer = proto.String(leaf.Issuer.CommonName)
			r.TlsExpiresUnix = proto.Int64(leaf.NotAfter.Unix())
		}
		stream = tlsConn
	}
	begin = time.Now()
	req, err := http.NewRequestWithContext(ctx, http.MethodHead, u.String(), nil)
	if err != nil {
		return fail("input", err)
	}
	req.Header.Set("User-Agent", "Throned-Diagnostics/1")
	req.Close = true
	err = req.Write(stream)
	if err != nil {
		r.HttpMs = proto.Int64(time.Since(begin).Milliseconds())
		return fail("http", err)
	}
	resp, err := http.ReadResponse(bufio.NewReader(io.LimitReader(stream, 1<<20)), req)
	r.HttpMs = proto.Int64(time.Since(begin).Milliseconds())
	if err != nil {
		return fail("http", err)
	}
	defer resp.Body.Close()
	r.Status = proto.Int32(int32(resp.StatusCode))
	if served, parseErr := http.ParseTime(resp.Header.Get("Date")); parseErr == nil {
		r.ServerUnix = proto.Int64(served.Unix())
	}
	return r
}

// A cut handshake shows up as a reset or a truncated read, never as a certificate
// or protocol complaint, because the peer never got far enough to make one.
func handshakeWasCut(err error) bool {
	if errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) || errors.Is(err, syscall.ECONNRESET) {
		return true
	}
	var recordErr tls.RecordHeaderError
	if errors.As(err, &recordErr) {
		return true
	}
	message := strings.ToLower(err.Error())
	for _, marker := range []string{"connection reset", "reset by peer", "unexpected eof", "broken pipe", "forcibly closed"} {
		if strings.Contains(message, marker) {
			return true
		}
	}
	return false
}
