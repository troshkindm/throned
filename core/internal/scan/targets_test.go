package scan

import (
	"net/netip"
	"testing"
)

func smallSpec(shuffle bool, seed uint64) Spec {
	return Spec{
		Entries: []Entry{
			{CIDR: "10.0.0.0/29"},
			{CIDR: "192.168.1.7", Port: 8443},
			{CIDR: "2001:db8::/126"},
			{CIDR: "172.16.5.0/30", Port: 22},
		},
		DefaultPorts: []int32{443, 80, 443},
		Shuffle:      shuffle,
		Seed:         seed,
	}
}

func collect(t *testing.T, targets *Targets, from, to uint64) []Target {
	t.Helper()
	out := make([]Target, 0, to-from)
	for position := from; position < to; position++ {
		out = append(out, targets.At(position))
	}
	return out
}

func TestTargetsBijection(t *testing.T) {
	for _, shuffle := range []bool{false, true} {
		targets := NewTargets(smallSpec(shuffle, 42))
		// 8 hosts x 2 ports + 1 x 1 + 4 x 2 + 4 x 1
		if targets.Total() != 29 {
			t.Fatalf("shuffle=%v: total %d, want 29", shuffle, targets.Total())
		}
		seen := make(map[Target]bool)
		for _, target := range collect(t, targets, 0, targets.Total()) {
			if seen[target] {
				t.Fatalf("shuffle=%v: %v repeated", shuffle, target)
			}
			seen[target] = true
		}
		want := []Target{
			{netip.MustParseAddr("10.0.0.0"), 443},
			{netip.MustParseAddr("10.0.0.7"), 80},
			{netip.MustParseAddr("192.168.1.7"), 8443},
			{netip.MustParseAddr("2001:db8::3"), 443},
			{netip.MustParseAddr("172.16.5.3"), 22},
		}
		for _, target := range want {
			if !seen[target] {
				t.Fatalf("shuffle=%v: %v missing", shuffle, target)
			}
		}
	}
}

func TestTargetsPortModes(t *testing.T) {
	entries := []Entry{{CIDR: "10.0.0.1", Port: 8443}, {CIDR: "10.0.0.2"}, {CIDR: "10.0.0.3", Port: 443}}
	ports := func(mode string, defaults []int32) map[string][]uint16 {
		targets := NewTargets(Spec{Entries: entries, DefaultPorts: defaults, PortMode: mode})
		out := make(map[string][]uint16)
		for _, target := range collect(t, targets, 0, targets.Total()) {
			out[target.Addr.String()] = append(out[target.Addr.String()], target.Port)
		}
		return out
	}
	cases := []struct {
		mode     string
		defaults []int32
		want     map[string][]uint16
	}{
		{PortModeEntry, []int32{443, 80}, map[string][]uint16{"10.0.0.1": {8443}, "10.0.0.2": {443, 80}, "10.0.0.3": {443}}},
		{PortModeIgnore, []int32{443, 80}, map[string][]uint16{"10.0.0.1": {443, 80}, "10.0.0.2": {443, 80}, "10.0.0.3": {443, 80}}},
		{PortModeMerge, []int32{443, 80}, map[string][]uint16{"10.0.0.1": {8443, 443, 80}, "10.0.0.2": {443, 80}, "10.0.0.3": {443, 80}}},
		{PortModeList, []int32{443, 80}, map[string][]uint16{"10.0.0.1": {8443}, "10.0.0.2": {0}, "10.0.0.3": {443}}},
		{PortModeIgnore, nil, map[string][]uint16{"10.0.0.1": {0}, "10.0.0.2": {0}, "10.0.0.3": {0}}},
		{PortModeMerge, nil, map[string][]uint16{"10.0.0.1": {8443}, "10.0.0.2": {0}, "10.0.0.3": {443}}},
	}
	for _, c := range cases {
		got := ports(c.mode, c.defaults)
		for addr, want := range c.want {
			if len(got[addr]) != len(want) {
				t.Fatalf("mode %q %s: %v, want %v", c.mode, addr, got[addr], want)
			}
			for i := range want {
				if got[addr][i] != want[i] {
					t.Fatalf("mode %q %s: %v, want %v", c.mode, addr, got[addr], want)
				}
			}
		}
	}
}

