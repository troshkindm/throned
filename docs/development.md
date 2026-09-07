# Development guide

## Repository map

| Path                 | Purpose                                                                              |
|----------------------|--------------------------------------------------------------------------------------|
| `src/`               | C++ implementation of the desktop application.                                       |
| `include/`           | C++ headers mirroring the feature layout under `src/`.                               |
| `core/server/`       | Go module that builds the proxy core process.                                        |
| `updater/`           | Go module for the updater and legacy launcher.                                       |
| `res/`               | Runtime assets: tray/app artwork, translations, emoji fonts, schema, and the embedded dashboard. |
| `skins/`             | Skin packages; shipped skins are embedded via `res/Throned.qrc`, with loose packages also supported. |
| `tests/`             | C++/Qt unit and integration test entry points.                                       |
| `tools/`             | Small focused developer utilities; production UI scenarios live under `src/ui/preview/`. |
| `3rdparty/`          | Vendored C and C++ dependencies. Do not reformat or lint these as project code.      |
| `cmake/`             | Platform setup, generated version resources, dependency helpers, and CMake patches.  |
| `script/`            | Build, packaging, formatting, lint, snapshot, and release helper scripts.            |
| `.agents/skills/`    | Canonical task skills shared by every agent; `.claude/skills/` holds stubs pointing at them. |
| `.github/workflows/` | Authoritative clean Windows and Linux CI builds.                                     |
| `docs/`              | Design notes, developer documentation, and reviewed UI screenshots.                  |

The top-level `CMakeLists.txt` contains the build options and the `Throned`
executable. Vendored libraries are isolated as CMake targets in
`cmake/ThronedDependencies.cmake`. Project sources and resources are collected in
`cmake/ThronedSources.cmake`; platform implementations remain explicit in
`cmake/windows`, `cmake/linux`, and `cmake/macos`. Test targets live beside their
sources in `tests/CMakeLists.txt`.

Files added under `src/` and `include/` are picked up automatically. CMake reruns
when one is added or removed. Vendored sources under `3rdparty/` stay explicitly
listed so an upstream file cannot silently become part of the application.

With the local `windows-clion-dev` preset selected in CLion, the equivalent command
line workflow is:

```powershell
cmake --preset windows-clion-dev
cmake --build --preset windows-clion-dev
ctest --preset windows-clion-dev
```

All generated files go below `out/build/`; no build directory belongs at the
repository root.

### Why the Windows presets pin `VSLANG=1033`

Ninja learns header dependencies by matching the prefix of `cl.exe`'s
`/showIncludes` lines against the one CMake recorded at configure time. A
localized toolchain prints that prefix in the console code page, so when
configure and build run in different environments the match silently fails.
Ninja then rebuilds nothing when a header changes, and a header that alters a
class layout leaves half the objects on the old one. That links, runs, and
crashes on a wild pointer with no bad code in sight. CI never sees it, because
CI always builds from an empty directory.

What matters is that both phases agree, not that the prefix is English: a
toolchain without the English pack keeps printing its own language under
`VSLANG=1033`, and that is fine as long as configure saw the same thing. The
top-level `CMakeLists.txt` only warns, deliberately — setting the variable there
would fix configure and not the build, which is the same mismatch pointing the
other way. It has to be in the environment of both, which is what
`CMakePresets.json` does.

Verify recorded dependencies after configuring, or when a header edit appears
not to take:

```sh
cmake -DBUILD_DIR=out/build/windows-dev -P script/check_ninja_deps.cmake
```

When wrapping a command from PowerShell, set `$env:VSLANG = '1033'` before
launching the build shell. In `cmd`, use `set "VSLANG=1033"`; an unquoted
`set VSLANG=1033 && ...` includes a trailing space in the value.

If the IDE and an agent need independent configure/build runs, use separate
trees under `out/build/` with the same local toolchain settings. A lock or Ninja
recompaction failure is a reason to investigate competing writers, not to delete
another process's cache. Keep preview outputs separate too, and identify the
built executable in the report so a stale binary cannot stand in for the result.

## UI icons and resources

Monochrome interface actions belong in `MaterialIcon::Glyph` in
`include/ui/widget/MaterialIcon.h`; their vector paths are rendered and tinted by
`src/ui/widget/MaterialIcon.cpp`. Do not add one PNG per button, color, state, or
screen density.

