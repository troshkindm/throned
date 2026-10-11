package scan

import (
	"errors"
	"net"
	"net/netip"
)

var (
	errICMPNotPermitted = errors.New("ICMP is not permitted: run privileged or allow net.ipv4.ping_group_range")
	errNoIPv4Source     = errors.New("the default interface has no IPv4 address")
	errNoIPv6Source     = errors.New("the default interface has no IPv6 address")
)

func localFailure(egress Egress, addr netip.Addr, err error) bool {
	if errors.Is(err, errICMPNotPermitted) || errors.Is(err, errNoIPv4Source) || errors.Is(err, errNoIPv6Source) {
		return true
	}
	return routingError(err) && routeMissing(egress, addr)
}

// A remote unreachable can raise the same errno; a UDP connect sends nothing but repeats the local route lookup.
func routeMissing(egress Egress, addr netip.Addr) bool {
	controlFunc, err := egress.Control()
	if err != nil {
		return true
	}
	network := "udp4"
	if addr.Is6() {
		network = "udp6"
	}
	conn, err := (&net.Dialer{Control: controlFunc}).Dial(network, netip.AddrPortFrom(addr, 9).String())
	if err != nil {
		return true
	}
	_ = conn.Close()
	return false
}
