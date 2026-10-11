//go:build darwin || linux

package scan

import (
	"context"
	"errors"
	"math/rand/v2"
	"net"
	"net/netip"
	"os"
	"syscall"
	"time"

	"github.com/sagernet/sing/common/control"

	"golang.org/x/net/icmp"
	"golang.org/x/net/ipv4"
	"golang.org/x/net/ipv6"
)

const (
	protocolICMP   = 1
	protocolICMPv6 = 58
)

var icmpPayload = []byte("abcdefghijklmnopqrstuvwabcdefghi")

func ping(ctx context.Context, egress Egress, addr netip.Addr, timeout time.Duration, count int) (time.Duration, error) {
	controlFunc, err := egress.Control()
	if err != nil {
		return 0, err
	}
	conn, raw, err := openICMP(ctx, addr, controlFunc)
	if err != nil {
		return 0, err
	}
	defer func() { _ = conn.Close() }()
	stop := context.AfterFunc(ctx, func() { _ = conn.SetReadDeadline(time.Unix(1, 0)) })
	defer stop()

	v6 := addr.Is6()
	protocol, requestType, replyType := protocolICMP, icmp.Type(ipv4.ICMPTypeEcho), icmp.Type(ipv4.ICMPTypeEchoReply)
	if v6 {
		protocol, requestType, replyType = protocolICMPv6, ipv6.ICMPTypeEchoRequest, ipv6.ICMPTypeEchoReply
	}
	var destination net.Addr = &net.UDPAddr{IP: addr.AsSlice()}
	if raw {
		destination = &net.IPAddr{IP: addr.AsSlice()}
	}
	// Datagram sockets get their ID rewritten by the kernel, so only raw sockets can match on it.
	id := int(rand.Uint32() & 0xffff)
	buffer := make([]byte, 1500)
	lastErr := error(os.ErrDeadlineExceeded)
	for seq := 1; seq <= count; seq++ {
		message := icmp.Message{Type: requestType, Body: &icmp.Echo{ID: id, Seq: seq, Data: icmpPayload}}
		packet, err := message.Marshal(nil)
		if err != nil {
			return 0, err
		}
		sent := time.Now()
		if _, err = conn.WriteTo(packet, destination); err != nil {
			return 0, contextOr(ctx, err)
		}
		_ = conn.SetReadDeadline(sent.Add(timeout))
		if ctx.Err() != nil {
			return 0, ctx.Err()
		}
		for {
			n, peer, err := conn.ReadFrom(buffer)
			if err != nil {
				if ctx.Err() != nil {
					return 0, ctx.Err()
				}
				if errors.Is(err, os.ErrDeadlineExceeded) {
					lastErr = err
					break
				}
				return 0, err
			}
			if peerAddr(peer) != addr {
				continue
			}
			reply, err := icmp.ParseMessage(protocol, stripIPv4Header(buffer[:n], v6))
			if err != nil || reply.Type != replyType {
				continue
			}
			echo, ok := reply.Body.(*icmp.Echo)
			if !ok || echo.Seq != seq || (raw && echo.ID != id) {
				continue
			}
			return time.Since(sent), nil
		}
	}
	return 0, lastErr
}

func icmpAvailable(v6 bool) error {
	addr := netip.IPv4Unspecified()
	if v6 {
		addr = netip.IPv6Unspecified()
	}
	conn, _, err := openICMP(context.Background(), addr, nil)
	if err != nil {
		if errors.Is(err, errICMPNotPermitted) {
			return err
		}
		return nil
	}
	_ = conn.Close()
	return nil
}

// Darwin's IPv4 datagram sockets deliver the IP header; an ICMP message never starts with version nibble 4.
func stripIPv4Header(packet []byte, v6 bool) []byte {
	if v6 || len(packet) < 20 || packet[0]>>4 != 4 {
		return packet
	}
	headerLength := int(packet[0]&0x0f) * 4
	if headerLength < 20 || headerLength > len(packet) {
		return packet
	}
	return packet[headerLength:]
}

func peerAddr(addr net.Addr) netip.Addr {
	var ip net.IP
	switch typed := addr.(type) {
	case *net.UDPAddr:
		ip = typed.IP
	case *net.IPAddr:
		ip = typed.IP
	default:
		return netip.Addr{}
	}
	parsed, _ := netip.AddrFromSlice(ip)
	return parsed.Unmap()
}

func openICMP(ctx context.Context, addr netip.Addr, controlFunc control.Func) (net.PacketConn, bool, error) {
	conn, err := openICMPDatagram(addr, controlFunc)
	if err == nil {
		return conn, false, nil
	}
	if !errors.Is(err, syscall.EACCES) && !errors.Is(err, syscall.EPERM) && !errors.Is(err, syscall.EPROTONOSUPPORT) {
		return nil, false, err
	}
	network := "ip4:icmp"
	if addr.Is6() {
		network = "ip6:ipv6-icmp"
	}
	rawConn, err := (&net.ListenConfig{Control: controlFunc}).ListenPacket(ctx, network, "")
	if err != nil {
		if errors.Is(err, syscall.EACCES) || errors.Is(err, syscall.EPERM) {
			return nil, false, errICMPNotPermitted
		}
		return nil, false, err
	}
	return rawConn, true, nil
}

// x/net's icmp.ListenPacket, with the egress control applied before bind.
func openICMPDatagram(addr netip.Addr, controlFunc control.Func) (net.PacketConn, error) {
	family, protocol, network := syscall.AF_INET, protocolICMP, "udp4"
	var sockaddr syscall.Sockaddr = &syscall.SockaddrInet4{}
	if addr.Is6() {
		family, protocol, network = syscall.AF_INET6, protocolICMPv6, "udp6"
		sockaddr = &syscall.SockaddrInet6{}
	}
	fd, err := syscall.Socket(family, syscall.SOCK_DGRAM, protocol)
	if err != nil {
		return nil, os.NewSyscallError("socket", err)
	}
	syscall.CloseOnExec(fd)
	file := os.NewFile(uintptr(fd), "icmp")
	defer func() { _ = file.Close() }()
	rawConn, err := file.SyscallConn()
	if err != nil {
		return nil, err
	}
	if controlFunc != nil {
		if err = controlFunc(network, netip.AddrPortFrom(addr, 0).String(), rawConn); err != nil {
			return nil, err
		}
	}
	var bindErr error
	if err = rawConn.Control(func(fd uintptr) { bindErr = syscall.Bind(int(fd), sockaddr) }); err != nil {
		return nil, err
	}
	if bindErr != nil {
		return nil, os.NewSyscallError("bind", bindErr)
	}
	return net.FilePacketConn(file)
}
