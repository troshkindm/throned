package guard

import (
	"errors"
	"io"
	"io/fs"
	"log"
	"os"
	"strings"
	"syscall"
)

// CleanupStale flushes the pf anchors of guards that died without disarming (SIGKILL, crash, power loss).
func CleanupStale() {
	cleanupStale("")
}

func cleanupStale(own string) {
	if os.Geteuid() != 0 {
		return
	}
	anchors, err := pfctl(nil, "-a", pfRootAnchor, "-s", "Anchors")
	if err != nil {
		return
	}
	for line := range strings.Lines(anchors.stdout) {
		id, ok := strings.CutPrefix(strings.TrimSpace(line), pfAnchorPrefix)
		if ok && id != own && pfAnchorIDPattern.MatchString(id) {
			cleanupAnchor(id)
		}
	}
}

func cleanupAnchor(id string) {
	anchor := pfAnchorPrefix + id
	lock, err := os.OpenFile(pfLockPath(id), os.O_RDONLY|syscall.O_NOFOLLOW|syscall.O_NONBLOCK, 0)
	if errors.Is(err, fs.ErrNotExist) {
		if err = flushAnchor(anchor); err != nil {
			log.Printf("kill switch: %v", err)
			return
		}
		log.Printf("kill switch: removed the orphaned pf anchor %s", anchor)
		return
	}
	if err != nil {
		return
	}
	defer lock.Close()
	if syscall.Flock(int(lock.Fd()), syscall.LOCK_EX|syscall.LOCK_NB) != nil {
		return
	}
	// Unlinked since we opened it: another cleaner already handled this guard.
	var stat syscall.Stat_t
	if syscall.Fstat(int(lock.Fd()), &stat) != nil || stat.Nlink == 0 {
		return
	}
	content, _ := io.ReadAll(io.LimitReader(lock, 128))
	if err = flushAnchor(anchor); err != nil {
		log.Printf("kill switch: %v", err)
		return
	}
	if fields := strings.Fields(string(content)); len(fields) == 2 {
		_ = releaseToken(fields[0], fields[1])
	}
	_ = os.Remove(lock.Name())
	log.Printf("kill switch: removed the pf anchor %s of a dead guard", anchor)
}