func TestTargetsUnshuffledOrder(t *testing.T) {
	targets := NewTargets(smallSpec(false, 1))
	got := collect(t, targets, 0, 4)
	want := []Target{
		{netip.MustParseAddr("10.0.0.0"), 443},
		{netip.MustParseAddr("10.0.0.0"), 80},
		{netip.MustParseAddr("10.0.0.1"), 443},
		{netip.MustParseAddr("10.0.0.1"), 80},
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("position %d: %v, want %v", i, got[i], want[i])
		}
	}
}

func TestTargetsShuffleLargeBijection(t *testing.T) {
	spec := Spec{Entries: []Entry{{CIDR: "10.0.0.0/22"}, {CIDR: "10.8.0.0/23"}}, DefaultPorts: []int32{1, 2, 3}, Shuffle: true, Seed: 7}
	targets := NewTargets(spec)
	if targets.Total() != (1024+512)*3 {
		t.Fatalf("total %d", targets.Total())
	}
	seen := make(map[Target]bool, targets.Total())
	inOrder := 0
	for position, target := range collect(t, targets, 0, targets.Total()) {
		if seen[target] {
			t.Fatalf("%v repeated", target)
		}
		seen[target] = true
		if NewTargets(Spec{Entries: spec.Entries, DefaultPorts: spec.DefaultPorts}).At(uint64(position)) == target {
			inOrder++
		}
	}
	if inOrder > int(targets.Total())/10 {
		t.Fatalf("%d of %d positions unmoved by the shuffle", inOrder, targets.Total())
	}
}

func TestTargetsDeterministicBySeed(t *testing.T) {
	a := collect(t, NewTargets(smallSpec(true, 99)), 0, 29)
	b := collect(t, NewTargets(smallSpec(true, 99)), 0, 29)
	c := collect(t, NewTargets(smallSpec(true, 100)), 0, 29)
	same := true
	for i := range a {
		if a[i] != b[i] {
			t.Fatalf("position %d differs for the same seed", i)
		}
		if a[i] != c[i] {
			same = false
		}
	}
	if same {
		t.Fatal("a different seed produced the same order")
	}
}

func TestTargetsSampling(t *testing.T) {
	prefix := netip.MustParsePrefix("100.64.0.0/16")
	targets := NewTargets(Spec{
		Entries:          []Entry{{CIDR: prefix.String()}, {CIDR: "100.65.0.0/28"}},
		DefaultPorts:     []int32{443},
		MaxHostsPerEntry: 100,
		Seed:             3,
	})
	if targets.Total() != 100+16 {
		t.Fatalf("total %d, want 116", targets.Total())
	}
	if targets.SampledEntries != 1 {
		t.Fatalf("sampled %d, want 1", targets.SampledEntries)
	}
	seen := make(map[Target]bool)
	for position, target := range collect(t, targets, 0, targets.Total()) {
		if seen[target] {
			t.Fatalf("%v repeated", target)
		}
		seen[target] = true
		if position < 100 && !prefix.Contains(target.Addr) {
			t.Fatalf("%v outside %v", target.Addr, prefix)
		}
	}
	other := NewTargets(Spec{Entries: []Entry{{CIDR: prefix.String()}}, MaxHostsPerEntry: 100, Seed: 4})
	differs := false
	for position := range uint64(100) {
		if other.At(position).Addr != targets.At(position).Addr {
			differs = true
		}
	}
	if !differs {
		t.Fatal("the sample does not depend on the seed")
	}
}

