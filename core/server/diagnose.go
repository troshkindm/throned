package main

import (
	"ThroneCore/gen"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	M "github.com/sagernet/sing/common/metadata"
)

// A URL test collapses every failure into one deadline, which says nothing about
// which hop broke. Diagnose walks the same path one stage at a time so the
// failing stage names itself: a dead name server no longer looks like a dead
// server. Steps reuse URLTestResp - the label rides in OutboundTag, so this
// needs no new message on either side of the IPC.
func (s *server) Diagnose(ctx context.Context, in *gen.TestReq) (*gen.TestResp, error) {
	const stageTimeout = 10 * time.Second

	var steps []*gen.URLTestResp
	step := func(label string, started time.Time, err error) bool {
		text := ""
		if err != nil {
			text = err.Error()
		}
		steps = append(steps, &gen.URLTestResp{
			OutboundTag: To(label),
			LatencyMs:   To(int32(time.Since(started).Milliseconds())),
			Error:       To(text),
		})
		return err == nil
	}

	tag := "proxy"
	if len(in.OutboundTags) > 0 {
		tag = in.OutboundTags[0]
	}

	endpoints := outboundEndpoints(in.GetConfig(), tag)
	if len(endpoints) == 0 {
		return &gen.TestResp{Results: []*gen.URLTestResp{{
			OutboundTag: To("config"),
			LatencyMs:   To(int32(0)),
			Error:       To("no outbound named " + tag + " carries a server address"),
		}}}, nil
	}

	// Stages 1 and 2: try every configured remote and every resolved address.
	// A dead first OpenVPN remote or unreachable IPv6 address must not hide a
	// working fallback that the core itself would use.
	reachable := false
	reachCtx, cancelReach := context.WithTimeout(ctx, stageTimeout)
	defer cancelReach()
	const attemptTimeout = 3 * time.Second
	for _, candidate := range endpoints {
		if reachCtx.Err() != nil {
			break
		}
		addresses := []string{candidate.host}
		if net.ParseIP(candidate.host) == nil {
			started := time.Now()
			resolveCtx, cancel := context.WithTimeout(reachCtx, attemptTimeout)
			ips, err := net.DefaultResolver.LookupHost(resolveCtx, candidate.host)
			cancel()
			if err == nil && len(ips) == 0 {
				err = fmt.Errorf("resolver returned no addresses")
			}
			label := "resolve " + candidate.host
			if err == nil {
				label += " → " + strings.Join(ips, ", ")
				addresses = ips
			}
			step(label, started, err)
			if err != nil {
				continue
			}
		}

		for _, address := range addresses {
			if reachCtx.Err() != nil {
				break
			}
			endpoint := net.JoinHostPort(address, strconv.Itoa(candidate.port))
			started := time.Now()
			dialCtx, cancel := context.WithTimeout(reachCtx, attemptTimeout)
			conn, err := (&net.Dialer{}).DialContext(dialCtx, candidate.network, endpoint)
			cancel()
			if conn != nil {
				conn.Close()
			}
			step(candidate.network+" "+endpoint, started, err)
			if err == nil {
				reachable = true
				break
			}
		}
		if reachable {
			break
		}
	}
	if !reachable {
		return &gen.TestResp{Results: steps}, nil
	}

	// Stage 3 and 4 need the outbound itself, which is what carries tls, the
	// transport and the proxy handshake.
	env, err := prepareTestEnv(in.GetTestCurrent(), in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy())
	if err != nil {
		step("start core", time.Now(), err)
		return &gen.TestResp{Results: steps}, nil
	}
	defer env.close()

	outbound, exists := env.box.Outbound().Outbound(tag)
	if !exists {
		step("start core", time.Now(), fmt.Errorf("no outbound with tag %s", tag))
		return &gen.TestResp{Results: steps}, nil
	}

	target := in.GetUrl()
	parsed, err := url.Parse(target)
	if err != nil {
		step("target url", time.Now(), err)
		return &gen.TestResp{Results: steps}, nil
	}
	targetPort := parsed.Port()
	if targetPort == "" {
		targetPort = map[string]string{"http": "80", "https": "443"}[parsed.Scheme]
	}
	targetAddr := net.JoinHostPort(parsed.Hostname(), targetPort)

	started := time.Now()
	dialCtx, cancelDial := context.WithTimeout(ctx, stageTimeout)
	proxyConn, err := outbound.DialContext(dialCtx, "tcp", M.ParseSocksaddr(targetAddr))
	cancelDial()
	if !step("proxy handshake to "+targetAddr, started, err) {
		return &gen.TestResp{Results: steps}, nil
	}

	// Reuse the connection the handshake stage opened: dialing again would
	// measure a second handshake instead of the request.
	started = time.Now()
	client := &http.Client{
		Timeout: stageTimeout,
		Transport: &http.Transport{
			DialContext: func(context.Context, string, string) (net.Conn, error) { return proxyConn, nil },
		},
	}
	resp, err := client.Get(target)
	label := "http " + parsed.Hostname()
	if err == nil {
		label += " → " + resp.Status
		io.Copy(io.Discard, io.LimitReader(resp.Body, 4096))
		resp.Body.Close()
	}
	step(label, started, err)

	return &gen.TestResp{Results: steps}, nil
}

