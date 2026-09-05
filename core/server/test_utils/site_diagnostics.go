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
	// Capture the variable rather than the current value: a refused HEAD is retried on a
	// fresh connection below, and that connection must also be closed on every return.
	defer func() { _ = conn.Close() }()
	initialConn := conn
	stop := context.AfterFunc(ctx, func() { _ = initialConn.Close() })
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
	reader := bufio.NewReader(io.LimitReader(stream, 1<<20))
	resp, err := writeProbe(ctx, stream, reader, u, http.MethodHead, false)
	if err != nil {
		r.HttpMs = proto.Int64(time.Since(begin).Milliseconds())
		return fail("http", err)
	}
	// A site that will not serve a bare HEAD is not a site that is unreachable. Retry on a
	// fresh connection: servers commonly answer HEAD with Connection: close, and writing a
	// GET to that socket would silently leave the original refusal as the final result.
	if ShouldRetryWithGet(resp.StatusCode) {
		_ = resp.Body.Close()
		_ = conn.Close()
		if retryConn, retryErr := dial(ctx, "tcp", destination); retryErr == nil {
			retryStop := context.AfterFunc(ctx, func() { _ = retryConn.Close() })
			defer retryStop()
			deadline, _ := ctx.Deadline()
			_ = retryConn.SetDeadline(deadline)
			var retryStream net.Conn = retryConn
			if u.Scheme == "https" {
				retryTLS := tls.Client(retryConn, &tls.Config{ServerName: u.Hostname(), MinVersion: tls.VersionTLS12, NextProtos: []string{"http/1.1"}})
				if retryErr = retryTLS.HandshakeContext(ctx); retryErr == nil {
					retryStream = retryTLS
				}
			}
			if retryErr == nil {
				retryReader := bufio.NewReader(io.LimitReader(retryStream, 1<<20))
				if retry, readErr := writeProbe(ctx, retryStream, retryReader, u, http.MethodGet, true); readErr == nil {
					conn = retryConn
					resp = retry
				} else {
					_ = retryConn.Close()
				}
			} else {
				_ = retryConn.Close()
			}
		}
	}
	r.HttpMs = proto.Int64(time.Since(begin).Milliseconds())
	r.Status = proto.Int32(int32(resp.StatusCode))
	if served, parseErr := http.ParseTime(resp.Header.Get("Date")); parseErr == nil {
		r.ServerUnix = proto.Int64(served.Unix())
	}
	// Closing a raw http.ReadResponse body can drain it first. A streaming GET would then
	// keep a completed diagnosis blocked until the deadline, so stop the socket first.
	_ = conn.Close()
	_ = resp.Body.Close()
	return r
}

// ProbeUserAgent makes the request representative of an ordinary browser. The returned
// status is still reported without guessing whether policy, load or filtering caused it.
const ProbeUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36"

// ShouldRetryWithGet identifies responses for which a browser-like GET can distinguish
// a rejected HEAD probe from the site's final answer. Retrying does not assign a cause.
func ShouldRetryWithGet(status int) bool {
	switch status {
	case http.StatusForbidden, http.StatusMethodNotAllowed, http.StatusNotImplemented,
		http.StatusTooManyRequests, http.StatusServiceUnavailable:
		return true
	}
	return false
}

func writeProbe(ctx context.Context, stream net.Conn, reader *bufio.Reader, u *url.URL, method string, last bool) (*http.Response, error) {
	req, err := http.NewRequestWithContext(ctx, method, u.String(), nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", ProbeUserAgent)
	req.Header.Set("Accept", "*/*")
	req.Close = last
	if err = req.Write(stream); err != nil {
		return nil, err
	}
	// The body is never read: the status line and the headers are the whole answer, and
	// the connection is closed underneath either way.
	return http.ReadResponse(reader, req)
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
