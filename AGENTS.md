# Throned

Qt 6 Widgets desktop proxy client, C++20 and CMake, driven by a sing-box/Xray
core. It is an unofficial fork of [Throne](https://github.com/throneproj/Throne),
GPL-3.0, shipping for Windows and Linux. macOS builds from source but is not
covered by CI.

This file is the contract for anyone — human or agent — changing the
repository. Keep it short and factual, and update it when the workflow moves.
Explanations belong in `docs/development.md`; nothing here should be a fact a
ten-second grep would answer.

## Shape

| Path                            | What it is                                                                                                    |
|---------------------------------|---------------------------------------------------------------------------------------------------------------|
| `src/`, `include/`              | First-party C++. The two trees mirror each other; a file added to either is picked up by CMake automatically. |
| `src/sys/{windows,linux,macos}` | Operating-system code. Listed explicitly in the matching `cmake/<platform>` file, not globbed.                |
| `src/ui/preview/`               | Screenshot and preview modes of the production widgets. Not a mock.                                           |
| `core/server/`, `updater/`      | Go modules: the proxy core process and the updater. `gen/*.pb.go` is generated, not committed.                |
| `3rdparty/`                     | Vendored C and C++. Each dependency is its own CMake target in `cmake/ThronedDependencies.cmake`.             |
| `res/`, `skins/`                | Compiled Qt resources and loose skin packages.                                                                |
| `tests/`                        | Qt Test targets plus the UI snapshot baselines.                                                               |
| `script/`                       | Build, packaging, lint, format and snapshot helpers.                                                          |
| `.github/workflows/`            | The authoritative clean builds.                                                                               |

## Remotes and upstream

`origin` is `troshkindm/throned`. `upstream` is `throneproj/Throne`, the project
this forked from, and it is fetch-only: its push URL is set to `DISABLED` on
purpose, so `git push upstream` fails loudly instead of aiming a branch at
somebody else's repository. Restore that if a fresh clone loses it:

```sh
git remote set-url --push upstream DISABLED
```

Everything lands on `origin/dev`, the default branch here. Pull requests are the
exception, not the rule, so a change is expected to be complete and checked
before it is pushed. Plain `git push` from `dev` goes to the right place; naming
a remote by hand is how it goes to the wrong one.

Upstream arrives by merge, never by rebase. That merge is a review, not a
formality: upstream and this fork have solved several of the same problems in
different ways, so a clean automatic merge can silently revert work done here,
and the repository-wide reformat means every merge now also reports whitespace
conflicts that carry no meaning. The survey commands, the standing theme and
`.ui` rules and the verification steps are in
`.agents/skills/upstream-merge/SKILL.md`.

## Build

Never commit machine-specific SDK or toolchain paths. They belong in the ignored
`CMakeUserPresets.json`.

Windows, with the maintainer's local preset:

```powershell
cmake --preset windows-clion-dev
cmake --build --preset windows-clion-dev
ctest --preset windows-clion-dev
```

Linux:

```sh
cmake -S . -B out/build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/build/linux
ctest --test-dir out/build/linux --output-on-failure
```

Generated trees go below `out/`. Never create a build directory at the
repository root.

### Trust the linker, not the exit code

On Windows with Ninja and MSVC, ninja learns header dependencies by stripping a
prefix from `cl.exe`'s `/showIncludes` output, and it stores the prefix CMake
saw at configure time. Configure and build in different shells — different
`VSLANG`, different console code page — and the prefix stops matching. Ninja
then records nothing, header edits rebuild nothing, and a header that changes a
class layout leaves half the objects on the old one. The result links, exits 0,
runs, and crashes on a wild pointer with no bad code in sight. A clean build
never shows it.

After configuring a tree, and whenever an edit appears not to take:

```sh
cmake -DBUILD_DIR=out/build/windows-dev -P script/check_ninja_deps.cmake
```

If it fails, the tree is lying to you and only a clean build is trustworthy.
Configure and build from the same environment; the Windows presets pin
`VSLANG=1033` for exactly this reason. `docs/development.md` has the full
mechanism.

## Checks

Run what the change touches; say which ones you ran and which you could not.

```sh
./script/lint_qt_idioms.sh    # Qt mistakes that compile cleanly and fail at runtime
./script/format_cpp.sh        # clang-format over first-party sources
./script/ui_snapshot.sh       # the pinned UI snapshot gate (Linux)
```

`ctest` after any application or library change. For UI work also build the
`ui-smoke` target and look at the images.

UI snapshots come in two kinds and the difference matters. `script/ui_snapshot.sh`
pins the platform plugin, the font, the DPI and the scale factor, and is the only
run allowed to fail a build over pixels. The `ui-smoke` and `ui-all` targets and
the Windows CI job render pictures for a human to look at and never assert. Never
widen `CHANNEL_TOLERANCE` or `MAX_DIFFERENT_RATIO` to make a run pass — a
tolerant pixel test is a green blind test. See `tests/ui/README.md`.

Previews and tests must not touch user data or the network beyond what they
declare: a temporary database, reserved example domains, RFC 5737 addresses, no
proxy core, no system proxy change, no TUN.

## Editing rules

- Follow `.clang-format` and `.clang-tidy`. Vendored and generated code is
  excluded on purpose; do not reformat `3rdparty/` or `core/server/gen/`.
- Comments earn their place by explaining a decision that is not obvious from
  the code. One line is usually enough. Do not narrate what the next line does.
- Prefer a small feature-specific translation unit over adding unrelated logic
  to `src/main.cpp` or another already-large file.
- Monochrome interface actions are `MaterialIcon::Glyph` vector paths. Do not
  add a PNG per button, colour, state or density.
- `.ui` files stay. Existing screens keep their designer file; only genuinely
  new screens are built in code.
- Translations: edit `res/translations/*.ts` by hand. The `lupdate` target
  rewrites all four files and buries the change.
- Do not weaken or delete a test to make a change pass. When behaviour changes
  on purpose, update the test and state the new contract.

## Commits

One Conventional Commits subject line, English, imperative, lower case after
the type:

```
fix: keep filtered traffic statistics consistent
feat: split traffic statistics into proxied and direct
```

No body, no footers, no trailers of any kind — including co-author and
generated-by trailers. The log is read as a changelog, so the subject carries
the whole message; authorship lives in the commit author field. Do not commit or
push unless you were asked to.

## Reporting your work

State what changed, which checks ran, and which could not run and why. Point at
exact files and commands. Do not claim a platform was tested from a different
operating system. Leave unrelated working-tree changes alone.
