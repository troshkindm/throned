package guard

// Windows identifies the core by its executable, so there is nothing to apply.
func IdentityEnabled() bool { return false }

func ApplyIdentity() error { return nil }
