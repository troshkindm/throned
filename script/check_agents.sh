#!/usr/bin/env bash
# Keeps the two skill trees honest.
#
# The canonical copy of every skill lives in .agents/skills/, because that is
# where Codex looks. Claude Code only discovers skills under .claude/skills/, so
# each one has a stub there. A stub whose description has drifted is worse than
# no stub: the agent picks the skill for the wrong task, or fails to pick it for
# the right one.
set -uo pipefail

cd "$(dirname "$0")/.."
status=0

field() {
    sed -n "s/^$2: //p" "$1" | head -1
}

for canon in .agents/skills/*/SKILL.md; do
    [ -e "$canon" ] || continue
    name=$(basename "$(dirname "$canon")")
    stub=".claude/skills/$name/SKILL.md"

    if [ ! -f "$stub" ]; then
        echo "error: $canon has no stub at $stub"
        status=1
        continue
    fi
    if [ "$(field "$canon" name)" != "$(field "$stub" name)" ]; then
        echo "error: name differs between $canon and $stub"
        status=1
    fi
    if [ "$(field "$canon" description)" != "$(field "$stub" description)" ]; then
        echo "error: description differs between $canon and $stub"
        status=1
    fi
    if ! grep -qF ".agents/skills/$name/SKILL.md" "$stub"; then
        echo "error: $stub does not point at its canonical copy"
        status=1
    fi
done

for stub in .claude/skills/*/SKILL.md; do
    [ -e "$stub" ] || continue
    name=$(basename "$(dirname "$stub")")
    if [ ! -f ".agents/skills/$name/SKILL.md" ]; then
        echo "error: $stub has no canonical copy in .agents/skills/$name/"
        status=1
    fi
done


# A script committed without its executable bit fails CI with a bare "Permission
# denied", and Windows checkouts do not carry the mode, so it is easy to miss.
while read -r mode _ _ path; do
    if [ "$mode" != "100755" ]; then
        echo "error: $path is committed as $mode; run: git update-index --chmod=+x $path"
        status=1
    fi
done < <(git ls-files -s 'script/*.sh')

for required in AGENTS.md CLAUDE.md; do
    if [ ! -f "$required" ]; then
        echo "error: $required is missing"
        status=1
    fi
done

if [ "$(cat CLAUDE.md 2>/dev/null)" != "@AGENTS.md" ]; then
    echo "error: CLAUDE.md must be exactly '@AGENTS.md' so the two never drift"
    status=1
fi

((status)) && echo && echo "Agent instruction check failed."
((status)) || echo "Agent instruction check passed."
exit $status
