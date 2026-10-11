//go:build !windows

package scan

import (
	"errors"
	"slices"
	"syscall"
)

var routingErrnos = []syscall.Errno{syscall.ENETUNREACH, syscall.EHOSTUNREACH, syscall.EADDRNOTAVAIL, syscall.EAFNOSUPPORT, syscall.ENETDOWN}

func routingError(err error) bool {
	var errno syscall.Errno
	return errors.As(err, &errno) && slices.Contains(routingErrnos, errno)
}
