---
name: build
description: Configure, build and test Throned on Windows or Linux, and verify that the build tree is actually tracking header dependencies. Use before running anything, after changing CMake files, or whenever an edit appears to have no effect on the binary.
---

# Building Throned

Qt 6 Widgets, C++20, CMake with the Ninja generator. Machine-specific paths live
in the ignored `CMakeUserPresets.json` and never in a committed file.

## Windows

```powershell
cmake --preset windows-clion-dev
cmake --build --preset windows-clion-dev
ctest --preset windows-clion-dev
```

The configure and build presets both pin `VSLANG=1033`; read the next section
before working around that. From a bare shell the MSVC environment has to be
loaded first (`vcvars64.bat`), or `cl.exe` is not on `PATH`.

Useful targets: `Throned` (the application), `throned_snapshot_compare` (the
image comparator), `ui-smoke` and `ui-all` (screenshots), `ui-update-baselines`.

## Linux

```sh
cmake -S . -B out/build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/build/linux
ctest --test-dir out/build/linux --output-on-failure
```

The C++ build does not need the Go core: `core/server/gen/libcore.proto` is
committed and the C++ side generates from it. Only packaging needs the built
core. `srslist.h` is fetched into the build directory by CI; a local tree uses
the copy at the repository root.

## A green build is not proof

Ninja learns header dependencies by stripping a prefix from `cl.exe`'s
`/showIncludes` output, and stores the prefix CMake observed at configure time.
Configure the tree in one shell and build it in another — a different `VSLANG`,
a different console code page — and the prefix stops matching. Ninja silently
records nothing. Header edits then rebuild nothing, a header that changes a
class layout leaves half the objects on the old one, and the binary links, exits
0, runs, and crashes on a wild pointer with no bad code in sight.

This is not theoretical; it has been observed in this repository's own
development tree, where 55 objects had zero recorded dependencies.

```sh
cmake -DBUILD_DIR=out/build/windows-dev -P script/check_ninja_deps.cmake
```

Run it after configuring, and any time an edit appears not to take. If it fails:

1. Configure and build from the same shell and environment.
2. Installing the MSVC English language pack makes `VSLANG=1033` effective and
   takes the console code page out of the equation.
3. Until the check passes, only a clean build is trustworthy. Deleting the
   object files of the affected target is enough; a full wipe is not needed.

Unity builds make this worse, not better: a source reaches ninja only through
the depfile, so a lost depfile loses the `.cpp` too, not just its headers.

## Pitfalls

- Build directories belong under `out/`, never at the repository root.
- `CMakeLists.txt` globs `src/` and `include/` with `CONFIGURE_DEPENDS`; adding
  a file needs no CMake edit, but `src/sys/{windows,linux,macos}` is excluded
  from the glob and listed explicitly in `cmake/<platform>/<platform>.cmake`. A
  new platform source that is not added there is silently not compiled.
- The exit code of a wrapped build command can be the wrapper's, not the
  compiler's. Read the last lines of the output, and check that the linker ran.
