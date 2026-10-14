# Compact mode audit and upstream catch-up

Merged upstream `6906912f` into the fork, preserving its themes, protocol editors,
subscription-provider metadata, speed-test traffic accounting and comfortable
server rows. The four upstream commits add persistent group sorting, an offline
AmneziaWG QUIC-signature generator, the Linux test DNS fix and Go dependency
updates. Comfortable Ping sorting is pinned to latency independently of a group's
full-table test-sort preference. The migration preview also checks the new sort
columns alongside this fork's provider fields.

## Window and status layout

Compact mode keeps its own normal geometry through hiding, minimizing, startup
restoration and transitions from a maximized full window. Its width is bounded,
and the maximize control is hidden. Returning to the full interface restores its
independent normal or maximized geometry. Saving while hidden retains the last
valid compact geometry.

The old layout retained its widget item while the status browser was reparented.
Qt's replacement item could retain a stale size cache: starting a test hid the normal
footer but placed progress below the visible window. Detaching the original item
before reparenting, and destroying transferred items before adding replacements,
restores size invalidation. Test progress now sits
outside the two mode-specific containers so both interfaces can display it.

The subscription tab widget also suppresses Qt's document-mode corner bases,
which appeared as unwanted lines above the corner controls on Linux.

## Routing and sheets

- Named, blocked and WARP default targets survive compact editing. Referenced
  servers in other groups keep their names instead of appearing missing.
- Applying explicitly enables a disabled routing profile's rules. The page
  reports that disabled state before Apply.
- Staged rules belong to the profile they were edited in. Switching profiles
  retains separate drafts; asynchronous Apply retains its original profile and
  does not discard edits made after its snapshot.
- Background app/target updates retain input drafts, focus, cursor selection and
  scroll position. Search starts at the first result.
- Executable paths remain full `process_path` matches, separate from process-name
  rules. Rules extended with advanced conditions, or split across different
  targets, remain in the advanced editor and survive compact Apply.
- Long target names and executable paths fit the compact viewport; full values
  remain available in tooltips. Additional-rule notices wrap. Row headers can
  shrink, so the default target cannot stretch the entire scroll content.
- Subscription hover cards explicitly reject activation; clicking promotes them
  into focusable popups, and delayed click-away arming cannot affect a hover card.
- Server-group tabs are retained during unchanged background updates. Sorting
  and membership updates preserve the visible server and browsing selection.

## Verification

The new Qt tests cover route-page drafts and defaults, advanced-rule preservation,
process paths, narrow content, group-tab scrolling and server reorder anchors.
The isolated production previews cover tray-style restoration, minimize/maximize,
separate routing-profile drafts, SQLite persistence, generated connection rules,
and visible latency/speed progress in both interfaces. Preview fixtures use a
private database and reserved example values, with no proxy core, TUN or system
proxy changes.

Production Go tests and updater tests pass. A separate loopback-only IPC fixture
checks the built production core's signature RPC, invalid IP rejection, config
validation, start, a real HTTP request through its mixed listener, and stop.

Final verification passed on Linux with Qt 6.8.2:

- Normal and separate non-unity/no-PCH builds, with 26/26 CTest targets in each.
- `ui-smoke` and all 46 scenarios from the pinned `ui_snapshot.sh --all` run.
- Inspected compact sheets, routing, RTL, light/Mica styling, progress and the
  subscription hover card. Linux pixel baselines are absent, so the pinned run
  verifies scenario assertions and produces images for inspection; it does not
  establish a pixel-baseline match.
- C++ formatting, Qt idiom lint, translation checks, instruction checks and live
  Ninja header-dependency tracking.

Windows and macOS native runtime behavior is not verified on this Linux host.
