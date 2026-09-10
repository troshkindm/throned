package main

import (
	"archive/zip"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"testing"
)

func TestUpdateReplacesProtectedExecutables(t *testing.T) {
	if os.Getenv("THRONED_UPDATER_INSTALL_HELPER") == "1" {
		file, err := os.OpenFile("ThronedCore", os.O_WRONLY, 0)
		if err == nil {
			file.Close()
			t.Fatal("fixture core must reject writes by the updater user")
		}
		if !os.IsPermission(err) {
			t.Fatalf("open protected core: %v", err)
		}
		var samples [][2]uint64
		if err := updateWithProgress(func(_ string, completed, total uint64) {
			samples = append(samples, [2]uint64{completed, total})
		}); err != nil {
			t.Fatal(err)
		}
		requireMonotonicProgress(t, samples)
		return
	}

	dir := t.TempDir()
	installDir := filepath.Join(dir, "Throned install")
	if err := os.Mkdir(installDir, 0o755); err != nil {
		t.Fatal(err)
	}
	core := filepath.Join(installDir, "ThronedCore")
	if err := os.WriteFile(core, []byte("old core"), 0o555); err != nil {
		t.Fatal(err)
	}
	config := filepath.Join(installDir, "config.json")
	if err := os.WriteFile(config, []byte("existing settings"), 0o644); err != nil {
		t.Fatal(err)
	}

	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	input, err := os.Open(executable)
	if err != nil {
		t.Fatal(err)
	}
	defer input.Close()
	updater := filepath.Join(installDir, "updater")
	output, err := os.OpenFile(updater, os.O_CREATE|os.O_WRONLY, 0o755)
	if err != nil {
		t.Fatal(err)
	}
	_, copyErr := io.Copy(output, input)
	closeErr := output.Close()
	if copyErr != nil {
		t.Fatal(copyErr)
	}
	if closeErr != nil {
		t.Fatal(closeErr)
	}

	archive, err := os.Create(filepath.Join(installDir, packageName))
	if err != nil {
		t.Fatal(err)
	}
	writer := zip.NewWriter(archive)
	for _, name := range []string{"Throned", "ThronedCore", "updater"} {
		header := &zip.FileHeader{Name: rootName + "/" + name}
		header.SetMode(0o755)
		entry, err := writer.CreateHeader(header)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := io.WriteString(entry, "new "+name); err != nil {
			t.Fatal(err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	if err := archive.Close(); err != nil {
		t.Fatal(err)
	}

	command := exec.Command(updater, "-test.run=^TestUpdateReplacesProtectedExecutables$")
	command.Dir = installDir
	command.Env = append(os.Environ(), "THRONED_UPDATER_INSTALL_HELPER=1")
	if os.Geteuid() == 0 {
		// Only the fixture directory belongs to the updater user; the old core stays root-owned.
		for _, parent := range []string{filepath.Dir(dir), dir} {
			if err := os.Chmod(parent, 0o755); err != nil {
				t.Fatal(err)
			}
		}
		if err := os.Chown(installDir, 65534, 65534); err != nil {
			t.Fatal(err)
		}
		if err := os.Chmod(core, os.ModeSetuid|0o755); err != nil {
			t.Fatal(err)
		}
		command.SysProcAttr = &syscall.SysProcAttr{Credential: &syscall.Credential{Uid: 65534, Gid: 65534}}
		t.Log("updating a root-owned setuid core as uid 65534")
	} else {
		t.Log("updating a read-only core as the current user; run as root for the setuid fixture")
	}
	if data, err := command.CombinedOutput(); err != nil {
		t.Fatalf("update protected executables: %v\n%s", err, data)
	}
	for _, name := range []string{"Throned", "ThronedCore", "updater"} {
		path := filepath.Join(installDir, name)
		data, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		if string(data) != "new "+name {
			t.Fatalf("%s contents = %q", name, data)
		}
		info, err := os.Stat(path)
		if err != nil {
			t.Fatal(err)
		}
		if info.Mode() != 0o755 {
			t.Fatalf("%s mode = %v, want executable without setuid", name, info.Mode())
		}
		if info.Sys().(*syscall.Stat_t).Uid == 0 {
			t.Fatalf("%s must belong to the unprivileged updater user", name)
		}
	}
	if data, err := os.ReadFile(config); err != nil || string(data) != "existing settings" {
		t.Fatalf("existing settings changed: %q, %v", data, err)
	}
	entries, err := os.ReadDir(installDir)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 4 {
		t.Fatalf("update left temporary files: %v", entries)
	}
}
