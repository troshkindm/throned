package main

import (
	"ThroneCore/gen"
	"net/http"
	"net/http/httptest"
	"testing"

	"google.golang.org/protobuf/proto"
)

func TestDispatchCoversProtobufService(t *testing.T) {
	methods := gen.File_libcore_proto.Services().ByName("LibcoreService").Methods()
	for idx := 0; idx < methods.Len(); idx++ {
		name := string(methods.Get(idx).Name())
		if handlers[name] == nil {
			t.Errorf("RPC %s is declared but cannot be called over IPC", name)
		}
	}
}

func TestDispatchDiagnoseSiteUsesRunningOutbound(t *testing.T) {
	previous, previousCancel := currentInstance()
	defer setBoxInstance(previous, previousCancel)
	setBoxInstance(nil, nil)
	payload, _ := proto.Marshal(&gen.DiagnoseSiteRequest{Url: To("http://127.0.0.1")})
	if _, err := dispatch("DiagnoseSite", payload); err == nil {
		t.Fatal("a stopped instance must return an explicit error")
	}
	env, err := prepareTestEnv(false, false, "", nil,
		`{"outbounds":[{"type":"direct","tag":"diagnostic-direct"}]}`, nil, true, "")
	if err != nil {
		t.Fatal(err)
	}
	defer env.close()
	setBoxInstance(env.box, nil)
	site := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { w.WriteHeader(http.StatusForbidden) }))
	defer site.Close()
	for _, tag := range []string{"", "diagnostic-direct"} {
		payload, _ = proto.Marshal(&gen.DiagnoseSiteRequest{Url: To(site.URL), OutboundTag: To(tag)})
		response, err := dispatch("DiagnoseSite", payload)
		if err != nil {
			t.Fatal(err)
		}
		var result gen.DiagnoseSiteResponse
		if err := proto.Unmarshal(response, &result); err != nil {
			t.Fatal(err)
		}
		if result.GetStatus() != 403 || result.GetOutboundTag() != "diagnostic-direct" || result.GetError() != "" {
			t.Fatalf("unexpected response: %v", &result)
		}
	}
	payload, _ = proto.Marshal(&gen.DiagnoseSiteRequest{Url: To(site.URL), OutboundTag: To("removed-outbound")})
	if _, err := dispatch("DiagnoseSite", payload); err == nil {
		t.Fatal("must not fall back silently when an outbound disappears")
	}
}

func TestDispatchPreviewRouteReplaysTheRunningRules(t *testing.T) {
	previous, previousCancel := currentInstance()
	defer setBoxInstance(previous, previousCancel)
	setBoxInstance(nil, nil)
	payload, _ := proto.Marshal(&gen.PreviewRouteRequest{Url: To("https://preview.example")})
	if _, err := dispatch("PreviewRoute", payload); err == nil {
		t.Fatal("a stopped instance must return an explicit error")
	}
	env, err := prepareTestEnv(false, false, "", nil, `{
		"outbounds": [{"type":"direct","tag":"fallback"},{"type":"direct","tag":"rule-target"}],
		"route": {"rules":[{"domain_suffix":["preview.example"],"outbound":"rule-target"}],"final":"fallback"}
	}`, nil, true, "")
	if err != nil {
		t.Fatal(err)
	}
	defer env.close()
	setBoxInstance(env.box, nil)

	preview := func(url string) *gen.PreviewRouteResponse {
		t.Helper()
		payload, _ := proto.Marshal(&gen.PreviewRouteRequest{Url: To(url)})
		response, err := dispatch("PreviewRoute", payload)
		if err != nil {
			t.Fatal(err)
		}
		var result gen.PreviewRouteResponse
		if err := proto.Unmarshal(response, &result); err != nil {
			t.Fatal(err)
		}
		return &result
	}
	matched := preview("https://preview.example")
	if matched.GetOutboundTag() != "rule-target" || matched.GetAction() != "route" || matched.GetMatchedRule() == "" {
		t.Fatalf("the rule was not replayed: %v", matched)
	}
	// Nothing matches, so the answer has to be the default outbound and no rule.
	unmatched := preview("https://elsewhere.example")
	if unmatched.GetOutboundTag() != "fallback" || unmatched.GetMatchedRule() != "" || unmatched.GetAction() != "" {
		t.Fatalf("unmatched address did not fall through to the default: %v", unmatched)
	}
	if bad := preview("not a url"); bad.GetError() == "" {
		t.Fatalf("invalid input was accepted: %v", bad)
	}
}

