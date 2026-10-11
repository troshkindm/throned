//go:build windows

package scan

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"net/netip"
	"time"
	"unsafe"

	"github.com/sagernet/sing/common/control"

	"golang.org/x/sys/windows"
)

var (
	modIphlpapi         = windows.NewLazySystemDLL("iphlpapi.dll")
	procIcmpCreateFile  = modIphlpapi.NewProc("IcmpCreateFile")
	procIcmp6CreateFile = modIphlpapi.NewProc("Icmp6CreateFile")
	procIcmpCloseHandle = modIphlpapi.NewProc("IcmpCloseHandle")
	procIcmpSendEcho2Ex = modIphlpapi.NewProc("IcmpSendEcho2Ex")
	procIcmp6SendEcho2  = modIphlpapi.NewProc("Icmp6SendEcho2")
	procGetBestRoute2   = modIphlpapi.NewProc("GetBestRoute2")
)

const (
	// Status and RoundTripTime sit at the same offsets in ICMP_ECHO_REPLY on every architecture.
	icmpReplyStatusOffset = 4
	icmpReplyRTTOffset    = 8
	// ICMPV6_ECHO_REPLY: a 28-byte IPV6_ADDRESS_EX, then Status and RoundTripTime.
	icmp6ReplyStatusOffset = 28
	icmp6ReplyRTTOffset    = 32

	ipStatusSuccess         = 0
	ipStatusBase            = 11000
	ipStatusNetUnreachable  = 11002
	ipStatusHostUnreachable = 11003
	ipStatusTimedOut        = 11010
	ipStatusGeneralFailure  = 11050
)

var icmpPayload = []byte("abcdefghijklmnopqrstuvwabcdefghi")

func icmpAvailable(bool) error {
	return nil
}

// The iphlpapi calls block for up to the timeout; cancellation is seen between sends.
func ping(ctx context.Context, egress Egress, addr netip.Addr, timeout time.Duration, count int) (time.Duration, error) {
	controlFunc, err := egress.Control()
	if err != nil {
		return 0, err
	}
	v6 := addr.Is6()
	var source netip.Addr
	if controlFunc != nil {
		if source, err = icmpSource(egress.InterfaceFor(addr), addr); err != nil {
			return 0, err
		}
	}
	create := procIcmpCreateFile
	if v6 {
		create = procIcmp6CreateFile
	}
	handle, _, callErr := create.Call()
	if windows.Handle(handle) == windows.InvalidHandle {
		return 0, fmt.Errorf("open ICMP handle: %w", callErr)
	}
	defer procIcmpCloseHandle.Call(handle)

	reply := make([]byte, 256+len(icmpPayload))
	timeoutMs := uintptr(timeout.Milliseconds())
	var lastErr error
	for range count {
		if ctx.Err() != nil {
			return 0, ctx.Err()
		}
		var replies uintptr
		var statusOffset, rttOffset int
		if v6 {
			sourceAddr, destinationAddr := sockaddrInet6(source), sockaddrInet6(addr)
			replies, _, callErr = procIcmp6SendEcho2.Call(handle, 0, 0, 0,
				uintptr(unsafe.Pointer(&sourceAddr)), uintptr(unsafe.Pointer(&destinationAddr)),
				uintptr(unsafe.Pointer(&icmpPayload[0])), uintptr(len(icmpPayload)), 0,
				uintptr(unsafe.Pointer(&reply[0])), uintptr(len(reply)), timeoutMs)
			statusOffset, rttOffset = icmp6ReplyStatusOffset, icmp6ReplyRTTOffset
		} else {
			replies, _, callErr = procIcmpSendEcho2Ex.Call(handle, 0, 0, 0,
				uintptr(ipAddrValue(source)), uintptr(ipAddrValue(addr)),
				uintptr(unsafe.Pointer(&icmpPayload[0])), uintptr(len(icmpPayload)), 0,
				uintptr(unsafe.Pointer(&reply[0])), uintptr(len(reply)), timeoutMs)
			statusOffset, rttOffset = icmpReplyStatusOffset, icmpReplyRTTOffset
		}
		if replies == 0 {
			var errno windows.Errno
			if errors.As(callErr, &errno) && (errno == 0 || errno >= ipStatusBase) {
				lastErr = icmpStatus(errno)
			} else {
				lastErr = callErr
			}
			continue
		}
		if status := binary.LittleEndian.Uint32(reply[statusOffset:]); status != ipStatusSuccess {
			lastErr = icmpStatus(status)
			continue
		}
		return time.Duration(binary.LittleEndian.Uint32(reply[rttOffset:])) * time.Millisecond, nil
	}
	return 0, lastErr
}

