# Upstream sync, 11 October 2026

Integrated `throneproj/Throne` dev at
`9172ec37cfada7290e55b4e9b390abc27d2efcdb` into the fork at `a62620dd`:
42 upstream commits since the common ancestor
`125c3b010061593141a8ff2f6557b01b6da2835f`.
The upstream changes are integrated by merge, preserving both histories.

## Result

- Added the encrypted Throne API, IP scanner and reusable IP lists, endpoint
  overrides, experimental kill switch, ECH support and native global hotkeys.
- Updated the sing-box integration, tunnel DNS probing, subscription metadata,
  refresh scheduling, log controls, TLS validation for updates and elevation
  handling.
- Kept Throned's themes, routing editors, favourites, traffic and diagnostics,
  UDP tests, local control CLI, subscription meters and provider announcements.
- Preserved existing database fields while migrating subscription metadata and
  profile/group endpoint settings. The global subscription update switch still
  disables all automatic subscription refreshes.
- Removed upstream's obsolete Hijack/System DNS implementation. Legacy Hijack
  settings are read only to show the removal notice, with a separate dismissal
  marker from the previous deprecation notice.
- Kept the fork's packaging and release workflow. Upstream AUR templates that
  publish the official Throne package were excluded. RPM dependencies now cover
  Fedora and openSUSE package names.

The real profile check exposed a sing-box compatibility issue: DNS cannot use
an explicit detour through a bare Direct outbound. Config generation now omits
that detour for bare Direct profiles and retains it when Direct has additional
connection settings. A regression assertion covers both configurations.

## Verification

All checks below passed on Linux with Qt 6.8.2 and Go 1.27:

- Application build in `out/build/linux` and a separate build in
  `out/build/nopch` with both precompiled headers and unity compilation disabled.
- `ctest --test-dir out/build/linux --output-on-failure`: 22/22 test targets,
  including protocol round trips, notice dismissal, routing, diagnostics,
  database migration and multilingual UI geometry assertions.
- `cmake --build out/build/linux --target ui-smoke` and
  `BUILD_DIR=out/build/linux ./script/ui_snapshot.sh`: five UI scenarios rendered.
  Main, settings, routing, scanner and API screenshots were inspected.
- Core `go test -count=1 ./...`, plus the full production build tags with
  `-trimpath -ldflags=-checklinkname=0`, including Naive, QUIC, WireGuard,
  OpenVPN and OpenConnect. Existing Windows-path basename assertions now pass
  on Linux too.
- Updater `go test -count=1 -v ./...` with `umask 022`: seven tests passed.
- `script/build_go.sh`: core, updater and legacy launcher binaries built.
- `script/format_cpp.sh --check`, `script/lint_qt_idioms.sh`,
  `script/check_translations.sh`, shell syntax and Git whitespace checks.

Additional checks used isolated temporary data and loopback addresses: two GUI
starts, core IPC and local CLI, profile start/stop, and HTTP traffic through the
new core. An independent Python ChaCha20-Poly1305 client verified API status and
profile responses, multiple encrypted frames, a 404 response and rejection of
an incorrect key. No system proxy or TUN was enabled.

## Limits

Windows and macOS builds/runtime, native hotkeys, privileged TUN/firewall kill
switch operations, the root-only updater setuid fixture and actual RPM package
creation were not exercised in this Linux environment. Linux pixel baselines
are absent, so the screenshot run verifies rendering and internal assertions,
not a pixel comparison. New translation keys are tracked in all required
locales; existing unfinished translation backlogs remain.
