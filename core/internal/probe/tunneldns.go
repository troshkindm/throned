package probe

import (
	"context"
	"errors"
	"net"
	"net/netip"
	"sync"
	"time"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/dns"
	"github.com/sagernet/sing-box/dns/transport"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/common/logger"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"

	mDNS "github.com/miekg/dns"
)

// An endpoint needs an address before anything enters the tunnel, and a probe box would look the name up from this
// device; these are asked through the endpoint instead, like the profile's remote DNS in a running config.
var tunnelResolvers = []M.Socksaddr{
	M.ParseSocksaddr("1.1.1.1:53"),
	M.ParseSocksaddr("8.8.8.8:53"),
}

const tunnelLookupAttempt = 2 * time.Second

// Resolves names through the endpoint itself, once per client: a warm request that redials reuses the answer.
func tunnelResolvingDial(dialCtx context.Context, outbound adapter.Outbound) func(ctx context.Context, network, addr string) (net.Conn, error) {
	var (
		access   sync.Mutex
		resolved = make(map[string][]netip.Addr)
	)
	return func(ctx context.Context, _ string, addr string) (net.Conn, error) {
		destination := M.ParseSocksaddr(addr)
		if !destination.IsFqdn() {
			return outbound.DialContext(dialCtx, N.NetworkTCP, destination)
		}
		access.Lock()
		addresses, cached := resolved[destination.Fqdn]
		access.Unlock()
		if !cached {
			lookupCtx := dialCtx
			if deadline, hasDeadline := ctx.Deadline(); hasDeadline {
				var cancel context.CancelFunc
				lookupCtx, cancel = context.WithDeadline(dialCtx, deadline)
				defer cancel()
			}
			var err error
			addresses, err = lookupThroughTunnel(lookupCtx, outbound, destination.Fqdn)
			if err != nil {
				return nil, err
			}
			access.Lock()
			resolved[destination.Fqdn] = addresses
			access.Unlock()
		}
		return N.DialSerial(dialCtx, outbound, N.NetworkTCP, destination, addresses)
	}
}

func lookupThroughTunnel(ctx context.Context, dialer N.Dialer, host string) ([]netip.Addr, error) {
	transports := make([]*transport.UDPTransport, 0, len(tunnelResolvers))
	scope := adapter.NewScope(ctx, logger.NOP())
	defer scope.Close()
	for _, server := range tunnelResolvers {
		udp := transport.NewUDPRaw(logger.NOP(), dns.NewTransportAdapter(C.DNSTypeUDP, "", nil), dialer, server)
		if err := udp.Start(adapter.StartStateStart, scope); err != nil {
			return nil, err
		}
		transports = append(transports, udp)
	}
	// A query or its answer lost in a tunnel still coming up is not the name's fault, so attempts rotate until ctx ends.
	var lastErr error
	for attempt := 0; ctx.Err() == nil; attempt++ {
		attemptCtx, cancel := context.WithTimeout(ctx, tunnelLookupAttempt)
		addresses, err := exchangeAddresses(attemptCtx, transports[attempt%len(transports)], host)
		cancel()
		if err == nil {
			return addresses, nil
		}
		lastErr = err
		var rcode dns.RcodeError
		if errors.As(err, &rcode) {
			break
		}
	}
	if lastErr == nil {
		lastErr = ctx.Err()
	}
	return nil, E.Cause(lastErr, "resolve ", host, " through the tunnel")
}

func exchangeAddresses(ctx context.Context, server adapter.DNSTransport, host string) ([]netip.Addr, error) {
	for _, queryType := range []uint16{mDNS.TypeA, mDNS.TypeAAAA} {
		query := new(mDNS.Msg)
		query.SetQuestion(mDNS.Fqdn(host), queryType)
		response, err := server.Exchange(ctx, query)
		if err != nil {
			return nil, err
		}
		if response.Rcode != mDNS.RcodeSuccess {
			return nil, dns.RcodeError(response.Rcode)
		}
		var addresses []netip.Addr
		for _, answer := range response.Answer {
			switch record := answer.(type) {
			case *mDNS.A:
				addresses = append(addresses, M.AddrFromIP(record.A))
			case *mDNS.AAAA:
				addresses = append(addresses, M.AddrFromIP(record.AAAA))
			}
		}
		if len(addresses) > 0 {
			return addresses, nil
		}
	}
	return nil, E.New("no address for ", host)
}