func TestDispatchHealthNeedsARunningInstanceAndListsOutbounds(t *testing.T) {
	previous, previousCancel := currentInstance()
	defer setBoxInstance(previous, previousCancel)
	setBoxInstance(nil, nil)
	payload, _ := proto.Marshal(&gen.HealthRequest{})
	if _, err := dispatch("Health", payload); err == nil {
		t.Fatal("a stopped instance must return an explicit error")
	}
	env, err := prepareTestEnv(false, false, "", nil,
		`{"outbounds":[{"type":"direct","tag":"fallback"},{"type":"direct","tag":"second"}]}`, nil, true, "")
	if err != nil {
		t.Fatal(err)
	}
	defer env.close()
	setBoxInstance(env.box, nil)
	// A domain nothing can resolve keeps the test off the network while still
	// exercising the whole path; only the outbound inventory is asserted.
	payload, _ = proto.Marshal(&gen.HealthRequest{DnsProbeDomain: To("invalid.")})
	response, err := dispatch("Health", payload)
	if err != nil {
		t.Fatal(err)
	}
	var result gen.HealthResponse
	if err := proto.Unmarshal(response, &result); err != nil {
		t.Fatal(err)
	}
	if len(result.GetOutbounds()) < 2 || result.GetOutboundTag() == "" {
		t.Fatalf("outbound inventory missing: %v", &result)
	}
	if result.GetDnsCompared() {
		t.Fatalf("an unresolvable name must not be reported as a compared answer: %v", &result)
	}
}

func TestDispatchSiteTestReturnsHTTPResults(t *testing.T) {
	site := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/blocked" {
			w.WriteHeader(http.StatusForbidden)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	}))
	defer site.Close()

	request := &gen.SiteTestRequest{
		Config:        To(`{"outbounds":[{"type":"direct","tag":"test-direct"}]}`),
		OutboundTags:  []string{"test-direct"},
		TestTimeoutMs: To(int32(2000)),
		Targets: []*gen.SiteTarget{
			{Name: To("available"), Url: To(site.URL)},
			{Name: To("blocked"), Url: To(site.URL + "/blocked")},
		},
	}
	payload, err := proto.Marshal(request)
	if err != nil {
		t.Fatal(err)
	}
	response, err := dispatch("SiteTest", payload)
	if err != nil {
		t.Fatalf("SiteTest IPC failed: %v", err)
	}
	var result gen.SiteTestResp
	if err := proto.Unmarshal(response, &result); err != nil {
		t.Fatal(err)
	}
	if len(result.Results) != 1 {
		t.Fatalf("got %d profile results, want 1", len(result.Results))
	}
	row := result.Results[0]
	if row.GetError() != "" || row.GetOutboundTag() != "test-direct" || len(row.Probes) != 2 {
		t.Fatalf("unexpected profile result: %v", row)
	}
	for idx, want := range []int32{http.StatusNoContent, http.StatusForbidden} {
		if probe := row.Probes[idx]; probe.GetStatus() != want || probe.GetError() != "" {
			t.Errorf("probe %d: %v, want HTTP %d", idx, probe, want)
		}
	}
	if _, err := dispatch("QuerySiteTest", nil); err != nil {
		t.Fatalf("QuerySiteTest IPC failed: %v", err)
	}
}
