//go:build !with_quic

package scan

import (
	"context"
	"errors"
	"net/netip"
)

func (p *httpPlan) probeHTTP3(ctx context.Context, session *Session, egress Egress, addr netip.Addr, port uint16, display string, result *Result) (string, error) {
	session.emit(EventStart, PhaseTLS, display, 0, "")
	return PhaseTLS, errors.New("HTTP/3 is not included in this build, rebuild with -tags with_quic")
}
