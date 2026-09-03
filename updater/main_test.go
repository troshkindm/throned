package main

import (
	"archive/zip"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

func requireMonotonicProgress(t *testing.T, samples [][2]uint64) {
	t.Helper()
	if len(samples) < 2 {
		t.Fatalf("expected multiple progress samples, got %v", samples)
	}
	for index, sample := range samples {
		if sample[0] > sample[1] {
			t.Fatalf("sample %d exceeds total: %v", index, sample)
		}
		if index > 0 && sample[0] < samples[index-1][0] {
			t.Fatalf("progress moved backwards at %d: %v", index, samples)
		}
	}
	last := samples[len(samples)-1]
	if last[0] != last[1] {
		t.Fatalf("progress did not reach total: %v", samples)
	}
}

func TestExtractRejectsPathTraversal(t *testing.T) {
	dir := t.TempDir()
	archivePath := filepath.Join(dir, "unsafe.zip")
	file, err := os.Create(archivePath)
	if err != nil {
		t.Fatal(err)
	}
	writer := zip.NewWriter(file)
	entry, err := writer.Create("../outside.txt")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := entry.Write([]byte("unsafe")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}

	if err := extract(archivePath, filepath.Join(dir, "stage")); err == nil {
		t.Fatal("expected traversal path to be rejected")
	}
	if _, err := os.Stat(filepath.Join(dir, "outside.txt")); !os.IsNotExist(err) {
		t.Fatalf("archive escaped staging directory: %v", err)
	}
}

func TestExtractAndCopyTree(t *testing.T) {
	dir := t.TempDir()
	archivePath := filepath.Join(dir, "update.zip")
	file, err := os.Create(archivePath)
	if err != nil {
		t.Fatal(err)
	}
	writer := zip.NewWriter(file)
	entry, err := writer.Create("Throned/Throned.exe")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := entry.Write([]byte("new binary")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}

	stage := filepath.Join(dir, "stage")
	var extractSamples [][2]uint64
	if err := extractWithProgress(archivePath, stage, func(completed, total uint64) {
		extractSamples = append(extractSamples, [2]uint64{completed, total})
	}); err != nil {
		t.Fatal(err)
	}
	requireMonotonicProgress(t, extractSamples)
	destination := filepath.Join(dir, "install")
	var copySamples [][2]uint64
	if err := copyTreeWithProgress(filepath.Join(stage, rootName), destination, func(completed, total uint64) {
		copySamples = append(copySamples, [2]uint64{completed, total})
	}); err != nil {
		t.Fatal(err)
	}
	requireMonotonicProgress(t, copySamples)
	data, err := os.ReadFile(filepath.Join(destination, "Throned.exe"))
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "new binary" {
		t.Fatalf("copied contents = %q", data)
	}
}

func TestParseUpdaterOptions(t *testing.T) {
	options, err := parseUpdaterOptions([]string{
		"--lang", "RU", "--parent-pid", "4123", "--executable", `C:\Throned\Throned.exe`, "--launch-tray",
	})
	if err != nil {
		t.Fatal(err)
	}
	if options.language != "ru" || options.parentPID != 4123 ||
		options.executable != `C:\Throned\Throned.exe` || !options.launchTray {
		t.Fatalf("unexpected options: %+v", options)
	}
}

func TestParseUpdaterOptionsRejectsInvalidParent(t *testing.T) {
	if _, err := parseUpdaterOptions([]string{"--parent-pid", "none"}); err == nil {
		t.Fatal("expected invalid parent process id to fail")
	}
}

func TestWaitForParent(t *testing.T) {
	if os.Getenv("THRONED_UPDATER_WAIT_HELPER") == "1" {
		time.Sleep(150 * time.Millisecond)
		return
	}
	command := exec.Command(os.Args[0], "-test.run=TestWaitForParent")
	command.Env = append(os.Environ(), "THRONED_UPDATER_WAIT_HELPER=1")
	if err := command.Start(); err != nil {
		t.Fatal(err)
	}
	defer command.Process.Kill()
	if err := waitForParent(command.Process.Pid, 3*time.Second); err != nil {
		t.Fatal(err)
	}
}
