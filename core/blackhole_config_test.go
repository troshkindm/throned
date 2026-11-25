package main

import (
	"testing"

	"ThroneCore/internal/boxmain"
)

// The config Configs::BuildBlackholeConfig() emits. It runs in place of a
// stopped profile when the kill switch is on, so sing-box has to accept it on
// its own - there is no profile to fall back to if it does not.
func TestBlackholeConfig(t *testing.T) {
	config := []byte(`{
		"log": {"level": "info", "timestamp": true},
		"inbounds": [{
			"tag": "tun-in",
			"type": "tun",
			"interface_name": "throned-tun",
			"auto_route": true,
			"mtu": 9000,
			"strict_route": true,
			"address": ["172.19.0.1/30"],
			"route_exclude_address": [
				"127.0.0.0/8", "255.255.255.255/32",
				"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16",
				"169.254.0.0/16", "224.0.0.0/4"
			]
		}],
		"outbounds": [{"type": "direct", "tag": "direct"}],
		"route": {
			"rules": [{"inbound": ["tun-in"], "action": "reject"}],
			"final": "direct",
			"auto_detect_interface": true
		}
	}`)

	if err := boxmain.Check(config); err != nil {
		t.Fatalf("blackhole config rejected by sing-box: %v", err)
	}
}
