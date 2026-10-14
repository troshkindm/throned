package amnezia

import (
	"context"
	"encoding/hex"
	"strings"
	"testing"
	"time"
)

func TestNormalizeServerName(t *testing.T) {
	for _, tc := range []struct{ input, want string }{
		{" example.org ", "example.org"},
		{"https://example.org/path", "example.org"},
		{"example.org:443", "example.org"},
		{"example.org.", "example.org"},
		{"пример.рф", "xn--e1afmkfd.xn--p1ai"},
	} {
		t.Run(tc.input, func(t *testing.T) {
			got, err := normalizeServerName(tc.input)
			if err != nil || got != tc.want {
				t.Fatalf("normalizeServerName(%q) = %q, %v; want %q", tc.input, got, err, tc.want)
			}
		})
	}
	for _, input := range []string{"", "192.0.2.1", "https://192.0.2.1/path", "[2001:db8::1]:443"} {
		if _, err := normalizeServerName(input); err == nil {
			t.Errorf("accepted domain without usable SNI: %q", input)
		}
	}
}

func TestSignatureProducesBoundedOfflineFlight(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	packets, err := QUICSignature(ctx, "https://example.org")
	if err != nil {
		t.Fatal(err)
	}
	if len(packets) == 0 || len(packets) > maxSignaturePackets {
		t.Fatalf("got %d signature slots", len(packets))
	}
	for _, packet := range packets {
		if !strings.HasPrefix(packet, "<b 0x") || !strings.HasSuffix(packet, ">") {
			t.Fatalf("invalid AmneziaWG packet encoding: %.20q", packet)
		}
		data, err := hex.DecodeString(strings.TrimSuffix(strings.TrimPrefix(packet, "<b 0x"), ">"))
		if err != nil || len(data) < 1200 {
			t.Fatalf("invalid QUIC initial packet: %d bytes, %v", len(data), err)
		}
	}
}
