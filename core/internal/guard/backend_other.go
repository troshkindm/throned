//go:build !windows && !linux && !darwin

package guard

import "runtime"

func newBackend() (backend, error) {
	return nil, newError(CodeUnsupported, "the kill switch is not supported on %s", runtime.GOOS)
}
