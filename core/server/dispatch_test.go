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
