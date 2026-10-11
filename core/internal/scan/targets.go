package scan

import (
	"encoding/binary"
	"math/bits"
	"net/netip"
	"sort"
	"strings"
)

type Entry struct {
	CIDR string
	// 0 = none; Spec.PortMode decides how it combines with Spec.DefaultPorts.
	Port int32
}

type Spec struct {
	Entries          []Entry
	DefaultPorts     []int32
	PortMode         string
	MaxHostsPerEntry int64
	Shuffle          bool
	Seed             uint64
}

const (
	// PortModeEntry (the zero value): the entry's port, else every default port.
	PortModeEntry  = ""
	PortModeIgnore = "ignore"
	PortModeMerge  = "merge"
	PortModeList   = "list"
)

type Target struct {
	Addr netip.Addr
	// 0 = the phase applies its own fallback.
	Port uint16
}

const (
	maxTargetTotal    = uint64(1) << 62
	defaultSampleSize = 1024
	// Wider entries are always sampled, whatever the spec says.
	maxEnumeratedBits = 32
	mixerRounds       = 4
)

type targetRange struct {
	base     netip.Addr
	hostBits int
	sampled  bool
	ports    []uint16
	start    uint64
	sampler  mixer
	highKey  uint64
}

type Targets struct {
	ranges  []targetRange
	total   uint64
	shuffle bool
	order   mixer

	SampledEntries int
	InvalidEntries int
}

func NewTargets(spec Spec) *Targets {
	t := &Targets{shuffle: spec.Shuffle}
	defaultPorts := normalizePorts(spec.DefaultPorts)
	var limit uint64
	if spec.MaxHostsPerEntry > 0 {
		limit = uint64(spec.MaxHostsPerEntry)
	}
	for _, entry := range spec.Entries {
		prefix, ok := parseTargetPrefix(entry.CIDR)
		if !ok || entry.Port < 0 || entry.Port > 0xffff {
			t.InvalidEntries++
			continue
		}
		ports := entryPorts(spec.PortMode, uint16(entry.Port), defaultPorts)
		hostBits := prefix.Addr().BitLen() - prefix.Bits()
		entryLimit := limit
		if entryLimit == 0 && hostBits > maxEnumeratedBits {
			entryLimit = defaultSampleSize
		}
		hosts := uint64(0)
		sampled := false
		if hostBits < 64 {
			hosts = uint64(1) << hostBits
		}
		if entryLimit > 0 && (hostBits >= 64 || entryLimit < hosts) {
			hosts = entryLimit
			sampled = true
		}
		count := saturatingMul(hosts, uint64(len(ports)))
		if remaining := maxTargetTotal - t.total; count > remaining {
			count = remaining
		}
		if count == 0 {
			continue
		}
		r := targetRange{
			base:     prefix.Addr(),
			hostBits: hostBits,
			sampled:  sampled,
			ports:    ports,
			start:    t.total,
		}
		if sampled {
			t.SampledEntries++
			key := spec.Seed ^ prefixHash(prefix)
			r.sampler = newMixer(min(hostBits, 64), key)
			r.highKey = splitmix64(&key)
		}
		t.ranges = append(t.ranges, r)
		t.total += count
	}
	if t.total > 1 {
		t.order = newMixer(bits.Len64(t.total-1), spec.Seed)
	}
	return t
}

func (t *Targets) Total() uint64 {
	return t.total
}

// Distinct positions below Total give distinct targets.
func (t *Targets) At(position uint64) Target {
	global := position
	if t.shuffle {
		global = t.permute(position)
	}
	index := sort.Search(len(t.ranges), func(i int) bool { return t.ranges[i].start > global }) - 1
	r := &t.ranges[index]
	local := global - r.start
	portCount := uint64(len(r.ports))
	hostIndex, portIndex := local/portCount, local%portCount
	return Target{Addr: r.address(hostIndex), Port: r.ports[portIndex]}
}

func (t *Targets) permute(position uint64) uint64 {
	if t.total <= 1 {
		return 0
	}
	// Cycle walking: the mixer permutes the enclosing power of two, so stepping until the value falls in range stays a bijection.
	y := t.order.mix(position)
	for y >= t.total {
		y = t.order.mix(y)
	}
	return y
}

