//go:build !windows

package main

import (
	"errors"
	"fmt"
	"os"
	"syscall"
	"time"
)

func waitForParent(pid int, timeout time.Duration) error {
	process, err := os.FindProcess(pid)
	if err != nil {
		return nil
	}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		err = process.Signal(syscall.Signal(0))
		if errors.Is(err, os.ErrProcessDone) || errors.Is(err, syscall.ESRCH) {
			return nil
		}
		if err != nil {
			return fmt.Errorf("check Throned process %d: %w", pid, err)
		}
		time.Sleep(50 * time.Millisecond)
	}
	return fmt.Errorf("Throned process %d did not close within %s", pid, timeout)
}