Keep files in `res/` when they carry image data that cannot be represented by a
single tinted glyph: application and tray artwork, the checkbox mark used from a
Qt stylesheet, emoji fonts, translations, schemas, and dashboard files. Optional
theme artwork belongs in a skin under `skins/`.

## Local-only paths

The following paths are generated locally and must stay out of Git:

| Path | Purpose |
| --- | --- |
| `CMakeUserPresets.json` | Developer-specific Qt, OpenSSL, toolchain, and build-directory paths. |
| `.idea/` | CLion workspace settings. |
| `out/` | Active local build outputs. |
| `config/` | Runtime databases, logs, cache, and crash data created by the application. |
| `deployment/` | Packaged application output. |

## C++ formatting and analysis

`.clang-format` is a four-space Google-derived style with no column limit and no
include sorting. `.clang-format-ignore` and `script/format_cpp.sh` both exclude
`3rdparty/` and `core/server/gen/`: vendored code stays byte-identical to
upstream so a future merge is a real diff, and generated code is regenerated
rather than edited.

```sh
./script/format_cpp.sh            # rewrite
./script/format_cpp.sh --check    # report, exit non-zero if anything would change
```

clang-format reformats differently between major versions, so CI pins one
(`pip install clang-format==19.1.5`) and a workstation should use the same. The
MSVC build tools ship a matching copy under
`<VS install>/VC/Tools/Llvm/x64/bin/clang-format.exe`; set `CLANG_FORMAT` to
point the script at it.

The repository was baselined in a single reformat commit listed in
`.git-blame-ignore-revs`. Configure Git once so blame skips it:

```sh
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

Never mix a reformat with a behaviour change in one commit.

`.clang-tidy` runs the compiler analyser plus a conservative set of correctness
and performance checks. It deliberately omits the broad style and modernisation
groups while the existing code is still being baselined. CLion reads it once
**Prefer `.clang-tidy` files over IDE settings** is enabled under:

```text
Settings | Editor | Inspections | C/C++ | Static Analysis Tools | Clang-Tidy
```

`script/lint_qt_idioms.sh` catches project-specific Qt mistakes that generic
analysers do not: arithmetic on `QDialog::exec()`, signals invoked as methods,
and deprecated `QCheckBox::stateChanged`. `script/check_agents.sh` verifies that
`.claude/skills/` stubs still match their canonical copies in `.agents/skills/`.
Both run in the `checks` CI job, which needs neither a compiler nor Qt and
reports while the builds are still running.

## Visual UI scenarios

The screenshot suite launches the real `Throned` executable with a temporary
database and synthetic RFC 5737 / example-domain data. Scenario code lives in
`src/ui/preview/`; the catalogue and process runner is
`script/run_ui_scenarios.cmake`. A run keeps `actual/`, `diff/`, `logs/` and
`manifest.json` under its output directory.

There are two distinct uses for these images, and they have different rules.

### Comparing: the gate

A rendered glyph is a function of the Qt build, the font files, the DPI and the
platform plugin. Comparison only means anything when all four are identical
everywhere it runs, so they are pinned in `script/ui_snapshot.sh` — offscreen
platform, DejaVu Sans from a fixed path, 96 DPI, scale factor 1 — and CI runs
that script unchanged.

```sh
./script/ui_snapshot.sh            # compare the smoke set
./script/ui_snapshot.sh --all      # compare the full catalogue
./script/ui_snapshot.sh --update   # accept new baselines
```

Baselines live in `tests/ui/baselines/linux-offscreen/`. Inspect `actual/` and
`diff/` before accepting an update, and commit the new images with the change
that moved them. Never loosen `CHANNEL_TOLERANCE` or `MAX_DIFFERENT_RATIO` to
make a run pass: a tolerant pixel test is a green blind test. If a diff is
noise, the environment is not pinned; fix that instead.

### Looking: the gallery

Everything else renders images for a human. In CLion, build the `ui-smoke`
target; `ui-all` covers the complete catalogue.

```powershell
cmake --build --preset windows-clion-dev --target ui-smoke
cmake --build --preset windows-clion-dev --target ui-all
```

These compare against `tests/ui/baselines/windows/`, which is a convenience for
one workstation. Accept intentional changes there with the
`ui-update-baselines` target. Windows CI renders the same catalogue with
`-DCOMPARE_BASELINES=OFF` and uploads it as an artifact without ever failing the
build, because its Qt build and its fonts are not the ones those baselines were
recorded with.

Drive a single screen directly when investigating one:

```powershell
cmake '-DTHRONED_EXECUTABLE=out/build/windows-dev/Throned.exe' `
  '-DSCENARIOS=settings' -P script/run_ui_scenarios.cmake
```


