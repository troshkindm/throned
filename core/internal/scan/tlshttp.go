package scan

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"strings"
	"time"

	boxtls "github.com/sagernet/sing-box/common/tls"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/json/badoption"
	"github.com/sagernet/sing/common/logger"

	"golang.org/x/net/http2"
)

const (
	httpVersion11 = 11
	httpVersion2  = 2
	httpVersion3  = 3

	// Inside TLS the agent is hidden from the network, so one browser-like value serves every probe.
	userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36"
)

type httpPlan struct {
	tls     bool
	version int
	// Empty = handshake only.
	method       string
	serverName   string
	host         string
	path         string
	timeout      time.Duration
	fallbackPort uint16
	tlsConfig    boxtls.Config
}

func newHTTPPlan(ctx context.Context, options HTTPOptions) (*httpPlan, error) {
	plan := &httpPlan{
		tls:          options.TLS,
		serverName:   strings.TrimSpace(options.ServerName),
		host:         strings.TrimSpace(options.Host),
		timeout:      clampTimeout(options.Timeout, 3*time.Second),
		fallbackPort: phasePort(options.FallbackPort),
	}
	switch strings.ToLower(strings.TrimSpace(options.Version)) {
	case "", "1.1", "1", "http/1.1":
		plan.version = httpVersion11
	case "2", "2.0", "h2":
		plan.version = httpVersion2
	case "3", "3.0", "h3":
		plan.version = httpVersion3
	default:
		return nil, fmt.Errorf("unsupported HTTP version %q", options.Version)
	}
	switch method := strings.ToUpper(strings.TrimSpace(options.Method)); method {
	case "", http.MethodGet:
		plan.method = http.MethodGet
	case http.MethodHead:
		plan.method = http.MethodHead
	case "NONE":
	default:
		return nil, fmt.Errorf("unsupported HTTP method %q", options.Method)
	}
	if !plan.tls && plan.method == "" {
		return nil, errors.New("a handshake-only check needs TLS")
	}
	if !plan.tls && plan.version == httpVersion3 {
		return nil, errors.New("HTTP/3 needs TLS")
	}
	plan.path = strings.TrimSpace(options.Path)
	if !strings.HasPrefix(plan.path, "/") {
		plan.path = "/" + plan.path
	}
	if _, err := url.ParseRequestURI(plan.path); err != nil {
		return nil, fmt.Errorf("invalid HTTP path %q", options.Path)
	}
	if plan.tls {
		var err error
		plan.tlsConfig, err = newTLSConfig(ctx, options, plan.version, plan.timeout)
		if err != nil {
			return nil, err
		}
	}
	return plan, nil
}

// Fragment options capture ctx, so the config must be built with the session's context.
func newTLSConfig(ctx context.Context, options HTTPOptions, version int, timeout time.Duration) (boxtls.Config, error) {
	var alpn []string
	for _, protocol := range options.ALPN {
		if protocol = strings.TrimSpace(protocol); protocol != "" {
			alpn = append(alpn, protocol)
		}
	}
	if len(alpn) == 0 {
		switch version {
		case httpVersion11:
			alpn = []string{"http/1.1"}
		case httpVersion2:
			alpn = []string{"h2"}
		}
	}
	tlsOptions := option.OutboundTLSOptions{
		Enabled:          true,
		ServerName:       strings.TrimSpace(options.ServerName),
		DisableSNI:       options.DisableSNI,
		Insecure:         options.Insecure,
		ALPN:             alpn,
		MinVersion:       options.MinVersion,
		MaxVersion:       options.MaxVersion,
		HandshakeTimeout: badoption.Duration(timeout),
	}
	if options.MixedCaseSNI {
		tlsOptions.TLSTricks = &option.TLSTricksOptions{MixedCaseSNI: true}
	}
	// QUIC carries its own ClientHello: no uTLS fingerprint and no TCP fragmentation apply.
	if version == httpVersion3 {
		tlsOptions.ALPN = []string{"h3"}
	} else {
		tlsOptions.Fragment = options.Fragment
		tlsOptions.FragmentFallbackDelay = badoption.Duration(options.FragmentFallbackDelay)
		tlsOptions.RecordFragment = options.RecordFragment
		if options.Fingerprint != "" {
			tlsOptions.UTLS = &option.OutboundUTLSOptions{Enabled: true, Fingerprint: options.Fingerprint}
		}
	}
	return boxtls.NewClientWithOptions(boxtls.ClientOptions{
		Context:              ctx,
		Logger:               logger.NOP(),
		Options:              tlsOptions,
		AllowEmptyServerName: true,
	})
}

func (p *httpPlan) port(targetPort uint16) uint16 {
	if targetPort != 0 {
		return targetPort
	}
	return p.fallbackPort
}

