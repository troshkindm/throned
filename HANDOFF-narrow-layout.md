# Handoff: narrow main window layout (work in progress)

> **Temporary note. Delete this file in the commit that finishes the work, before
> the branch is merged — it must never land on `dev`.**

## Goal

Issue troshkindm/throned#5 (a Persian-speaking user): the main window refuses to
shrink below 960×680. The owner asked for a good-looking compact layout and
screenshots of it. Do not comment on the issue unless the owner asks.

Instead of a smaller hard floor, the window folds into a narrow layout below the
width the labelled header needs, and the minimum becomes whatever the folded
layout needs (design floor 560×360).

## Branches

- `claude/kind-wozniak-6si9km` — upstream Throne merge (3 commits, verified, pushed).
- `claude/narrow-layout-wip` — this work, one WIP commit on top of the merge.
  It builds (CI-style Linux build, no PCH/unity) and renders; it is not finished.

## Done in the WIP commit

- `src/ui/mainWindow/mainwindow_narrow.cpp` (new): `applyTopBarMetrics()` moved
  here from `mainwindow_view.cpp`; `setNarrowLayout()`, `updateNarrowLayout()`,
  `applyWindowMinimum()`, `metricColumnsOverflowing()`.
- Narrow mode (window narrower than `fullLayoutWidth`, the header's labelled
  `sizeHint` width): nav buttons icon-only with the label as tooltip, toggle
  labels "TUN mode"/"System proxy" → "TUN"/"Proxy", status bar keeps only
  Connection and Routing cells, strip hint hidden, search field 268 → 170 px,
  the five selection buttons fold into one "Test" menu button, stats tabs elide.
- Metric columns: Traffic, then Speed, drop when the table viewport cannot hold
  `ProfileRowDelegate::serverColumnFloor()` + Ping + them
  (`applyProfileColumnVisibility()` honours `narrowHiddenMetrics`).
- `resizeEvent` calls `updateNarrowLayout()`.
- Preview: `-ui-preview-size` floor lowered to 480×400; new `main-narrow`
  scenario (560×440) in `script/run_ui_scenarios.cmake`.

## Problems seen in the WIP screenshots, and the planned fix for each

1. **Table collapses to zero rows** at 560×440 with the stats panel open.
   Fix: table minimum height = `horizontalHeader()->sizeHint().height()` +
   `verticalHeader()->defaultSectionSize()` + `2 * frameWidth()`; set it at the
   end of `refreshProfileRowStyle()` and in `applyTopBarMetrics()` (font changes).
   The splitter has `setChildrenCollapsible(false)`, so it is respected.
2. **Window minimum is stale / wrong per state.** `windowMinimumClosed/Open` are
   measured synchronously after flipping visibility, but box layouts cache child
   hints, and a flip made and undone in one call never reaches the event loop.
   Fix: drop both fields. Measure `narrowMinimumWidth` in `applyTopBarMetrics()`
   in the narrow configuration after invalidating every cache
   (`updateGeometry()` on every child widget, `invalidate()` on every `QLayout`).
   Add `bool event(QEvent *) override` to `MainWindow`: after
   `QMainWindow::event()` handles `QEvent::LayoutRequest` (the layout is already
   activated by then), call `applyWindowMinimum()`, which uses the live
   `minimumSizeHint().height()` and width
   `narrowLayout ? max(narrowMinimumWidth, content.width()) : narrowMinimumWidth`,
   expanded to `designMinimumSize`, then `FitWindowToScreen(this)`. Remove the
   three `applyWindowMinimum()` calls from `setStatsPanelOpen()`. This also
   covers the update footer, selection card and group announcement appearing.
3. **Stats tabs elide to "L…", "Connect…"** when the Connections tab's corner
   tools are visible (QTabBar takes space equally from every tab). Fix: short
   labels in narrow mode, full text as tooltip, for the tab bar and the strip
   buttons (`statsStripTabs`, matched by their `statsPage` property):
   "Traffic Graph" → `tr("Graph")`, "Runtime Stats" → `tr("Runtime")`. Capture
   the full text from `tabText()` at setup. Keep `ElideRight` as the fallback.
4. **Footer text is clipped mid-word** ("Restart when cor"). Fix in
   `UpdateStatusWidget.cpp`: a local `QLabel` subclass that paints
   `fontMetrics().elidedText(...)` via `style()->drawItemText(...)`, used for
   `title_` and `detail_`; the title overrides `minimumSizeHint()` to width 0
   (policy stays `Maximum`), so it elides only after the detail is gone.
5. **Server column too narrow.** At 560 the exit IP is middle-elided
   ("198.5…00.24"); at 760 names are truncated while all columns stay. Fix
   `serverColumnFloor()`: add the country badge (`captionFont` "WW" + 9) and its
   7 px gap to the exit part, plus a chip allowance
   (`chipWidth(small, "Shadowsocks") + kChipGap`) so the name gets as much room
   as the address. Expected: Traffic drops below ~860 px, Speed below ~750 px.
6. **560×360 cannot be rendered**: lower the preview floor to 480×320 in
   `src/ui/preview/MainWindowPreview.cpp`.
7. **Translations** (hand-edit `res/translations/*.ts`, MainWindow context):
   "TUN" → ru/zh/fa "TUN"; "Graph" → ru "График", zh "图表", fa "نمودار";
   "Runtime" → ru "Статистика", zh "运行时", fa unfinished. "Proxy" and "Test"
   already exist in MainWindow context (ru "Прокси", "Тесты"). Then run
   `./script/check_translations.sh`.

## Build and render

```sh
cmake -S . -B out/build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTHRONED_PRECOMPILE_HEADERS=OFF -DTHRONED_UNITY_BUILD=OFF
cmake --build out/build/linux
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/linux --output-on-failure
```

`out/build/linux/srslist.h` must exist before configuring; fetch it from
`https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h`.

Render the main-shell flow at a size (writes `<prefix>-*.png`):

```sh
export QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu \
  QT_FONT_DPI=96 QT_SCALE_FACTOR=1 QT_ENABLE_HIGHDPI_SCALING=0 LC_ALL=C.UTF-8
out/build/linux/Throned -ui-preview out/shots/n560 -ui-preview-docs \
  -theme "Throned Graphite" -ui-preview-size 560x440 -ui-preview-font "DejaVu Sans"
```

Render 560×360, 560×440 (panel open), 760×540 and 1180×780 (must look exactly
like before), plus `-ui-preview-selection` at 560×720 (that scenario needs empty
space under the rows). A size below the preview floor is silently replaced by
1180×780, so check the PNG dimensions. The offscreen screen is small, so
`FitWindowToScreen` also caps the minimum there.

## Before finishing

- `./script/format_cpp.sh`, `./script/lint_qt_idioms.sh`,
  `./script/check_translations.sh`, `./script/check_agents.sh`, `ctest`.
- `tests/ui/baselines/linux-offscreen/` is empty, so the pinned snapshot gate
  compares nothing; the renders are for people to look at.
- Show the owner the screenshots (they asked to see them), then squash into one
  commit, e.g. `feat: fold the main window into a narrow layout instead of refusing to shrink`.
  Commit rules are in `AGENTS.md`: one subject line, no body, no trailers.
- Delete this file in that commit.
