/* SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019-2026 WireGuard LLC. All Rights Reserved.
 */

package guard

import (
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	layerALEAuthConnectV4    = windows.GUID{Data1: 0xc38d57d1, Data2: 0x05a7, Data3: 0x4c33, Data4: [8]byte{0x90, 0x4f, 0x7f, 0xbc, 0xee, 0xe6, 0x0e, 0x82}}
	layerALEAuthConnectV6    = windows.GUID{Data1: 0x4a72393b, Data2: 0x319f, Data3: 0x44bc, Data4: [8]byte{0x84, 0xc3, 0xba, 0x54, 0xdc, 0xb3, 0xb6, 0xb4}}
	layerALEAuthRecvAcceptV4 = windows.GUID{Data1: 0xe1cd9fe7, Data2: 0xf4b5, Data3: 0x4273, Data4: [8]byte{0x96, 0xc0, 0x59, 0x2e, 0x48, 0x7b, 0x86, 0x50}}
	layerALEAuthRecvAcceptV6 = windows.GUID{Data1: 0xa3b42c97, Data2: 0x9f04, Data3: 0x4672, Data4: [8]byte{0xb8, 0x7e, 0xce, 0xe9, 0xc4, 0x83, 0x25, 0x7f}}
	layerIPForwardV4         = windows.GUID{Data1: 0xa82acc24, Data2: 0x4ee1, Data3: 0x4ee1, Data4: [8]byte{0xb4, 0x65, 0xfd, 0x1d, 0x25, 0xcb, 0x10, 0xa4}}
	layerIPForwardV6         = windows.GUID{Data1: 0x7b964818, Data2: 0x19c7, Data3: 0x493a, Data4: [8]byte{0xb7, 0x1f, 0x83, 0x2c, 0x36, 0x84, 0xd2, 0x8c}}

	conditionALEAppID                  = windows.GUID{Data1: 0xd78e1e87, Data2: 0x8644, Data3: 0x4ea5, Data4: [8]byte{0x94, 0x37, 0xd8, 0x09, 0xec, 0xef, 0xc9, 0x71}}
	conditionFlags                     = windows.GUID{Data1: 0x632ce23b, Data2: 0x5167, Data3: 0x435c, Data4: [8]byte{0x86, 0xd7, 0xe9, 0x03, 0x68, 0x4a, 0xa8, 0x0c}}
	conditionIPLocalInterface          = windows.GUID{Data1: 0x4cd62a49, Data2: 0x59c3, Data3: 0x4969, Data4: [8]byte{0xb7, 0xf3, 0xbd, 0xa5, 0xd3, 0x28, 0x90, 0xa4}}
	conditionIPLocalAddress            = windows.GUID{Data1: 0xd9ee00de, Data2: 0xc1ef, Data3: 0x4617, Data4: [8]byte{0xbf, 0xe3, 0xff, 0xd8, 0xf5, 0xa0, 0x89, 0x57}}
	conditionIPRemoteAddress           = windows.GUID{Data1: 0xb235ae9a, Data2: 0x1d64, Data3: 0x49b8, Data4: [8]byte{0xa4, 0x4c, 0x5f, 0xf3, 0xd9, 0x09, 0x50, 0x45}}
	conditionIPProtocol                = windows.GUID{Data1: 0x3971ef2b, Data2: 0x623e, Data3: 0x4f9a, Data4: [8]byte{0x8c, 0xb1, 0x6e, 0x79, 0xb8, 0x06, 0xb9, 0xa7}}
	conditionIPLocalPort               = windows.GUID{Data1: 0x0c1ba1af, Data2: 0x5765, Data3: 0x453f, Data4: [8]byte{0xaf, 0x22, 0xa8, 0xf7, 0x91, 0xac, 0x77, 0x5b}}
	conditionIPRemotePort              = windows.GUID{Data1: 0xc35a604d, Data2: 0xd22b, Data3: 0x4e1a, Data4: [8]byte{0x91, 0xb4, 0x68, 0xf6, 0x74, 0xee, 0x67, 0x4b}}
	conditionIPSourceAddress           = windows.GUID{Data1: 0xae96897e, Data2: 0x2e94, Data3: 0x4bc9, Data4: [8]byte{0xb3, 0x13, 0xb2, 0x7e, 0xe8, 0x0e, 0x57, 0x4d}}
	conditionIPDestinationAddress      = windows.GUID{Data1: 0x2d79133b, Data2: 0xb390, Data3: 0x45c6, Data4: [8]byte{0x86, 0x99, 0xac, 0xac, 0xea, 0xaf, 0xed, 0x33}}
	conditionSourceInterfaceIndex      = windows.GUID{Data1: 0x2311334d, Data2: 0xc92d, Data3: 0x45bf, Data4: [8]byte{0x94, 0x96, 0xed, 0xf4, 0x47, 0x82, 0x0e, 0x2d}}
	conditionDestinationInterfaceIndex = windows.GUID{Data1: 0x35cf6522, Data2: 0x4139, Data3: 0x45ee, Data4: [8]byte{0xa0, 0xd5, 0x67, 0xb8, 0x09, 0x49, 0xd8, 0x79}}

	// ICMP carries its type and code in the port fields (fwpmu.h aliases them).
	conditionICMPType = conditionIPLocalPort
	conditionICMPCode = conditionIPRemotePort
)