func (r *targetRange) address(hostIndex uint64) netip.Addr {
	high, low := uint64(0), hostIndex
	if r.sampled {
		low = r.sampler.mix(hostIndex)
		if r.hostBits > 64 {
			state := hostIndex ^ r.highKey
			high = splitmix64(&state) & bitMask(r.hostBits-64)
		}
	}
	if r.base.Is4() {
		b := r.base.As4()
		value := binary.BigEndian.Uint32(b[:]) + uint32(low)
		binary.BigEndian.PutUint32(b[:], value)
		return netip.AddrFrom4(b)
	}
	b := r.base.As16()
	baseHigh := binary.BigEndian.Uint64(b[:8])
	baseLow := binary.BigEndian.Uint64(b[8:])
	sumLow, carry := bits.Add64(baseLow, low, 0)
	sumHigh, _ := bits.Add64(baseHigh, high, carry)
	binary.BigEndian.PutUint64(b[:8], sumHigh)
	binary.BigEndian.PutUint64(b[8:], sumLow)
	return netip.AddrFrom16(b)
}

func parseTargetPrefix(text string) (netip.Prefix, bool) {
	text = strings.TrimSpace(text)
	prefix, err := netip.ParsePrefix(text)
	if err != nil {
		addr, addrErr := netip.ParseAddr(text)
		if addrErr != nil {
			return netip.Prefix{}, false
		}
		addr = addr.WithZone("")
		prefix = netip.PrefixFrom(addr, addr.BitLen())
	}
	if prefix.Addr().Is4In6() && prefix.Bits() >= 96 {
		prefix = netip.PrefixFrom(prefix.Addr().Unmap(), prefix.Bits()-96)
	}
	return prefix.Masked(), true
}

// Never empty: port 0 stands for "no port".
func entryPorts(mode string, entryPort uint16, defaultPorts []uint16) []uint16 {
	var ports []uint16
	switch mode {
	case PortModeIgnore:
		ports = defaultPorts
	case PortModeList:
		if entryPort != 0 {
			ports = []uint16{entryPort}
		}
	case PortModeMerge:
		if entryPort == 0 {
			ports = defaultPorts
			break
		}
		ports = []uint16{entryPort}
		for _, port := range defaultPorts {
			if port != entryPort {
				ports = append(ports, port)
			}
		}
	default:
		ports = defaultPorts
		if entryPort != 0 {
			ports = []uint16{entryPort}
		}
	}
	if len(ports) == 0 {
		return []uint16{0}
	}
	return ports
}

func normalizePorts(ports []int32) []uint16 {
	var out []uint16
	seen := make(map[int32]struct{}, len(ports))
	for _, port := range ports {
		if port < 1 || port > 0xffff {
			continue
		}
		if _, dup := seen[port]; dup {
			continue
		}
		seen[port] = struct{}{}
		out = append(out, uint16(port))
	}
	return out
}

func saturatingMul(a, b uint64) uint64 {
	high, low := bits.Mul64(a, b)
	if high != 0 || low > maxTargetTotal {
		return maxTargetTotal
	}
	return low
}

func bitMask(width int) uint64 {
	if width >= 64 {
		return ^uint64(0)
	}
	return uint64(1)<<width - 1
}

func splitmix64(state *uint64) uint64 {
	*state += 0x9e3779b97f4a7c15
	z := *state
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb
	return z ^ (z >> 31)
}

func prefixHash(prefix netip.Prefix) uint64 {
	b := prefix.Addr().As16()
	state := binary.BigEndian.Uint64(b[:8])
	h := splitmix64(&state)
	state = h ^ binary.BigEndian.Uint64(b[8:])
	h = splitmix64(&state)
	state = h ^ uint64(prefix.Bits()) ^ uint64(prefix.Addr().BitLen())<<8
	return splitmix64(&state)
}

// A keyed permutation of [0, 2^width): every step (add, xorshift-right, odd multiply) is invertible modulo 2^width.
type mixer struct {
	mask  uint64
	shift uint
	add   [mixerRounds]uint64
	mul   [mixerRounds]uint64
}

func newMixer(width int, key uint64) mixer {
	m := mixer{mask: bitMask(width), shift: uint(max(1, width/2))}
	state := key
	for round := range mixerRounds {
		m.add[round] = splitmix64(&state) & m.mask
		m.mul[round] = (splitmix64(&state) | 1) & m.mask
	}
	return m
}

func (m *mixer) mix(x uint64) uint64 {
	for round := range mixerRounds {
		x = (x + m.add[round]) & m.mask
		x ^= x >> m.shift
		x = (x * m.mul[round]) & m.mask
	}
	return x
}
