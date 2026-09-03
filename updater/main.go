package main

import (
	"archive/zip"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

const (
	packageName = "Throned.zip"
	stageName   = ".throned-update"
	rootName    = "Throned"
)

type progressFunc func(message string, completed, total uint64)

var updaterLanguage = "en"
var updaterLogger = log.New(io.Discard, "", log.LstdFlags|log.Lmicroseconds)

type updaterOptions struct {
	language   string
	parentPID  int
	executable string
	launchTray bool
}

func main() {
	installDir, logFile, err := initializeUpdaterRuntime()
	if err != nil {
		showUpdateError(err)
		os.Exit(1)
	}
	if logFile != nil {
		defer logFile.Close()
	}
	options, err := parseUpdaterOptions(os.Args[1:])
	if err != nil {
		failUpdate(err)
	}
	updaterLanguage = options.language
	updaterLogger.Printf("updater started: pid=%d parent=%d executable=%q tray=%t", os.Getpid(), options.parentPID,
		options.executable, options.launchTray)
	if previewRequested() {
		runProgressPreview()
		return
	}
	progress := newInstallProgress(700 * time.Millisecond)
	if options.parentPID > 0 {
		progress.Update(localize("Waiting for Throned to close…", "Ожидание завершения Throned…"), 5, 1000)
		if err := waitForParent(options.parentPID, 30*time.Second); err != nil {
			progress.Close()
			failUpdate(err)
		}
		updaterLogger.Printf("parent process %d exited", options.parentPID)
	}
	if err := updateWithProgress(progress.Update); err != nil {
		progress.Close()
		failUpdate(err)
	}

	progress.Update(localize("Starting Throned…", "Запуск Throned…"), 1000, 1000)
	progress.Close()
	if err := restartThroned(installDir, options); err != nil {
		failUpdate(err)
	}
	updaterLogger.Print("update completed and Throned stayed alive after restart")
}

func initializeUpdaterRuntime() (string, *os.File, error) {
	executable, err := os.Executable()
	if err != nil {
		return "", nil, fmt.Errorf("resolve updater location: %w", err)
	}
	installDir := filepath.Dir(executable)
	if err := os.Chdir(installDir); err != nil {
		return "", nil, fmt.Errorf("open installation directory: %w", err)
	}
	logFile, logErr := os.OpenFile(filepath.Join(installDir, "updater.log"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if logErr == nil {
		updaterLogger = log.New(logFile, "", log.LstdFlags|log.Lmicroseconds)
	}
	return installDir, logFile, nil
}

func parseUpdaterOptions(args []string) (updaterOptions, error) {
	options := updaterOptions{language: "en"}
	for index := 0; index < len(args); index++ {
		name := args[index]
		switch name {
		case "--lang", "--parent-pid", "--executable":
			if index+1 >= len(args) {
				return options, fmt.Errorf("%s requires a value", name)
			}
			value := args[index+1]
			index++
			switch name {
			case "--lang":
				options.language = strings.ToLower(value)
			case "--parent-pid":
				pid, err := strconv.Atoi(value)
				if err != nil || pid <= 0 {
					return options, fmt.Errorf("invalid parent process id %q", value)
				}
				options.parentPID = pid
			case "--executable":
				options.executable = value
			}
		case "--launch-tray":
			options.launchTray = true
		}
	}
	return options, nil
}

func failUpdate(err error) {
	updaterLogger.Printf("update failed: %v", err)
	_, _ = fmt.Fprintln(os.Stderr, "Throned update failed:", err)
	showUpdateError(err)
	os.Exit(1)
}

func restartThroned(installDir string, options updaterOptions) error {
	executable := options.executable
	if executable == "" {
		executable = filepath.Join(installDir, "Throned")
		if runtime.GOOS == "windows" {
			executable += ".exe"
		}
	}
	if !filepath.IsAbs(executable) {
		executable = filepath.Join(installDir, executable)
	}
	arguments := []string{}
	if options.launchTray {
		arguments = append(arguments, "-tray")
	}
	command := exec.Command(executable, arguments...)
	command.Dir = installDir
	if err := command.Start(); err != nil {
		return fmt.Errorf("could not restart Throned: %w", err)
	}
	updaterLogger.Printf("started Throned: pid=%d executable=%q args=%q", command.Process.Pid, executable, arguments)
	done := make(chan error, 1)
	go func() { done <- command.Wait() }()
	select {
	case err := <-done:
		if err == nil {
			return fmt.Errorf("Throned exited immediately after the update")
		}
		return fmt.Errorf("Throned exited immediately after the update: %w", err)
	case <-time.After(3 * time.Second):
		return nil
	}
}

func localize(english, russian string) string {
	if strings.HasPrefix(updaterLanguage, "ru") {
		return russian
	}
	return english
}

func previewRequested() bool {
	for _, arg := range os.Args[1:] {
		if arg == "--ui-preview" {
			return true
		}
	}
	return false
}

// A zero-I/O harness for checking the native updater window. It never reads the
// real package or installation directory.
func runProgressPreview() {
	progress := newInstallProgress(250 * time.Millisecond)
	phases := []string{
		localize("Checking the update package…", "Проверка пакета обновления…"),
		localize("Extracting the update…", "Распаковка обновления…"),
		localize("Installing files…", "Установка файлов…"),
		localize("Finishing the update…", "Завершение обновления…"),
	}
	for step := uint64(0); step <= 1000; step += 20 {
		phase := phases[0]
		switch {
		case step >= 900:
			phase = phases[3]
		case step >= 560:
			phase = phases[2]
		case step >= 80:
			phase = phases[1]
		}
		progress.Update(phase, step, 1000)
		time.Sleep(150 * time.Millisecond)
	}
	progress.Close()
}

func update() error {
	return updateWithProgress(nil)
}

func updateWithProgress(report progressFunc) error {
	reportProgress(report, localize("Checking the update package…", "Проверка пакета обновления…"), 10, 1000)
	if _, err := os.Stat(packageName); err != nil {
		return fmt.Errorf("update package not found: %w", err)
	}
	if err := os.RemoveAll(stageName); err != nil {
		return fmt.Errorf("clear staging directory: %w", err)
	}
	defer os.RemoveAll(stageName)

	if err := extractWithProgress(packageName, stageName, func(completed, total uint64) {
		reportProgress(report, localize("Extracting the update…", "Распаковка обновления…"), 20+scaled(completed, total, 540), 1000)
	}); err != nil {
		return err
	}
	source := filepath.Join(stageName, rootName)
	if info, err := os.Stat(source); err != nil || !info.IsDir() {
		return fmt.Errorf("%s directory is missing from update package", rootName)
	}
	if err := copyTreeWithProgress(source, ".", func(completed, total uint64) {
		reportProgress(report, localize("Installing files…", "Установка файлов…"), 580+scaled(completed, total, 370), 1000)
	}); err != nil {
		return err
	}
	reportProgress(report, localize("Finishing the update…", "Завершение обновления…"), 970, 1000)
	if err := os.Remove(packageName); err != nil {
		return fmt.Errorf("remove update package: %w", err)
	}
	reportProgress(report, localize("Update installed", "Обновление установлено"), 1000, 1000)
	return nil
}

func extract(source, destination string) error {
	return extractWithProgress(source, destination, nil)
}

func reportProgress(report progressFunc, message string, completed, total uint64) {
	if report != nil {
		report(message, completed, total)
	}
}

func scaled(completed, total, span uint64) uint64 {
	if total == 0 || completed >= total {
		return span
	}
	return completed * span / total
}

type progressWriter struct {
	writer    io.Writer
	completed *uint64
	total     uint64
	report    func(completed, total uint64)
}

func (w *progressWriter) Write(data []byte) (int, error) {
	n, err := w.writer.Write(data)
	*w.completed += uint64(n)
	if w.report != nil {
		w.report(*w.completed, w.total)
	}
	return n, err
}

func extractWithProgress(source, destination string, report func(completed, total uint64)) error {
	archive, err := zip.OpenReader(source)
	if err != nil {
		return fmt.Errorf("open update package: %w", err)
	}
	defer archive.Close()

	root, err := filepath.Abs(destination)
	if err != nil {
		return err
	}
	var total uint64
	for _, entry := range archive.File {
		if !entry.FileInfo().IsDir() {
			total += entry.UncompressedSize64
		}
	}
	var completed uint64
	if report != nil {
		report(0, total)
	}
	for _, entry := range archive.File {
		clean := filepath.Clean(filepath.FromSlash(entry.Name))
		if clean == "." || filepath.IsAbs(clean) || clean == ".." ||
			strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
			return fmt.Errorf("unsafe path in update package: %q", entry.Name)
		}
		target := filepath.Join(root, clean)
		if target != root && !strings.HasPrefix(target, root+string(filepath.Separator)) {
			return fmt.Errorf("path escapes staging directory: %q", entry.Name)
		}
		if entry.FileInfo().IsDir() {
			if err := os.MkdirAll(target, 0o755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		input, err := entry.Open()
		if err != nil {
			return err
		}
		mode := entry.Mode()
		if mode == 0 {
			mode = 0o644
		}
		output, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, mode)
		if err != nil {
			input.Close()
			return err
		}
		writer := &progressWriter{writer: output, completed: &completed, total: total, report: report}
		_, copyErr := io.Copy(writer, input)
		closeErr := output.Close()
		input.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}
	if report != nil && total == 0 {
		report(1, 1)
	}
	return nil
}

func copyTree(source, destination string) error {
	return copyTreeWithProgress(source, destination, nil)
}

func copyTreeWithProgress(source, destination string, report func(completed, total uint64)) error {
	if err := os.MkdirAll(destination, 0o755); err != nil {
		return err
	}
	var total uint64
	if err := filepath.Walk(source, func(_ string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if !info.IsDir() {
			total += uint64(info.Size())
		}
		return nil
	}); err != nil {
		return err
	}
	var completed uint64
	if report != nil {
		report(0, total)
	}
	err := filepath.Walk(source, func(path string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		relative, err := filepath.Rel(source, path)
		if err != nil || relative == "." {
			return err
		}
		target := filepath.Join(destination, relative)
		if info.IsDir() {
			return os.MkdirAll(target, info.Mode())
		}
		input, err := os.Open(path)
		if err != nil {
			return err
		}
		output, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, info.Mode())
		if err != nil {
			input.Close()
			return err
		}
		writer := &progressWriter{writer: output, completed: &completed, total: total, report: report}
		_, copyErr := io.Copy(writer, input)
		closeErr := output.Close()
		input.Close()
		if copyErr != nil {
			return copyErr
		}
		return closeErr
	})
	if err == nil && report != nil && total == 0 {
		report(1, 1)
	}
	return err
}
