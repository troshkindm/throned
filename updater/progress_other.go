//go:build !windows

package main

import "time"

type quietInstallProgress struct{}

func newInstallProgress(_ time.Duration) *quietInstallProgress {
	return &quietInstallProgress{}
}

func (*quietInstallProgress) Update(_ string, _, _ uint64) {}
func (*quietInstallProgress) Close()                       {}
func showUpdateError(_ error)                              {}