const (
	fwpUint8           = 1
	fwpUint16          = 2
	fwpUint32          = 3
	fwpUint64          = 4
	fwpByteArray16Type = 11
	fwpByteBlobType    = 12
	fwpV4AddrMask      = 0x100
	fwpV6AddrMask      = 0x101

	fwpMatchEqual       = 0
	fwpMatchFlagsAllSet = 6

	fwpActionBlock  = 0x1001
	fwpActionPermit = 0x1002

	fwpConditionFlagIsLoopback = 0x1
	fwpmSessionFlagDynamic     = 0x1
	rpcCAuthnWinNT             = 10
)

type fwpByteBlob struct {
	size uint32
	data *uint8
}

type fwpV4AddrAndMask struct {
	addr uint32
	mask uint32
}

type fwpV6AddrAndMask struct {
	addr         [16]uint8
	prefixLength uint8
}

// FWP_VALUE0 and FWP_CONDITION_VALUE0: scalars up to 32 bits sit in value, wider types are pointers.
type fwpValue0 struct {
	typ   uint32
	value uintptr
}

type fwpmDisplayData0 struct {
	name        *uint16
	description *uint16
}

type fwpmSession0 struct {
	sessionKey           windows.GUID
	displayData          fwpmDisplayData0
	flags                uint32
	txnWaitTimeoutInMSec uint32
	processID            uint32
	sid                  *windows.SID
	username             *uint16
	kernelMode           int32
}

type fwpmProvider0 struct {
	providerKey  windows.GUID
	displayData  fwpmDisplayData0
	flags        uint32
	providerData fwpByteBlob
	serviceName  *uint16
}

type fwpmSublayer0 struct {
	subLayerKey  windows.GUID
	displayData  fwpmDisplayData0
	flags        uint32
	providerKey  *windows.GUID
	providerData fwpByteBlob
	weight       uint16
}

type fwpmFilterCondition0 struct {
	fieldKey       windows.GUID
	matchType      uint32
	conditionValue fwpValue0
}

type fwpmAction0 struct {
	typ        uint32
	filterType windows.GUID
}

type fwpmFilter0 struct {
	filterKey           windows.GUID
	displayData         fwpmDisplayData0
	flags               uint32
	providerKey         *windows.GUID
	providerData        fwpByteBlob
	layerKey            windows.GUID
	subLayerKey         windows.GUID
	weight              fwpValue0
	numFilterConditions uint32
	filterCondition     *fwpmFilterCondition0
	action              fwpmAction0
	// C 8-aligns the context union and filterId; Go 4-aligns GUIDs, and uint64 on 386.
	_                  [4]byte
	providerContextKey windows.GUID
	reserved           *windows.GUID
	_                  [8 - unsafe.Sizeof(uintptr(0))]byte
	filterID           uint64
	effectiveWeight    fwpValue0
}

var (
	modfwpuclnt = windows.NewLazySystemDLL("fwpuclnt.dll")

	procFwpmEngineOpen0           = modfwpuclnt.NewProc("FwpmEngineOpen0")
	procFwpmEngineClose0          = modfwpuclnt.NewProc("FwpmEngineClose0")
	procFwpmTransactionBegin0     = modfwpuclnt.NewProc("FwpmTransactionBegin0")
	procFwpmTransactionCommit0    = modfwpuclnt.NewProc("FwpmTransactionCommit0")
	procFwpmTransactionAbort0     = modfwpuclnt.NewProc("FwpmTransactionAbort0")
	procFwpmProviderAdd0          = modfwpuclnt.NewProc("FwpmProviderAdd0")
	procFwpmSubLayerAdd0          = modfwpuclnt.NewProc("FwpmSubLayerAdd0")
	procFwpmFilterAdd0            = modfwpuclnt.NewProc("FwpmFilterAdd0")
	procFwpmFilterDeleteById0     = modfwpuclnt.NewProc("FwpmFilterDeleteById0")
	procFwpmFilterGetById0        = modfwpuclnt.NewProc("FwpmFilterGetById0")
	procFwpmFreeMemory0           = modfwpuclnt.NewProc("FwpmFreeMemory0")
	procFwpmGetAppIdFromFileName0 = modfwpuclnt.NewProc("FwpmGetAppIdFromFileName0")
)

