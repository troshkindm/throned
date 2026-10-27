---
name: upstream-merge
description: Merge changes from the upstream Throne project into this fork — surveying what is new, resolving the conflicts this repository reliably produces, and verifying the result. Use when asked to catch up with upstream, sync the fork, or when an upstream merge conflicted.
---

# Merging upstream Throne

`upstream` is `throneproj/Throne`, fetch-only: its push URL is `DISABLED` so a
mistyped `git push` cannot aim a branch at somebody else's repository. `origin`
is `troshkindm/throned`.

Upstream arrives by **merge, never rebase** — a rebase would replay 200+ local
commits onto foreign history. Commit it as `chore: merge upstream Throne
updates`, or as `fix: ... and merge upstream Throne updates` when the merge
needed real repair work.

## Survey before merging

Never merge blind. The size of the diff says nothing about the size of the job.

```sh
git fetch upstream
base=$(git merge-base HEAD upstream/dev)
git log --oneline $base..upstream/dev          # what is new
git diff --stat $base..upstream/dev            # how big it really is
```

Then measure the collision, which is what actually costs time — for each file
upstream touched, how far this fork has moved on the same file:

```sh
for f in $(git diff --name-only $base..upstream/dev); do
    printf '%-50s %s\n' "$f" "$(git diff --numstat $base..HEAD -- "$f" | cut -f1,2)"
done
```

Read the upstream diff itself before merging it. Several upstream changes are
alternative solutions to problems this fork already solved differently, and
taking one silently reverts work here.

When a batch contains a structural commit — one that moves code into new
directories — merge in steps: up to its parent, then that commit alone, then the
rest. Each merge commit then resolves one kind of conflict, and a trial merge in
a scratch `git worktree` measures each step before you commit to it.

## The conflicts this repository always produces

**Formatting.** Every first-party file was reformatted in one commit, and
upstream still carries the old layout. `-X ignore-all-space` alone is not
enough: the reformat also moved braces and rewrapped lines, which a
whitespace-blind diff still sees as edits. Run base, ours and theirs through the
project's clang-format *before* the three-way merge — `-X renormalize` with a
clean filter that exists only for this one command:

```sh
cf=$(command -v clang-format)    # must be the version CI pins
attrs=$(mktemp)
printf '%s filter=cf\n' '*.c' '*.cc' '*.cpp' '*.h' '*.hpp' > "$attrs"
printf '%s -filter\n' '3rdparty/**' 'core/gen/**' >> "$attrs"
git -c core.attributesFile="$attrs" \
    -c filter.cf.clean="$cf --style=file:$PWD/.clang-format --assume-filename=%f" \
    merge -X renormalize -X ignore-all-space upstream/dev
```

On the October 2026 merge this halved the job: 174 conflicted files became 82,
507 hunks became 266.

- The clang-format version must be the one `throned-windows.yml` pins
  (`pip install clang-format==<that version>` into a venv). With another major
  the filter reports committed files as modified and the merge refuses to start.
- The filter lives on that command only, so resolving and `git add` afterwards
  see plain files. Run `./script/format_cpp.sh` once everything is resolved:
  files only upstream added are not renormalized.
- What still conflicts is a real overlap, sometimes with whitespace noise around
  it. Resolve on the code.

**Moved files.** When upstream deletes or moves a file this fork edited, git
reports modify/delete and keeps our copy where it was. The fork's edits have to
be ported by hand into wherever upstream put that code; diff the file
`$base..HEAD -w` to see exactly what they were.

**Theming.** This fork's theming (`ThronedPalette.hpp`, its own `ThemeManager`
with `Colors()`, `RegisterStyle()`, `RefreshRegisteredStyles()`) and upstream's
`ThemeTokens` plus an app-level overlay stylesheet are two separate solutions to
one problem. Ours wins, always:

```sh
git checkout --ours include/ui/setting/ThemeManager.hpp src/ui/setting/ThemeManager.cpp
grep -rn 'themeManager->tokens' src include
```

Rewrite every hit against `Colors()` — `tokens.muted` becomes `textMuted`,
`tokens.info` becomes `accent`. Files that have needed it before:
`RawRouteItem.cpp`, `RouteItem.cpp`, `DataViewHtmlGenerator.cpp` (twice),
`dialog_basic_settings.cpp`.

Take upstream *fixes* inside their theme code even though the code is theirs —
their `qApp->setFont(qApp->font())` after `setStyleSheet` applies here too,
because these themes call `setStyle()` as well.

**`.ui` files.** Upstream's hardcoded-QSS deletions are safe to take wherever
this code already calls `setStyleSheet({})` on that widget. Their `colorRole`
dynamic-property swaps in `GroupItem.ui` and `ProxyItem.ui` are **not**: the
property is inert without their stylesheet, so taking it silently removes
colour. Also watch for two `setCornerWidget` calls fighting over
`stats_widget`'s top-right corner.

**Standing decisions.** Where upstream and this fork solved the same problem,
these sides were chosen on the October 2026 catch-up; keep them unless the user
says otherwise.

- Startup is upstream's `Bootstrap`/`GuiStartup`/`InstanceServer`. The fork adds
  the preview modes, `--cli` (`src/control/ControlClient.cpp`) and JSON control
  lines on a user-only instance socket (`GuiStartup::HandleLaunchLine`).
- The fork's updater, `ThroughputChart`, header sort arrows, settings dialog
  `.ui` and routing editor stay. Upstream features inside them are ported by
  hand: new settings widgets are copied into the fork's `.ui`, the routing editor
  took the rule on/off switch, the DNS tab and the shared outbound model.
  `dialog_update`, `CHANGELOG.md` and `LiveChart` are not taken.
- "Simple" belongs to the fork's simple mode (`simple_mode`, `setSimpleMode`);
  upstream's hide-advanced mode is `simple_interface`/`setSimpleInterface` here.
- A server routing target (`SimpleTarget` with `serverID`) is stored in the
  fork's via-profile bucket, which the Basic tab edits.
- Packaging is the fork's: upstream's headless units, `thronectl`, AUR, Docker
  and Android CI are not shipped. `engine_boundary` has no E4, because sources
  are globbed here.
- Strings upstream adds say `Throned`, binaries `ThronedCore`; the legacy
  `Throne/config` path and links to upstream keep the old name.
- When both sides add table columns, recount every `getColumn(N)` in the
  repositories' row readers; the merge keeps both and the indexes go stale
  silently.

**Translations.** Merge `.ts` files message by message, never as text: keep our
file as the skeleton, take upstream's translation only where ours still equals
the base, add messages that are new upstream. Do not keep `lupdate`'s rewrite.
Instead run it once on a scratch copy over the merged code, and once over the
pre-merge code. Drop merged-in messages the code no longer uses. Add strings new
to the code to `ru_RU`, `zh_CN` and `fa_IR` as unfinished. Then
`./script/check_translations.sh` decides what else needs filling.

## Verify

A merge that compiles is not a merge that worked. Conflicts resolved wrongly in
this repository usually surface as a widget that lost its colour, not as an
error.

```sh
cmake --build out/build/windows-dev
ctest --test-dir out/build/windows-dev --output-on-failure
cmake --build out/build/windows-dev --target ui-smoke
./script/format_cpp.sh --check
./script/lint_qt_idioms.sh
```

Look at the `ui-smoke` images even when they pass. If the merge touched Go under
`core/`, note that `core/gen/*.pb.go` is generated and not committed.
