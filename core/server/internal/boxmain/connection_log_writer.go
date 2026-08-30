package boxmain

import (
	"context"
	"fmt"
	"io"
	"net/netip"
	"path/filepath"
	"regexp"
	"strings"
	"sync"

	"github.com/sagernet/sing-box/common/trafficcontrol"
	"github.com/sagernet/sing/service"
)

type trackedProcess struct {
	source  string
	name    string
	path    string
	pid     uint32
	matched bool
}

type processResolver func(endpoints []string) trackedProcess

type connectionLogWriter struct {
	target  io.Writer
	resolve processResolver

	mu          sync.Mutex
	contexts    map[string]connectionContext
	contextRing [contextCacheSize]string
	contextNext int
}

type connectionContext struct {
	source      string
	processPath string
}

const contextCacheSize = 2048

var connectionEndpointsPattern = regexp.MustCompile(
	`(?i)\b(?:raw-read|raw-write) tcp(?:4|6)?\s+(\[[^\]]+\]:\d+|[^\s:]+:\d+)\s*->\s*(\[[^\]]+\]:\d+|[^\s:]+:\d+)`,
)

var connectionContextPattern = regexp.MustCompile(`\[(\d+)\s+[^\]]+\]`)

var inboundSourcePattern = regexp.MustCompile(
	`(?i)\binbound/[^:]+:\s+inbound (?:packet )?connection from\s+(\[[^\]]+\]:\d+|[^\s:]+:\d+)`,
)

func newConnectionLogWriter(ctx context.Context, target io.Writer) io.Writer {
	return &connectionLogWriter{
		target: target,
		resolve: func(endpoints []string) trackedProcess {
			return resolveTrackedProcess(ctx, endpoints)
		},
	}
}

func (w *connectionLogWriter) Write(data []byte) (int, error) {
	w.mu.Lock()
	hint := w.observeConnectionContext(string(data))
	output := enrichConnectionLogWithContext(string(data), w.resolve, hint)
	written, err := io.WriteString(w.target, output)
	w.mu.Unlock()
	if err != nil {
		return 0, err
	}
	if written != len(output) {
		return 0, io.ErrShortWrite
	}
	// io.Writer reports consumption of the input, not the enriched output.
	return len(data), nil
}

func enrichConnectionLog(input string, resolve processResolver) string {
	return enrichConnectionLogWithContext(input, resolve, connectionContext{})
}

func enrichConnectionLogWithContext(input string, resolve processResolver, hint connectionContext) string {
	lineEnding := ""
	line := input
	if strings.HasSuffix(line, "\n") {
		lineEnding = "\n"
		line = strings.TrimSuffix(line, "\n")
		if strings.HasSuffix(line, "\r") {
			lineEnding = "\r\n"
			line = strings.TrimSuffix(line, "\r")
		}
	}

	lower := strings.ToLower(line)
	if !strings.Contains(lower, "connection upload closed:") &&
		!strings.Contains(lower, "connection download closed:") {
		return input
	}

	match := connectionEndpointsPattern.FindStringSubmatch(line)
	if len(match) != 3 {
		return input
	}
	// The peer is normally the second endpoint. Trying it first also keeps a
	// loopback listener (127.0.0.1:2080) from winning over the app's ephemeral port.
	process := resolve([]string{match[2], match[1]})
	if !process.matched && hint.source != "" {
		process = resolve([]string{hint.source})
	}
	if !process.matched && hint.processPath != "" {
		process = trackedProcess{
			source:  hint.source,
			path:    hint.processPath,
			matched: true,
		}
	}
	if !process.matched {
		return input
	}

	annotation := processAnnotation(process)
	if isLoopbackEndpoint(process.source) && isConnectionReset(lower) {
		line = demoteErrorLevel(line)
		messageAt := strings.Index(strings.ToLower(line), "connection upload closed:")
		if messageAt < 0 {
			messageAt = strings.Index(strings.ToLower(line), "connection download closed:")
		}
		peer := match[1]
		if process.source == match[1] {
			peer = match[2]
		}
		line = line[:messageAt] + "local client closed connection: " + process.source + " -> " + peer + annotation
		return line + lineEnding
	}

	return line + annotation + lineEnding
}

func (w *connectionLogWriter) observeConnectionContext(line string) connectionContext {
	match := connectionContextPattern.FindStringSubmatch(line)
	if len(match) != 2 {
		return connectionContext{}
	}
	id := match[1]
	if w.contexts == nil {
		w.contexts = make(map[string]connectionContext, contextCacheSize)
	}
	hint, exists := w.contexts[id]
	if !exists {
		if evicted := w.contextRing[w.contextNext]; evicted != "" {
			delete(w.contexts, evicted)
		}
		w.contextRing[w.contextNext] = id
		w.contextNext = (w.contextNext + 1) % contextCacheSize
	}

	if source := inboundSourcePattern.FindStringSubmatch(line); len(source) == 2 {
		hint.source = source[1]
	}
	if at := strings.Index(line, "router: found process path:"); at >= 0 {
		hint.processPath = strings.TrimSpace(line[at+len("router: found process path:"):])
	}
	w.contexts[id] = hint
	return hint
}

func demoteErrorLevel(line string) string {
	if strings.HasPrefix(line, "ERROR") {
		return "INFO" + strings.TrimPrefix(line, "ERROR")
	}
	if at := strings.Index(line, " ERROR "); at >= 0 {
		return line[:at] + " INFO " + line[at+len(" ERROR "):]
	}
	return line
}

func processAnnotation(process trackedProcess) string {
	name := process.name
	if name == "" && process.path != "" {
		name = filepath.Base(process.path)
	}
	switch {
	case name != "" && process.pid != 0:
		return fmt.Sprintf(" [process: %s, PID %d]", name, process.pid)
	case name != "":
		return fmt.Sprintf(" [process: %s]", name)
	case process.pid != 0:
		return fmt.Sprintf(" [PID %d]", process.pid)
	default:
		return " [process unavailable]"
	}
}

func isConnectionReset(lowerLine string) bool {
	return strings.Contains(lowerLine, "forcibly closed by the remote host") ||
		strings.Contains(lowerLine, "connection reset by peer") ||
		strings.Contains(lowerLine, "wsarecv") ||
		strings.Contains(lowerLine, "wsasend") ||
		strings.Contains(lowerLine, "10054")
}

func isLoopbackEndpoint(endpoint string) bool {
	address, err := netip.ParseAddrPort(endpoint)
	return err == nil && address.Addr().IsLoopback()
}

func resolveTrackedProcess(ctx context.Context, endpoints []string) trackedProcess {
	manager := service.PtrFromContext[trafficcontrol.Manager](ctx)
	if manager == nil {
		return trackedProcess{}
	}

	connections := append(manager.Connections(), manager.ClosedConnections()...)
	for _, endpoint := range endpoints {
		var newest trackedProcess
		var newestAt int64
		for _, connection := range connections {
			if connection.Metadata.Source.String() != endpoint {
				continue
			}
			createdAt := connection.CreatedAt.UnixNano()
			if newest.matched && createdAt < newestAt {
				continue
			}
			newest = trackedProcess{source: endpoint, matched: true}
			newestAt = createdAt
			if connection.Metadata.ProcessInfo != nil {
				newest.path = connection.Metadata.ProcessInfo.ProcessPath
				if newest.path != "" {
					newest.name = filepath.Base(newest.path)
				}
				newest.pid = connection.Metadata.ProcessInfo.ProcessID
			}
		}
		if newest.matched {
			return newest
		}
	}
	return trackedProcess{}
}