// WFP returns its error code rather than setting the thread's last error.
func wfpError(r1 uintptr) error {
	if r1 != 0 {
		return windows.Errno(r1)
	}
	return nil
}

func fwpmEngineOpen0(session *fwpmSession0, engine *uintptr) error {
	r1, _, _ := syscall.SyscallN(procFwpmEngineOpen0.Addr(), 0, rpcCAuthnWinNT, 0, uintptr(unsafe.Pointer(session)), uintptr(unsafe.Pointer(engine)))
	return wfpError(r1)
}

func fwpmEngineClose0(engine uintptr) error {
	r1, _, _ := syscall.SyscallN(procFwpmEngineClose0.Addr(), engine)
	return wfpError(r1)
}

func fwpmTransactionBegin0(engine uintptr) error {
	r1, _, _ := syscall.SyscallN(procFwpmTransactionBegin0.Addr(), engine, 0)
	return wfpError(r1)
}

func fwpmTransactionCommit0(engine uintptr) error {
	r1, _, _ := syscall.SyscallN(procFwpmTransactionCommit0.Addr(), engine)
	return wfpError(r1)
}

func fwpmTransactionAbort0(engine uintptr) error {
	r1, _, _ := syscall.SyscallN(procFwpmTransactionAbort0.Addr(), engine)
	return wfpError(r1)
}

func fwpmProviderAdd0(engine uintptr, provider *fwpmProvider0) error {
	r1, _, _ := syscall.SyscallN(procFwpmProviderAdd0.Addr(), engine, uintptr(unsafe.Pointer(provider)), 0)
	return wfpError(r1)
}

func fwpmSubLayerAdd0(engine uintptr, sublayer *fwpmSublayer0) error {
	r1, _, _ := syscall.SyscallN(procFwpmSubLayerAdd0.Addr(), engine, uintptr(unsafe.Pointer(sublayer)), 0)
	return wfpError(r1)
}

func fwpmFilterAdd0(engine uintptr, filter *fwpmFilter0, id *uint64) error {
	r1, _, _ := syscall.SyscallN(procFwpmFilterAdd0.Addr(), engine, uintptr(unsafe.Pointer(filter)), 0, uintptr(unsafe.Pointer(id)))
	return wfpError(r1)
}

// A UINT64 passed by value takes two stack slots on 386, low half first.
func fwpmFilterDeleteById0(engine uintptr, id uint64) error {
	var r1 uintptr
	if unsafe.Sizeof(uintptr(0)) == 8 {
		r1, _, _ = syscall.SyscallN(procFwpmFilterDeleteById0.Addr(), engine, uintptr(id))
	} else {
		r1, _, _ = syscall.SyscallN(procFwpmFilterDeleteById0.Addr(), engine, uintptr(id), uintptr(id>>32))
	}
	return wfpError(r1)
}

func fwpmFilterGetById0(engine uintptr, id uint64, filter **fwpmFilter0) error {
	var r1 uintptr
	if unsafe.Sizeof(uintptr(0)) == 8 {
		r1, _, _ = syscall.SyscallN(procFwpmFilterGetById0.Addr(), engine, uintptr(id), uintptr(unsafe.Pointer(filter)))
	} else {
		r1, _, _ = syscall.SyscallN(procFwpmFilterGetById0.Addr(), engine, uintptr(id), uintptr(id>>32), uintptr(unsafe.Pointer(filter)))
	}
	return wfpError(r1)
}

func fwpmFreeMemory0(p unsafe.Pointer) {
	syscall.SyscallN(procFwpmFreeMemory0.Addr(), uintptr(p))
}

func fwpmGetAppIdFromFileName0(fileName *uint16, appID **fwpByteBlob) error {
	r1, _, _ := syscall.SyscallN(procFwpmGetAppIdFromFileName0.Addr(), uintptr(unsafe.Pointer(fileName)), uintptr(unsafe.Pointer(appID)))
	return wfpError(r1)
}
