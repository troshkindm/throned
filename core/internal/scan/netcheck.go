package scan

import (
	"context"
	"errors"
	"net"
	"time"
)

func CheckNetwork(ctx context.Context, egress Egress, targets []string, timeout time.Duration) (bool, error) {
	if len(targets) == 0 {
		return false, errors.New("no network check targets")
	}
	controlFunc, err := egress.Control()
	if err != nil {
		return false, err
	}
	ctx, cancel := context.WithTimeout(ctx, clampTimeout(timeout, 3*time.Second))
	defer cancel()
	failures := make(chan error, len(targets))
	for _, target := range targets {
		go func() {
			dialer := net.Dialer{Control: controlFunc}
			conn, dialErr := dialer.DialContext(ctx, "tcp", target)
			if dialErr == nil {
				_ = conn.Close()
				cancel()
			}
			failures <- dialErr
		}()
	}
	var firstErr error
	for range targets {
		if dialErr := <-failures; dialErr == nil {
			return true, nil
		} else if firstErr == nil {
			firstErr = dialErr
		}
	}
	return false, errors.New("no network check target answered: " + errorText(firstErr))
}
