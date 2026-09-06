# UI scenarios

`script/run_ui_scenarios.cmake` launches the production `Throned` executable,
drives deterministic preview states and writes one PNG per state. It is used for
two different jobs, and confusing them is what makes screenshot testing rot.

## The gate: `script/ui_snapshot.sh`

A rendered glyph depends on the Qt build, the font files, the DPI and the
platform plugin. Comparison is only meaningful when all four are identical
everywhere it runs, so they are pinned in `script/ui_snapshot.sh` and nowhere
else — offscreen platform, DejaVu Sans from a fixed path, 96 DPI, scale factor
1. CI runs that script unchanged, which is what makes a baseline recorded on a
workstation a baseline the gate accepts.

```sh
./script/ui_snapshot.sh            # compare the smoke set
./script/ui_snapshot.sh --all      # compare the full catalogue
./script/ui_snapshot.sh --update   # rewrite the baselines it compares against
```

Baselines live in `tests/ui/baselines/linux-offscreen/`. Review `actual/` and
`diff/` before accepting an update, and commit the new PNG files together with
the change that moved them.

## The gallery: `ui-smoke` and `ui-all`

Everything else is a picture for a human to look at, not an assertion.

```sh
cmake --build --preset windows-clion-dev --target ui-smoke
cmake --build --preset windows-clion-dev --target ui-all
```

These compare against `tests/ui/baselines/windows/`, which is a convenience for
the maintainer's workstation only. The Windows CI job renders the same
catalogue with `-DCOMPARE_BASELINES=OFF` and uploads it as an artifact; it never
fails a build, because its Qt build and its fonts are not the ones the baselines
were recorded with.

## Scenario coverage

`smoke` is `quick-add`, `selection`, `settings`, `route-simple` and
`route-advanced`. `all` adds the diagnostics, subscription, sites, hover,
favorites, theme and localisation states.

The route editor refuses to shrink below roughly 1120x700, so the former
`route-compact` and `route-1085x761` scenarios rendered pixel-identical output
to `route-simple` and were removed. Whether a dialog fits a given size is a
layout question and belongs in a geometry assertion, not in a screenshot.

## Safety

Previews always run against a temporary database holding reserved example
domains and RFC 5737 documentation addresses. They never start a proxy core,
change the system proxy, enable TUN, or read a real profile.
