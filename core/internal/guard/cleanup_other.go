//go:build !darwin

package guard

// CleanupStale is a no-op where the kernel removes a dead guard's rules itself.
func CleanupStale() {}
