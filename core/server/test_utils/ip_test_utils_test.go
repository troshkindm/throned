package test_utils

import (
	"testing"
	"time"
)

func TestClockReadingReportsNothingItCannotSeparateFromNoise(t *testing.T) {
	base := time.Date(2026, 9, 5, 20, 47, 36, 0, time.UTC)
	cases := []struct {
		name    string
		reading ClockReading
		want    int64
		known   bool
	}{
		// The header names a whole second and the answer arrives after a round trip, so a
		// correct clock lands anywhere in that band. Every one of these used to print a
		// different number of seconds for a machine that was never wrong.
		{"in step", ClockReading{Served: base, Sent: base.Add(120 * time.Millisecond), Got: base.Add(400 * time.Millisecond)}, 0, true},
		{"late in the second", ClockReading{Served: base, Sent: base.Add(900 * time.Millisecond), Got: base.Add(1300 * time.Millisecond)}, 0, true},
		{"slow round trip", ClockReading{Served: base, Sent: base.Add(-200 * time.Millisecond), Got: base.Add(1800 * time.Millisecond)}, 0, true},
		{"clock ahead", ClockReading{Served: base, Sent: base.Add(9 * time.Second), Got: base.Add(9500 * time.Millisecond)}, 8000, true},
		{"clock behind", ClockReading{Served: base, Sent: base.Add(-12 * time.Second), Got: base.Add(-11 * time.Second)}, -11000, true},
		{"nothing served", ClockReading{Sent: base, Got: base.Add(time.Second)}, 0, false},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got, known := c.reading.SkewMs()
			if known != c.known || got != c.want {
				t.Fatalf("SkewMs() = %d, %v; want %d, %v", got, known, c.want, c.known)
			}
		})
	}
}
