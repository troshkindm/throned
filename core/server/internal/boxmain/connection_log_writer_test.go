package boxmain

import (
	"bytes"
	"strings"
	"testing"
)

func TestConnectionLogWriterExplainsLocalReset(t *testing.T) {
	input := "ERROR[7899] [190071618 141ms] connection: connection upload closed: raw-read tcp 127.0.0.1:2080->127.0.0.1:44407: An existing connection was forcibly closed by the remote host.\n"
	resolver := func(endpoints []string) trackedProcess {
		if len(endpoints) != 2 || endpoints[0] != "127.0.0.1:44407" {
			t.Fatalf("unexpected endpoint order: %v", endpoints)
		}
		return trackedProcess{source: endpoints[0], name: "ExampleBrowser.exe", pid: 4242, matched: true}
	}

	got := enrichConnectionLog(input, resolver)
	want := "INFO[7899] [190071618 141ms] connection: local client closed connection: 127.0.0.1:44407 -> 127.0.0.1:2080 [process: ExampleBrowser.exe, PID 4242]\n"
	if got != want {
		t.Fatalf("unexpected log line:\n got: %q\nwant: %q", got, want)
	}
}

func TestConnectionLogWriterKeepsUnmatchedError(t *testing.T) {
	input := "ERROR[0042] connection: connection download closed: raw-read tcp 10.0.0.2:51300->203.0.113.7:443: connection reset by peer\n"
	got := enrichConnectionLog(input, func([]string) trackedProcess { return trackedProcess{} })
	if got != input {
		t.Fatalf("unmatched error was changed: %q", got)
	}
}

func TestConnectionLogWriterDemotesTimestampedReset(t *testing.T) {
	input := "2026-08-30 23:44:00 ERROR connection: connection download closed: raw-write tcp 127.0.0.1:2080->127.0.0.1:51112: wsarecv: 10054\n"
	got := enrichConnectionLog(input, func(endpoints []string) trackedProcess {
		return trackedProcess{source: endpoints[0], name: "Demo.exe", pid: 78, matched: true}
	})
	want := "2026-08-30 23:44:00 INFO connection: local client closed connection: 127.0.0.1:51112 -> 127.0.0.1:2080 [process: Demo.exe, PID 78]\n"
	if got != want {
		t.Fatalf("unexpected timestamped log line:\n got: %q\nwant: %q", got, want)
	}
}

func TestConnectionLogWriterAnnotatesNonResetError(t *testing.T) {
	input := "ERROR[0042] connection: connection upload closed: raw-write tcp 127.0.0.1:2080->127.0.0.1:51111: i/o timeout\r\n"
	got := enrichConnectionLog(input, func(endpoints []string) trackedProcess {
		return trackedProcess{source: endpoints[0], path: `C:\\Apps\\Demo.exe`, pid: 77, matched: true}
	})
	want := "ERROR[0042] connection: connection upload closed: raw-write tcp 127.0.0.1:2080->127.0.0.1:51111: i/o timeout [process: Demo.exe, PID 77]\r\n"
	if got != want {
		t.Fatalf("unexpected annotated error:\n got: %q\nwant: %q", got, want)
	}
}

func TestConnectionLogWriterHonoursWriterContract(t *testing.T) {
	var target bytes.Buffer
	writer := &connectionLogWriter{
		target: &target,
		resolve: func(endpoints []string) trackedProcess {
			return trackedProcess{source: endpoints[0], pid: 9, matched: true}
		},
	}
	input := []byte("ERROR[0001] connection: connection upload closed: raw-read tcp 127.0.0.1:2080->127.0.0.1:50000: 10054\n")
	n, err := writer.Write(input)
	if err != nil {
		t.Fatal(err)
	}
	if n != len(input) {
		t.Fatalf("reported %d bytes consumed, want %d", n, len(input))
	}
	if !bytes.Contains(target.Bytes(), []byte("[PID 9]")) {
		t.Fatalf("enriched output missing PID: %q", target.String())
	}
}

func TestConnectionLogWriterCorrelatesTunErrorByContextID(t *testing.T) {
	var target bytes.Buffer
	writer := &connectionLogWriter{
		target: &target,
		resolve: func(endpoints []string) trackedProcess {
			if len(endpoints) == 1 && endpoints[0] == "172.19.0.1:63094" {
				return trackedProcess{
					source: endpoints[0], name: "python.exe", pid: 31337, matched: true,
				}
			}
			return trackedProcess{}
		},
	}

	lines := []string{
		"INFO[0010] [1408276967 0ms] inbound/tun[tun-in]: inbound connection from 172.19.0.1:63094\n",
		`INFO[0010] [1408276967 0ms] router: found process path: C:\Users\demo\python.exe` + "\n",
		"ERROR[0010] [1408276967 1.15s] connection: connection upload closed: raw-read tcp4 172.19.0.1:52684->172.19.0.2:10051: An existing connection was forcibly closed by the remote host.\n",
	}
	for _, line := range lines {
		if _, err := writer.Write([]byte(line)); err != nil {
			t.Fatal(err)
		}
	}

	got := target.String()
	want := "ERROR[0010] [1408276967 1.15s] connection: connection upload closed: raw-read tcp4 172.19.0.1:52684->172.19.0.2:10051: An existing connection was forcibly closed by the remote host. [process: python.exe, PID 31337]\n"
	if !strings.Contains(got, want) {
		t.Fatalf("TUN error was not correlated:\n%s", got)
	}
}

func TestConnectionLogWriterFallsBackToContextProcessPath(t *testing.T) {
	var target bytes.Buffer
	writer := &connectionLogWriter{
		target:  &target,
		resolve: func([]string) trackedProcess { return trackedProcess{} },
	}
	_, _ = writer.Write([]byte(`INFO[0009] [123456789 0ms] router: found process path: C:\Apps\Fallback.exe` + "\n"))
	_, _ = writer.Write([]byte("ERROR[0010] [123456789 1.15s] connection: connection upload closed: raw-read tcp4 172.19.0.1:52684->172.19.0.2:10051: An existing connection was forcibly closed by the remote host.\n"))
	if !strings.Contains(target.String(), "[process: Fallback.exe]") {
		t.Fatalf("context path fallback missing: %q", target.String())
	}
}
