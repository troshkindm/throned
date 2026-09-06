---
name: ui-snapshot
description: Render and compare Throned's UI screenshots — the pinned gate versus the look-at-it gallery, how to read a diff, and when a baseline may be updated. Use when a change touches widgets, layout, themes or stylesheets, or when a snapshot run failed.
---

# UI snapshots

`script/run_ui_scenarios.cmake` launches the production `Throned` executable,
drives deterministic preview states and writes one PNG per state. It serves two
jobs with different rules, and mixing them is how screenshot suites rot.

## Why a screenshot suite goes red for no reason

A rendered glyph is a function of the Qt build, the font files and their
rasterisation, the DPI and scale factor, and the platform plugin. Change any one
and every antialiased edge moves. Pixel comparison is only meaningful where all
of them are identical — which is the same reason Playwright's snapshot tests are
run inside a fixed container image rather than on whatever the developer has.

So: pin the environment, never widen the tolerance. A tolerant pixel test is a
green blind test.

## The gate

`script/ui_snapshot.sh` pins the offscreen platform plugin, DejaVu Sans from a
fixed path, 96 DPI and scale factor 1, and passes the family to the application
as `-ui-preview-font`. CI runs the same script unchanged, which is what makes a
baseline recorded locally a baseline the gate accepts.

```sh
./script/ui_snapshot.sh            # compare the smoke set
./script/ui_snapshot.sh --all      # compare the full catalogue
./script/ui_snapshot.sh --update   # accept new baselines
```

Baselines: `tests/ui/baselines/linux-offscreen/`. The `ui-snapshots` CI job is
the only job allowed to fail a build over pixels.

## The gallery

Everything else renders pictures for a human.

```sh
cmake --build --preset windows-clion-dev --target ui-smoke
cmake --build --preset windows-clion-dev --target ui-all
```

These compare against `tests/ui/baselines/windows/`, which is a convenience for
one workstation. Windows CI renders the catalogue with `-DCOMPARE_BASELINES=OFF`
and uploads it as an artifact without ever failing the build, because its Qt
build and its fonts are not the ones those baselines came from.

## Reading a failure

Output lands under the run's directory: `actual/`, `diff/`, `logs/` and
`manifest.json`.

- **A scenario exited non-zero.** Read `logs/<scenario>.log`. Exit code 2 from a
  preview mode is a failed internal assertion, not a crash.
- **`size mismatch`.** The diff image is the two captures side by side on
  magenta. A changed window size is usually a layout change, not noise.
- **A scatter of magenta along text.** The environment is not pinned. Fix that
  rather than the tolerance.
- **A solid magenta region.** A real change. Look at it and decide.

## Updating baselines

Only after looking at `actual/` and `diff/`, and only in the same commit as the
change that moved them. A baseline updated on its own is a lost regression.
Never update baselines to clear a failure you have not explained.


## Geometry reports

Beside every compared capture the preview writes a `.json` report: each named
widget's class, box, visibility, and whether its text no longer fits. The runner
compares it as text when a baseline sits next to the image, so a failure names
the widget rather than handing over a picture, and it does not depend on a single
rasterised glyph.

Read a failure by diffing the two files the message points at. A changed `w` or
`x` is a layout move; a new `"cut": true` carries the offending text with it.

Three things are deliberately not reported, each having been a false positive
when tried: widgets Qt names itself, text on a widget never shown (it still sits
at its default size), and text the code elided on purpose — a trailing ellipsis
is a decision, not an accident. Measurement goes through `sizeHint()`, never
`QWidget::font()`, because a stylesheet `font-size` never reaches the latter.

## What a screenshot cannot answer

Whether a dialog fits a given size. The former `route-compact` and
`route-1085x761` scenarios asked exactly that and produced output byte-identical
to `route-simple`, because the route editor refuses to shrink below roughly
1120x700 and `resize()` was silently clamped. Two scenarios asserted nothing for
as long as they existed. Questions of that shape belong in a geometry assertion
over the widget tree.

## Safety

Previews run against a temporary database holding reserved example domains and
RFC 5737 documentation addresses. They must never start a proxy core, change the
system proxy, enable TUN, or read a real profile. A preview that needs one of
those is wrong.
