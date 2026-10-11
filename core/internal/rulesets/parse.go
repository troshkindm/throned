package rulesets

import (
	"bytes"
	"errors"
	"net/netip"

	"github.com/sagernet/sing-box/common/srs"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/json"

	"go4.org/netipx"
)

var ErrNoIPRanges = errors.New("the rule-set contains no IP ranges")

// Inverted rules and AND rules are skipped and counted: they do not describe a set of addresses.
func ParseIPCIDRs(content []byte) (cidrs []string, skipped int, version uint8, err error) {
	var compat option.PlainRuleSetCompat
	if bytes.HasPrefix(content, srs.MagicBytes[:]) {
		compat, err = srs.Read(bytes.NewReader(content), false)
	} else {
		compat, err = json.UnmarshalExtended[option.PlainRuleSetCompat](content)
	}
	if err != nil {
		return nil, 0, 0, err
	}
	version = compat.Version
	plain, err := compat.Upgrade()
	if err != nil {
		return nil, 0, version, err
	}
	var builder netipx.IPSetBuilder
	for _, rule := range plain.Rules {
		ruleSkipped, ruleErr := collectRule(&builder, rule)
		if ruleErr != nil {
			return nil, 0, version, ruleErr
		}
		skipped += ruleSkipped
	}
	set, err := builder.IPSet()
	if err != nil {
		return nil, skipped, version, err
	}
	for _, prefix := range set.Prefixes() {
		cidrs = append(cidrs, formatPrefix(prefix))
	}
	if len(cidrs) == 0 {
		return nil, skipped, version, ErrNoIPRanges
	}
	return cidrs, skipped, version, nil
}

func collectRule(builder *netipx.IPSetBuilder, rule option.HeadlessRule) (int, error) {
	switch rule.Type {
	case C.RuleTypeLogical:
		logical := rule.LogicalOptions
		if logical.Invert || logical.Mode == C.LogicalTypeAnd {
			return 1, nil
		}
		skipped := 0
		for _, child := range logical.Rules {
			childSkipped, err := collectRule(builder, child)
			if err != nil {
				return 0, err
			}
			skipped += childSkipped
		}
		return skipped, nil
	default:
		options := rule.DefaultOptions
		if options.Invert {
			return 1, nil
		}
		// Binary rule-sets carry a range set; JSON sources carry the prefixes.
		if options.IPSet != nil {
			builder.AddSet(options.IPSet.IPSet())
			return 0, nil
		}
		for _, prefix := range options.IPCIDR {
			builder.AddPrefix(unmapPrefix(prefix.Build(netip.Prefix{})).Masked())
		}
		return 0, nil
	}
}

func unmapPrefix(prefix netip.Prefix) netip.Prefix {
	if prefix.Addr().Is4In6() && prefix.Bits() >= 96 {
		return netip.PrefixFrom(prefix.Addr().Unmap(), prefix.Bits()-96)
	}
	return prefix
}

func formatPrefix(prefix netip.Prefix) string {
	if prefix.IsSingleIP() {
		return prefix.Addr().String()
	}
	return prefix.Masked().String()
}