func TestTargetsIPv6Sampling(t *testing.T) {
	prefix := netip.MustParsePrefix("2001:db8:1234::/48")
	targets := NewTargets(Spec{Entries: []Entry{{CIDR: prefix.String()}}, Seed: 11})
	if targets.Total() != defaultSampleSize {
		t.Fatalf("total %d, want %d", targets.Total(), defaultSampleSize)
	}
	if targets.SampledEntries != 1 {
		t.Fatalf("sampled %d", targets.SampledEntries)
	}
	seen := make(map[netip.Addr]bool)
	highBitsUsed := false
	for _, target := range collect(t, targets, 0, targets.Total()) {
		if !prefix.Contains(target.Addr) {
			t.Fatalf("%v outside %v", target.Addr, prefix)
		}
		if seen[target.Addr] {
			t.Fatalf("%v repeated", target.Addr)
		}
		seen[target.Addr] = true
		b := target.Addr.As16()
		if b[6] != 0 || b[7] != 0 {
			highBitsUsed = true
		}
		if target.Port != 0 {
			t.Fatalf("port %d, want 0", target.Port)
		}
	}
	if !highBitsUsed {
		t.Fatal("the sample never varies the subnet bits above /64")
	}

	capped := NewTargets(Spec{Entries: []Entry{{CIDR: prefix.String()}}, MaxHostsPerEntry: 5000, DefaultPorts: []int32{443, 2053}})
	if capped.Total() != 10000 {
		t.Fatalf("capped total %d", capped.Total())
	}
}

func TestTargetsCursorResume(t *testing.T) {
	spec := Spec{Entries: []Entry{{CIDR: "10.1.0.0/24"}, {CIDR: "2001:db8::/120"}}, DefaultPorts: []int32{443, 8443}, Shuffle: true, Seed: 5}
	whole := collect(t, NewTargets(spec), 100, 400)
	first := collect(t, NewTargets(spec), 100, 250)
	second := collect(t, NewTargets(spec), 250, 400)
	resumed := append(first, second...)
	for i := range whole {
		if whole[i] != resumed[i] {
			t.Fatalf("position %d: %v vs %v", 100+i, whole[i], resumed[i])
		}
	}
}

func TestTargetsInvalidAndNormalized(t *testing.T) {
	targets := NewTargets(Spec{Entries: []Entry{
		{CIDR: "not-an-ip"},
		{CIDR: "10.0.0.1/33"},
		{CIDR: "1.2.3.4", Port: 70000},
		{CIDR: " ::ffff:10.9.8.7 "},
		{CIDR: "10.20.30.40/30"},
	}})
	if targets.InvalidEntries != 3 {
		t.Fatalf("invalid %d, want 3", targets.InvalidEntries)
	}
	if targets.Total() != 5 {
		t.Fatalf("total %d, want 5", targets.Total())
	}
	if got := targets.At(0); got.Addr != netip.MustParseAddr("10.9.8.7") || got.Port != 0 {
		t.Fatalf("mapped address %v", got)
	}
	if got := targets.At(1); got.Addr != netip.MustParseAddr("10.20.30.40") {
		t.Fatalf("unmasked prefix %v", got)
	}
}

func TestTargetsSaturate(t *testing.T) {
	ports := make([]int32, 0, 1000)
	for port := range int32(1000) {
		ports = append(ports, port+1)
	}
	targets := NewTargets(Spec{
		Entries:          []Entry{{CIDR: "2001:db8::/32"}, {CIDR: "2001:db9::/32"}},
		DefaultPorts:     ports,
		MaxHostsPerEntry: 1 << 60,
		Shuffle:          true,
	})
	if targets.Total() != maxTargetTotal {
		t.Fatalf("total %d, want %d", targets.Total(), maxTargetTotal)
	}
	for _, position := range []uint64{0, 1, maxTargetTotal / 2, maxTargetTotal - 1} {
		if target := targets.At(position); !target.Addr.Is6() {
			t.Fatalf("position %d: %v", position, target)
		}
	}
}

func TestMixerBijection(t *testing.T) {
	for width := 1; width <= 12; width++ {
		m := newMixer(width, uint64(width)*977)
		seen := make(map[uint64]bool)
		for x := range uint64(1) << width {
			y := m.mix(x)
			if y >= uint64(1)<<width || seen[y] {
				t.Fatalf("width %d: %d -> %d", width, x, y)
			}
			seen[y] = true
		}
	}
}