// The echo APIs send from the given address as is, so pick the one the stack would choose for addr on the bound interface.
func icmpSource(ifc *control.Interface, addr netip.Addr) (netip.Addr, error) {
	if ifc == nil {
		return netip.Addr{}, ErrNoEgress
	}
	if source, ok := bestRouteSource(uint32(ifc.Index), addr); ok {
		return source, nil
	}
	var fallback netip.Addr
	for _, prefix := range ifc.Addresses {
		candidate := prefix.Addr().Unmap()
		if candidate.Is6() != addr.Is6() || !candidate.IsGlobalUnicast() {
			continue
		}
		if !candidate.Is6() || !candidate.IsPrivate() {
			return candidate, nil
		}
		if !fallback.IsValid() {
			fallback = candidate
		}
	}
	if fallback.IsValid() {
		return fallback, nil
	}
	if addr.Is6() {
		return netip.Addr{}, errNoIPv6Source
	}
	return netip.Addr{}, errNoIPv4Source
}

func bestRouteSource(ifIndex uint32, addr netip.Addr) (netip.Addr, bool) {
	var destination, best windows.RawSockaddrInet
	if addr.Is6() {
		sockaddr := (*windows.RawSockaddrInet6)(unsafe.Pointer(&destination))
		sockaddr.Family = windows.AF_INET6
		sockaddr.Addr = addr.As16()
	} else {
		sockaddr := (*windows.RawSockaddrInet4)(unsafe.Pointer(&destination))
		sockaddr.Family = windows.AF_INET
		sockaddr.Addr = addr.As4()
	}
	var route windows.MibIpForwardRow2
	ret, _, _ := procGetBestRoute2.Call(0, uintptr(ifIndex), 0, uintptr(unsafe.Pointer(&destination)), 0,
		uintptr(unsafe.Pointer(&route)), uintptr(unsafe.Pointer(&best)))
	if ret != 0 {
		return netip.Addr{}, false
	}
	var source netip.Addr
	switch best.Family {
	case windows.AF_INET:
		source = netip.AddrFrom4((*windows.RawSockaddrInet4)(unsafe.Pointer(&best)).Addr)
	case windows.AF_INET6:
		source = netip.AddrFrom16((*windows.RawSockaddrInet6)(unsafe.Pointer(&best)).Addr)
	}
	// A link-local source never gets a reply from a global target, so that case goes to the global-address fallback.
	return source, source.IsValid() && source.Is6() == addr.Is6() && (source.IsGlobalUnicast() || !addr.IsGlobalUnicast())
}

// IPAddr is the address in network byte order read as a native (little-endian) DWORD.
func ipAddrValue(addr netip.Addr) uint32 {
	if !addr.IsValid() {
		return 0
	}
	b := addr.As4()
	return binary.LittleEndian.Uint32(b[:])
}

func sockaddrInet6(addr netip.Addr) windows.RawSockaddrInet6 {
	sockaddr := windows.RawSockaddrInet6{Family: windows.AF_INET6}
	if addr.IsValid() {
		sockaddr.Addr = addr.As16()
	}
	return sockaddr
}

type icmpStatus uint32

func (s icmpStatus) Error() string {
	switch s {
	case ipStatusTimedOut:
		return "timeout"
	case ipStatusNetUnreachable:
		return "destination network unreachable"
	case ipStatusHostUnreachable:
		return "destination host unreachable"
	case 11004:
		return "destination protocol unreachable"
	case 11005:
		return "destination port unreachable"
	case 11013:
		return "TTL expired in transit"
	case ipStatusGeneralFailure:
		return "general ICMP failure"
	}
	return fmt.Sprintf("ICMP status %d", uint32(s))
}