type configServer struct {
	Tag         string          `json:"tag"`
	Type        string          `json:"type"`
	Server      string          `json:"server"`
	ServerPort  int             `json:"server_port"`
	Network     string          `json:"network"`
	Transport   string          `json:"transport"`
	ServerPorts json.RawMessage `json:"server_ports"`
	Servers     []configServer  `json:"servers"`
}

type diagnoseEndpoint struct {
	host    string
	port    int
	network string
}

func defaultServerPort(outboundType string) int {
	switch outboundType {
	case "openconnect":
		return 443
	case "openvpn", "openvpn-client":
		return 1194
	default:
		return 0
	}
}

func defaultServerNetwork(outboundType string) string {
	switch outboundType {
	case "hysteria", "hysteria2", "openvpn", "openvpn-client", "tuic":
		return "udp"
	}
	return "tcp"
}

func firstServerPort(raw json.RawMessage) int {
	if len(raw) == 0 {
		return 0
	}
	var values []string
	if raw[0] == '[' {
		if json.Unmarshal(raw, &values) != nil {
			return 0
		}
	} else {
		var value string
		if json.Unmarshal(raw, &value) != nil {
			return 0
		}
		values = strings.Split(value, ",")
	}
	for _, value := range values {
		parts := strings.FieldsFunc(value, func(separator rune) bool { return separator == '-' || separator == ':' })
		if len(parts) == 0 {
			continue
		}
		first := strings.TrimSpace(parts[0])
		if port, err := strconv.Atoi(first); err == nil && port > 0 && port <= 65535 {
			return port
		}
	}
	return 0
}

func serverEndpoints(server configServer, inheritedType string, inheritedNetwork string) []diagnoseEndpoint {
	var result []diagnoseEndpoint
	outboundType := server.Type
	if outboundType == "" {
		outboundType = inheritedType
	}
	network := strings.ToLower(server.Network)
	if network == "" {
		network = strings.ToLower(server.Transport)
	}
	if network == "" {
		network = inheritedNetwork
	}
	if network != "udp" && network != "tcp" {
		network = defaultServerNetwork(outboundType)
	}

	if server.Server != "" {
		host := server.Server
		port := server.ServerPort
		if port == 0 {
			port = firstServerPort(server.ServerPorts)
		}
		// OpenConnect stores one URL-like value, commonly without a scheme and
		// sometimes with a path. url.Parse only recognises its host after //.
		if outboundType == "openconnect" || strings.Contains(host, "://") {
			parseable := host
			if !strings.Contains(parseable, "://") {
				parseable = "//" + parseable
			}
			if parsed, err := url.Parse(parseable); err == nil && parsed.Hostname() != "" {
				host = parsed.Hostname()
				if parsed.Port() != "" {
					if parsedPort, err := strconv.Atoi(parsed.Port()); err == nil {
						port = parsedPort
					}
				}
			}
		} else if splitHost, splitPort, err := net.SplitHostPort(host); err == nil {
			host = splitHost
			if port == 0 {
				port, _ = strconv.Atoi(splitPort)
			}
		}
		host = strings.Trim(host, "[]")
		if port == 0 {
			port = defaultServerPort(outboundType)
		}
		if host != "" && port > 0 {
			result = append(result, diagnoseEndpoint{host: host, port: port, network: network})
		}
	}

	for _, remote := range server.Servers {
		result = append(result, serverEndpoints(remote, outboundType, network)...)
	}
	return result
}

func serverEndpoint(server configServer, inheritedType string, inheritedNetwork string) (string, int, string) {
	endpoints := serverEndpoints(server, inheritedType, inheritedNetwork)
	if len(endpoints) == 0 {
		return "", 0, ""
	}
	return endpoints[0].host, endpoints[0].port, endpoints[0].network
}

func serverAddress(server configServer, inheritedType string) (string, int) {
	host, port, _ := serverEndpoint(server, inheritedType, "")
	return host, port
}

// outboundServer digs the server address out of the generated config so the
// early stages can be run without standing a core up first. OpenVPN and
// OpenConnect live under endpoints rather than ordinary outbounds.
func outboundServer(config string, tag string) (string, int) {
	host, port, _ := outboundEndpoint(config, tag)
	return host, port
}

func outboundEndpoint(config string, tag string) (string, int, string) {
	endpoints := outboundEndpoints(config, tag)
	if len(endpoints) == 0 {
		return "", 0, ""
	}
	return endpoints[0].host, endpoints[0].port, endpoints[0].network
}

func outboundEndpoints(config string, tag string) []diagnoseEndpoint {
	var parsed struct {
		Outbounds []configServer `json:"outbounds"`
		Endpoints []configServer `json:"endpoints"`
	}
	if json.Unmarshal([]byte(config), &parsed) != nil {
		return nil
	}
	servers := append(parsed.Outbounds, parsed.Endpoints...)
	for _, outbound := range servers {
		if outbound.Tag == tag {
			if endpoints := serverEndpoints(outbound, "", ""); len(endpoints) > 0 {
				return endpoints
			}
		}
	}
	// A chain hands its tag to the outermost hop, so fall back to whichever
	// outbound or endpoint carries an address at all.
	for _, outbound := range servers {
		if endpoints := serverEndpoints(outbound, "", ""); len(endpoints) > 0 {
			return endpoints
		}
	}
	return nil
}
