package main

import (
	"context"
	"testing"

	boxLog "github.com/sagernet/sing-box/log"
)

func TestUDPMonitorQuietContextOnlyDemotesBackgroundProbeLogs(t *testing.T) {
	base := context.Background()
	if got := boxLog.OverrideLevelFromContext(boxLog.LevelInfo, udpTestLogContext(base, false)); got != boxLog.LevelInfo {
		t.Fatalf("explicit UDP test log level changed to %d", got)
	}
	if got := boxLog.OverrideLevelFromContext(boxLog.LevelInfo, udpTestLogContext(base, true)); got != boxLog.LevelDebug {
		t.Fatalf("background UDP monitor log level = %d, want debug", got)
	}
}