### Native Windows materials

For native Windows 11 Mica inspection, leave the isolated production preview open:

```powershell
out/build/windows-dev/Throned.exe -ui-preview out/mica -ui-preview-backdrop -theme "Mica (Windows 11)" -many
```

This uses the same temporary database and synthetic data as the screenshot modes,
but enables the desktop compositor and skips automatic captures and exit. Inspect
the active window, resize/maximize it, and switch to another theme and back in
Settings. Ordinary captures disable native materials so wallpaper and activation
cannot affect baselines. Mica uses the desktop wallpaper, and Windows substitutes
a solid material when the window is inactive or transparency effects are disabled.
The skin exposes the window shell and dialog bodies. Cards use WinUI's dark
`CardBackgroundFillColorDefault` (white at 13/255) and `Secondary` (8/255) layers
over Mica; controls use translucent fills with a stronger hover and focus edge,
while the fallback palette uses neutral solid greys. The index gutter and footer
expose the material, and subscription notices use a brighter film and accent edge.
The card values come from Microsoft's `Common_themeresources_any.xaml` in
`microsoft-ui-xaml`. Popup menus use a matching solid surface because they are
separate native windows without the main window's backdrop.

Inspect the table gutter, footer, announcement, input focus, toggle states,
open menus and combo selection after the opening animation settles. Also check
activation, maximize/restore, and switching to a solid theme and back. Capture
the target window itself; an inactive or occluded capture is not proof of the
active material. Do not change the user's system transparency settings for a test.

Mica follows the system color scheme. Its manifest supplies `lightColors` and
dark/light `styleVariables` for the shared stylesheet. Qt's `colorSchemeChanged`
reloads these after the platform palette update, then refreshes native windows
and controls together. `throned_mica_theme_change` exercises dark/light/dark
through Qt's process-local override against the isolated preview; it never
changes Windows personalization settings.

Material styles are gated by the window's `custom-style` property, set only when
the native backdrop succeeds. Keep opaque fallback colors and route native
changes through `ThronedChrome`. Setting `WA_TranslucentBackground` at runtime
interferes with the native composition; expose the material through transparent
Qt surfaces instead. After a property or theme change, descendant stylesheet
caches need to be refreshed, not just the title bar repainted.

For combo popups, Qt's menu mode paints an extra native panel beneath the list.
The Mica skin uses `combobox-popup: 0` and explicit selected-item styling; check
keyboard selection as well as the closed field before changing those rules.

### Footer notices

For adding a message, start with the
[ui-notices skill](../.agents/skills/ui-notices/SKILL.md), which covers placement,
eligibility, dismissal lifetime and focused verification.

`UpdateStatusWidget` shares the footer slot between updates and queued notices.
`postNotice()` replaces an existing stable ID; error and warning notices sort
ahead of tips, then by priority. Downloading, preparing, ready and failed updates
own the slot until dismissed; queued notices then resume. Producers handle action
and dismissal signals and decide which dismissals to persist.

`WindowNotices.cpp` offers Mica once when its skin is available and not already
selected. Enabling, dismissing, or manually selecting Mica records the versioned
notice ID in `SettingsRepo::dismissed_notices`. Adding a future tip needs its own
ID, eligibility rule, translated text and action. No release-number comparison or
network call is needed: the feature first appears when users install its release.

For interactive inspection add `-ui-preview-notices` to a backdrop preview launched
with `-theme "Throned Midnight"`. Add `-ui-preview-update-ready` to put a synthetic
ready update above the queued tip; click Later to reveal it. Restart and retry
actions do nothing in preview mode. Ordinary snapshots suppress real tip eligibility.

### What a screenshot cannot answer

Beside every compared capture the preview also writes a geometry report: each
named widget's class, box, visibility, and whether its text no longer fits.
`script/run_ui_scenarios.cmake` compares those as text when a `.json` baseline
sits next to the image, so a failure names the widget instead of handing over a
picture. It is font-independent, which is why it can run where pixel comparison
cannot be trusted.

Three things are deliberately not reported, because each was a false positive
when tried: widgets Qt names itself, text on a widget that has never been shown
(it still sits at its default size), and text the code elided on purpose — an
ellipsis at the end is a decision, not an accident.
