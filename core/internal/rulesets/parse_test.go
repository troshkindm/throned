package rulesets

import (
	"bytes"
	"errors"
	"net/netip"
	"slices"
	"testing"

	"github.com/sagernet/sing-box/common/srs"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common"
	"github.com/sagernet/sing/common/json/badoption"
)

func prefixes(cidrs ...string) badoption.Listable[*badoption.Prefixable] {
	return common.Map(cidrs, func(it string) *badoption.Prefixable {
		return common.Ptr(badoption.Prefixable(netip.MustParsePrefix(it)))
	})
}

func defaultRule(rule option.DefaultHeadlessRule) option.HeadlessRule {
	return option.HeadlessRule{Type: C.RuleTypeDefault, DefaultOptions: rule}
}

func logicalRule(mode string, invert bool, rules ...option.HeadlessRule) option.HeadlessRule {
	return option.HeadlessRule{Type: C.RuleTypeLogical, LogicalOptions: option.LogicalHeadlessRule{Mode: mode, Invert: invert, Rules: rules}}
}

func sampleRules() option.PlainRuleSet {
	return option.PlainRuleSet{Rules: []option.HeadlessRule{
		defaultRule(option.DefaultHeadlessRule{IPCIDR: prefixes("10.0.0.0/24", "10.0.1.0/24", "1.1.1.1/32", "2001:db8::/32")}),
		defaultRule(option.DefaultHeadlessRule{IPCIDR: prefixes("10.0.0.128/25"), SourceIPCIDR: prefixes("172.16.0.0/12")}),
		defaultRule(option.DefaultHeadlessRule{IPCIDR: prefixes("8.8.8.0/24"), Invert: true}),
		logicalRule(C.LogicalTypeOr, false,
			defaultRule(option.DefaultHeadlessRule{IPCIDR: prefixes("9.9.9.9/32")}),
			defaultRule(option.DefaultHeadlessRule{Domain: []string{"example.com"}}),
		),
		logicalRule(C.LogicalTypeAnd, false,
			defaultRule(option.DefaultHeadlessRule{IPCIDR: prefixes("4.4.4.0/24")}),
			defaultRule(option.DefaultHeadlessRule{Port: []uint16{443}}),
		),
	}}
}

var sampleWant = []string{"1.1.1.1", "9.9.9.9", "10.0.0.0/23", "2001:db8::/32"}

func TestParseBinary(t *testing.T) {
	var buffer bytes.Buffer
	if err := srs.Write(&buffer, sampleRules(), C.RuleSetVersion3); err != nil {
		t.Fatal(err)
	}
	cidrs, skipped, version, err := ParseIPCIDRs(buffer.Bytes())
	if err != nil {
		t.Fatal(err)
	}
	if !slices.Equal(cidrs, sampleWant) {
		t.Fatalf("cidrs %v, want %v", cidrs, sampleWant)
	}
	if skipped != 2 || version != C.RuleSetVersion3 {
		t.Fatalf("skipped %d version %d", skipped, version)
	}
}

func TestParseSource(t *testing.T) {
	source := []byte(`{
  "version": 2,
  "rules": [
    {"ip_cidr": ["10.0.0.0/24", "10.0.1.0/24", "1.1.1.1", "2001:db8::/32"]},
    {"ip_cidr": ["10.0.0.128/25"], "source_ip_cidr": ["172.16.0.0/12"]},
    {"ip_cidr": ["8.8.8.0/24"], "invert": true},
    {"type": "logical", "mode": "or", "rules": [{"ip_cidr": ["9.9.9.9/32"]}, {"domain": ["example.com"]}]},
    {"type": "logical", "mode": "and", "rules": [{"ip_cidr": ["4.4.4.0/24"]}, {"port": 443}]}
  ]
}`)
	cidrs, skipped, version, err := ParseIPCIDRs(source)
	if err != nil {
		t.Fatal(err)
	}
	if !slices.Equal(cidrs, sampleWant) {
		t.Fatalf("cidrs %v, want %v", cidrs, sampleWant)
	}
	if skipped != 2 || version != 2 {
		t.Fatalf("skipped %d version %d", skipped, version)
	}
}

func TestParseNoIPRanges(t *testing.T) {
	_, _, _, err := ParseIPCIDRs([]byte(`{"version": 3, "rules": [{"domain_suffix": ["example.com"]}]}`))
	if !errors.Is(err, ErrNoIPRanges) {
		t.Fatalf("err %v", err)
	}
	var buffer bytes.Buffer
	ruleSet := option.PlainRuleSet{Rules: []option.HeadlessRule{defaultRule(option.DefaultHeadlessRule{Domain: []string{"example.com"}})}}
	if err = srs.Write(&buffer, ruleSet, C.RuleSetVersion1); err != nil {
		t.Fatal(err)
	}
	if _, _, _, err = ParseIPCIDRs(buffer.Bytes()); !errors.Is(err, ErrNoIPRanges) {
		t.Fatalf("binary err %v", err)
	}
}

func TestParseRejectsGarbage(t *testing.T) {
	for _, content := range [][]byte{nil, []byte("<html></html>"), []byte("SRS\x09garbage"), []byte(`{"version": 1, "rules": [{"ip_cidr": ["nope"]}]}`)} {
		if _, _, _, err := ParseIPCIDRs(content); err == nil {
			t.Fatalf("%q parsed", content)
		}
	}
}
