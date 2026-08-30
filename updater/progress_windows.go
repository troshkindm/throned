//go:build windows

package main

import (
	"runtime"
	"sync"
	"syscall"
	"time"
	"unsafe"
)

const (
	clsctxInprocServer = 0x1
	coinitApartment    = 0x2
	progdlgNoTime      = 0x4
	progdlgNoMinimize  = 0x8
	progdlgNoCancel    = 0x40
	mbIconError        = 0x10
)

type guid struct {
	data1 uint32
	data2 uint16
	data3 uint16
	data4 [8]byte
}

var (
	clsidProgressDialog = guid{0xf8383852, 0xfcd3, 0x11d1, [8]byte{0xa6, 0xb9, 0x00, 0x60, 0x97, 0xdf, 0x5b, 0xd4}}
	iidProgressDialog   = guid{0xebbc7c04, 0x315e, 0x11d2, [8]byte{0xb6, 0x2f, 0x00, 0x60, 0x97, 0xdf, 0x5b, 0xd4}}

	ole32                = syscall.NewLazyDLL("ole32.dll")
	procCoInitializeEx   = ole32.NewProc("CoInitializeEx")
	procCoUninitialize   = ole32.NewProc("CoUninitialize")
	procCoCreateInstance = ole32.NewProc("CoCreateInstance")
	procMessageBoxW      = syscall.NewLazyDLL("user32.dll").NewProc("MessageBoxW")
)

type progressDialogVTable struct {
	queryInterface      uintptr
	addRef              uintptr
	release             uintptr
	startProgressDialog uintptr
	stopProgressDialog  uintptr
	setTitle            uintptr
	setAnimation        uintptr
	hasUserCancelled    uintptr
	setProgress         uintptr
	setProgress64       uintptr
	setLine             uintptr
	setCancelMsg        uintptr
	timer               uintptr
}

type progressDialog struct {
	vtable *progressDialogVTable
}

type progressCommand struct {
	message   string
	completed uint64
	total     uint64
	stop      bool
}

type windowsInstallProgress struct {
	commands chan progressCommand
	done     chan struct{}
	close    sync.Once
}

func newInstallProgress(delay time.Duration) *windowsInstallProgress {
	progress := &windowsInstallProgress{
		commands: make(chan progressCommand, 1),
		done:     make(chan struct{}),
	}
	go progress.run(delay)
	return progress
}

func (p *windowsInstallProgress) Update(message string, completed, total uint64) {
	command := progressCommand{message: message, completed: completed, total: total}
	select {
	case p.commands <- command:
	default:
		// Rendering cannot be allowed to slow file installation. Keep the newest
		// sample; the global percentage is monotonic so intermediate frames add no
		// information.
		select {
		case <-p.commands:
		default:
		}
		p.commands <- command
	}
}

func (p *windowsInstallProgress) Close() {
	p.close.Do(func() {
		for {
			select {
			case p.commands <- progressCommand{stop: true}:
				<-p.done
				return
			default:
				select {
				case <-p.commands:
				default:
				}
			}
		}
	})
}

func hresultFailed(result uintptr) bool {
	return int32(result) < 0
}

func (p *windowsInstallProgress) run(delay time.Duration) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	defer close(p.done)

	timer := time.NewTimer(delay)
	defer timer.Stop()
	var latest progressCommand
	var dialog *progressDialog
	comInitialized := false

	closeDialog := func() {
		if dialog != nil {
			syscall.SyscallN(dialog.vtable.stopProgressDialog, uintptr(unsafe.Pointer(dialog)))
			syscall.SyscallN(dialog.vtable.release, uintptr(unsafe.Pointer(dialog)))
			dialog = nil
		}
		if comInitialized {
			procCoUninitialize.Call()
			comInitialized = false
		}
	}
	defer closeDialog()

	apply := func() {
		if dialog == nil || latest.message == "" {
			return
		}
		line, _ := syscall.UTF16PtrFromString(latest.message)
		syscall.SyscallN(dialog.vtable.setLine, uintptr(unsafe.Pointer(dialog)), 1,
			uintptr(unsafe.Pointer(line)), 0, 0)
		total := latest.total
		if total == 0 {
			total = 1
		}
		completed := latest.completed
		if completed > total {
			completed = total
		}
		// The updater is also built for 32-bit legacy Windows. Its global scale
		// is 0..1000, so SetProgress avoids 64-bit ABI splitting entirely.
		syscall.SyscallN(dialog.vtable.setProgress, uintptr(unsafe.Pointer(dialog)),
			uintptr(uint32(completed)), uintptr(uint32(total)))
	}

	startDialog := func() {
		result, _, _ := procCoInitializeEx.Call(0, coinitApartment)
		if hresultFailed(result) {
			return
		}
		comInitialized = true
		var object unsafe.Pointer
		result, _, _ = procCoCreateInstance.Call(
			uintptr(unsafe.Pointer(&clsidProgressDialog)), 0, clsctxInprocServer,
			uintptr(unsafe.Pointer(&iidProgressDialog)), uintptr(unsafe.Pointer(&object)))
		if hresultFailed(result) || object == nil {
			return
		}
		dialog = (*progressDialog)(object)
		title, _ := syscall.UTF16PtrFromString(localize("Updating Throned", "Обновление Throned"))
		syscall.SyscallN(dialog.vtable.setTitle, uintptr(unsafe.Pointer(dialog)), uintptr(unsafe.Pointer(title)))
		syscall.SyscallN(dialog.vtable.startProgressDialog, uintptr(unsafe.Pointer(dialog)), 0, 0,
			progdlgNoTime|progdlgNoMinimize|progdlgNoCancel, 0)
		apply()
	}

	for {
		select {
		case command := <-p.commands:
			if command.stop {
				return
			}
			latest = command
			apply()
		case <-timer.C:
			startDialog()
		}
	}
}

func showUpdateError(err error) {
	message, _ := syscall.UTF16PtrFromString(localize(
		"Throned could not install the update.\n\n",
		"Throned не удалось установить обновление.\n\n") + err.Error())
	title, _ := syscall.UTF16PtrFromString(localize("Throned update failed", "Ошибка обновления Throned"))
	procMessageBoxW.Call(0, uintptr(unsafe.Pointer(message)), uintptr(unsafe.Pointer(title)), mbIconError)
}
