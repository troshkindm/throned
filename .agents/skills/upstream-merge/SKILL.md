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

## The conflicts this repository always produces

**Formatting.** Every first-party file was reformatted in one commit, and
upstream still carries the old layout. Git will report conflicts that carry no
meaning at all. Suppress them, then re-apply the project style:

```sh
git merge -X ignore-all-space upstream/dev
./script/format_cpp.sh
```

`-X ignore-all-space` removes most of it, not all: a hunk where upstream changed
code *and* indentation still conflicts, and there the whitespace is noise around
a real change. Resolve on the code.

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
`core/`, note that `core/server/gen/*.pb.go` is generated and not committed.
