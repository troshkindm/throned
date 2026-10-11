//go:build with_quic

package scan

import (
	"context"
	"net"
	"net/netip"
	"time"

	"github.com/sagernet/quic-go"
	"github.com/sagernet/quic-go/http3"
)

func (p *httpPlan) probeHTTP3(ctx context.Context, session *Session, egress Egress, addr netip.Addr, port uint16, display string, result *Result) (string, error) {
	session.emit(EventStart, PhaseTLS, display, 0, "")
	controlFunc, err := egress.Control()
	if err != nil {
		return PhaseTLS, err
	}
	network := "udp4"
	if addr.Is6() {
		network = "udp6"
	}
	packetConn, err := (&net.ListenConfig{Control: controlFunc}).ListenPacket(ctx, network, "")
	if err != nil {
		return PhaseTLS, err
	}
	defer func() { _ = packetConn.Close() }()
	stdConfig, err := p.clientConfig(addr).STDConfig()
	if err != nil {
		return PhaseTLS, err
	}
	stdConfig = stdConfig.Clone()
	stdConfig.NextProtos = []string{http3.NextProtoH3}

	handshakeCtx, cancel := context.WithTimeout(ctx, p.timeout)
	begin := time.Now()
	conn, err := quic.Dial(handshakeCtx, packetConn, net.UDPAddrFromAddrPort(netip.AddrPortFrom(addr, port)), stdConfig, &quic.Config{
		HandshakeIdleTimeout: p.timeout,
		MaxIdleTimeout:       p.timeout,
	})
	cancel()
	if err != nil {
		return PhaseTLS, contextOr(ctx, err)
	}
	defer func() { _ = conn.CloseWithError(0, "") }()
	latency := time.Since(begin)
	result.TLSMs = int32(latency.Milliseconds())
	session.emit(EventOK, PhaseTLS, display, latency, "")
	if p.method == "" {
		return "", nil
	}

	session.emit(EventStart, PhaseHTTP, display, 0, "")
	clientConn := (&http3.Transport{}).NewClientConn(conn)
	if err = p.exchange(ctx, addr, port, clientConn.RoundTrip, result); err != nil {
		return PhaseHTTP, err
	}
	session.emit(EventOK, PhaseHTTP, display, time.Duration(result.HTTPMs)*time.Millisecond, "")
	return "", nil
}