func (p *httpPlan) clientConfig(addr netip.Addr) boxtls.Config {
	config := p.tlsConfig.Clone()
	if p.serverName == "" {
		config.SetServerName(addr.String())
	}
	return config
}

// conn, when non-nil, is an established TCP connection that probe takes over.
func (p *httpPlan) probe(ctx context.Context, session *Session, egress Egress, addr netip.Addr, port uint16, display string, conn net.Conn, result *Result) (string, error) {
	if p.version == httpVersion3 {
		if conn != nil {
			_ = conn.Close()
		}
		return p.probeHTTP3(ctx, session, egress, addr, port, display, result)
	}
	firstPhase := PhaseHTTP
	if p.tls {
		firstPhase = PhaseTLS
	}
	session.emit(EventStart, firstPhase, display, 0, "")
	// Connect and handshake share one timeout: the GUI budgets two per target.
	setupCtx, cancelSetup := context.WithTimeout(ctx, p.timeout)
	defer cancelSetup()
	if conn == nil {
		dialed, _, err := dialTCP(setupCtx, egress, addr, port, p.timeout)
		if err != nil {
			return firstPhase, err
		}
		conn = dialed
	}
	stream := conn
	defer func() { _ = stream.Close() }()

	negotiated := ""
	if p.tls {
		begin := time.Now()
		tlsConn, err := boxtls.ClientHandshake(setupCtx, conn, p.clientConfig(addr))
		cancelSetup()
		if err != nil {
			return PhaseTLS, err
		}
		latency := time.Since(begin)
		stream = tlsConn
		negotiated = tlsConn.ConnectionState().NegotiatedProtocol
		result.TLSMs = int32(latency.Milliseconds())
		session.emit(EventOK, PhaseTLS, display, latency, "")
		if p.method == "" {
			return "", nil
		}
		session.emit(EventStart, PhaseHTTP, display, 0, "")
	}

	var roundTrip func(*http.Request) (*http.Response, error)
	switch {
	case negotiated == http2.NextProtoTLS || (!p.tls && p.version == httpVersion2):
		clientConn, err := (&http2.Transport{AllowHTTP: !p.tls}).NewClientConn(stream)
		if err != nil {
			return PhaseHTTP, err
		}
		defer func() { _ = clientConn.Close() }()
		roundTrip = clientConn.RoundTrip
	case p.version == httpVersion2:
		return PhaseHTTP, fmt.Errorf("the server did not negotiate HTTP/2 (ALPN %q)", negotiated)
	default:
		roundTrip = func(request *http.Request) (*http.Response, error) {
			return roundTripHTTP1(stream, request)
		}
	}
	if err := p.exchange(ctx, addr, port, roundTrip, result); err != nil {
		return PhaseHTTP, err
	}
	session.emit(EventOK, PhaseHTTP, display, time.Duration(result.HTTPMs)*time.Millisecond, "")
	return "", nil
}

func (p *httpPlan) exchange(ctx context.Context, addr netip.Addr, port uint16, roundTrip func(*http.Request) (*http.Response, error), result *Result) error {
	ctx, cancel := context.WithTimeout(ctx, p.timeout)
	defer cancel()
	scheme := "http"
	if p.tls {
		scheme = "https"
	}
	request, err := http.NewRequestWithContext(ctx, p.method, scheme+"://"+netip.AddrPortFrom(addr, port).String()+p.path, nil)
	if err != nil {
		return err
	}
	request.Host = p.host
	if request.Host == "" {
		request.Host = p.serverName
	}
	if request.Host == "" {
		request.Host = DisplayTarget(addr, 0)
	}
	request.Header.Set("User-Agent", userAgent)
	request.Header.Set("Accept", "*/*")

	begin := time.Now()
	response, err := roundTrip(request)
	if err != nil {
		return err
	}
	defer func() { _ = response.Body.Close() }()
	result.HTTPMs = int32(time.Since(begin).Milliseconds())
	result.HTTPStatus = response.StatusCode
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("unexpected status %d", response.StatusCode)
	}
	return nil
}

// The deadline also bounds the body read that follows; the conn is never reused.
func roundTripHTTP1(conn net.Conn, request *http.Request) (*http.Response, error) {
	ctx := request.Context()
	if deadline, ok := ctx.Deadline(); ok {
		_ = conn.SetDeadline(deadline)
	}
	stop := context.AfterFunc(ctx, func() { _ = conn.SetDeadline(time.Unix(1, 0)) })
	defer stop()
	request.Close = true
	if err := request.Write(conn); err != nil {
		return nil, contextOr(ctx, err)
	}
	response, err := http.ReadResponse(bufio.NewReader(conn), request)
	if err != nil {
		return nil, contextOr(ctx, err)
	}
	return response, nil
}

func contextOr(ctx context.Context, err error) error {
	if ctxErr := ctx.Err(); ctxErr != nil {
		return ctxErr
	}
	return err
}
