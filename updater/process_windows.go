//go:build windows

package main

import (
	"fmt"
	"syscall"
	"time"
)

const (
	processSynchronize                  = 0x00100000
	waitObject0                         = 0x00000000
	waitTimeout                         = 0x00000102
	errorInvalidParameter syscall.Errno = 87
)

var (
	kernel32Process         = syscall.NewLazyDLL("kernel32.dll")
	procOpenProcess         = kernel32Process.NewProc("OpenProcess")
	procWaitForSingleObject = kernel32Process.NewProc("WaitForSingleObject")
	procCloseProcessHandle  = kernel32Process.NewProc("CloseHandle")
)

func waitForParent(pid int, timeout time.Duration) error {
	handle, _, callErr := procOpenProcess.Call(processSynchronize, 0, uintptr(uint32(pid)))
	if handle == 0 {
		if callErr == errorInvalidParameter {
			return nil
		}
		return fmt.Errorf("open Throned process %d: %w", pid, callErr)
	}
	defer procCloseProcessHandle.Call(handle)

	waitMilliseconds := uint32(timeout / time.Millisecond)
	result, _, callErr := procWaitForSingleObject.Call(handle, uintptr(waitMilliseconds))
	switch result {
	case waitObject0:
		return nil
	case waitTimeout:
		return fmt.Errorf("Throned process %d did not close within %s", pid, timeout)
	default:
		return fmt.Errorf("wait for Throned process %d: %w", pid, callErr)
	}
}
