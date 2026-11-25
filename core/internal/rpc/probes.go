package rpc

import (
	"context"
	"time"

	"ThroneCore/gen"
	"ThroneCore/internal/killswitch"
	"ThroneCore/internal/probe"

	boxLog "github.com/sagernet/sing-box/log"
	E "github.com/sagernet/sing/common/exceptions"
)

func (s *server) UDPTest(ctx context.Context, in *gen.UDPTestRequest) (*gen.UDPTestResp, error) {
	env, err := prepareTestEnv(in.GetTestCurrent(), in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy())
	if err != nil {
		return nil, err
	}
	defer env.Close()

	tags := env.Tags
	if in.GetViaDirect() {
		if _, exists := env.Box.Outbound().Outbound("direct"); !exists {
			return nil, E.New("this profile has no direct outbound to measure")
		}
		tags = []string{"direct"}
	}

	timeout := time.Duration(in.GetTestTimeoutMs()) * time.Millisecond
	testCtx := udpTestLogContext(probe.TestContext(), in.GetQuiet())
	results := probe.BatchUDPTest(testCtx, env.Box, tags,
		in.GetTarget(), int(in.GetProbeCount()), int(in.GetMaxConcurrency()), timeout)

	res := make([]*gen.UDPTestRes, 0, len(results))
	for _, data := range results {
		res = append(res, udpResultToProto(data))
	}
	return &gen.UDPTestResp{Results: res}, nil
}

func udpTestLogContext(ctx context.Context, quiet bool) context.Context {
	if !quiet {
		return ctx
	}
	// The UI monitor emits its own state transitions, so its frequent dials stay at debug level.
	return boxLog.ContextWithOverrideLevel(ctx, boxLog.LevelDebug)
}

func (s *server) QueryUDPTest(context.Context, *gen.EmptyReq) (*gen.QueryUDPTestResponse, error) {
	out := &gen.QueryUDPTestResponse{}
	for _, result := range probe.UDPReporter.Results() {
		out.Results = append(out.Results, udpResultToProto(result))
	}
	return out, nil
}

func (s *server) SiteTest(ctx context.Context, in *gen.SiteTestRequest) (*gen.SiteTestResp, error) {
	targets := make([]probe.SiteTarget, 0, len(in.Targets))
	for _, target := range in.Targets {
		if target.GetUrl() != "" {
			targets = append(targets, probe.SiteTarget{Name: target.GetName(), URL: target.GetUrl()})
		}
	}
	if len(targets) == 0 {
		return nil, E.New("no sites to check")
	}

	env, err := prepareTestEnv(false, in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy())
	if err != nil {
		return nil, err
	}
	defer env.Close()

	timeout := time.Duration(in.GetTestTimeoutMs()) * time.Millisecond
	results := probe.BatchSiteTest(probe.TestContext(), env.Box, env.Tags, targets,
		int(in.GetMaxConcurrency()), timeout)
	res := make([]*gen.SiteTestRes, 0, len(results))
	for _, result := range results {
		res = append(res, siteResultToProto(result))
	}
	return &gen.SiteTestResp{Results: res}, nil
}

func (s *server) QuerySiteTest(context.Context, *gen.EmptyReq) (*gen.QuerySiteTestResponse, error) {
	out := &gen.QuerySiteTestResponse{}
	for _, result := range probe.SiteReporter.Results() {
		out.Results = append(out.Results, siteResultToProto(result))
	}
	return out, nil
}

func siteResultToProto(result *probe.SiteTestResult) *gen.SiteTestRes {
	errText := ""
	if result.Error != nil {
		errText = result.Error.Error()
	}
	probes := make([]*gen.SiteProbeRes, 0, len(result.Probes))
	for _, item := range result.Probes {
		probeError := ""
		if item.Error != nil {
			probeError = item.Error.Error()
		}
		probes = append(probes, &gen.SiteProbeRes{
			Name:      To(item.Name),
			Status:    To(int32(item.Status)),
			LatencyMs: To(int32(item.Duration.Milliseconds())),
			Error:     To(probeError),
		})
	}
	return &gen.SiteTestRes{OutboundTag: To(result.Tag), Probes: probes, Error: To(errText)}
}

func udpResultToProto(result *probe.UDPTestResult) *gen.UDPTestRes {
	errText := ""
	if result.Error != nil {
		errText = result.Error.Error()
	}
	ms := func(duration time.Duration) int32 { return int32(duration.Milliseconds()) }
	return &gen.UDPTestRes{
		OutboundTag: To(result.Tag),
		Sent:        To(int32(result.Sent)),
		Received:    To(int32(result.Received)),
		MinMs:       To(ms(result.Min)),
		AvgMs:       To(ms(result.Avg)),
		JitterMs:    To(ms(result.Jitter)),
		Error:       To(errText),
	}
}

func (s *server) SetTransitionGuard(_ context.Context, in *gen.SetTransitionGuardRequest) (*gen.ErrorResp, error) {
	if in.GetEnabled() {
		if err := killswitch.Enable(); err != nil {
			return &gen.ErrorResp{Error: To(err.Error())}, nil
		}
	} else {
		killswitch.Disable()
	}
	return &gen.ErrorResp{}, nil
}
