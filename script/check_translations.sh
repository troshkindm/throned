#!/usr/bin/env bash
# Fails when a user-visible string was added to one .ts file and not the others.
#
# The .ts files are edited by hand — the lupdate target rewrites all four and
# buries the change — so a new string reaches whichever file the author happened
# to open. Qt then falls back to the source text, which looks fine in English and
# means the string is invisible to whoever could translate it. Present but
# `type="unfinished"` is a tracked gap; absent is a lost one.
#
# Only strings added since the base commit are checked. The existing backlog is
# reported, never gated: zh_CN and fa_IR are hundreds of strings behind and a
# gate on that would be red forever and therefore ignored.
#
#   ./script/check_translations.sh [base-ref]
set -uo pipefail

cd "$(dirname "$0")/.."

BASE=${1:-${BASE:-origin/dev}}
# en_US carries only plural forms: Qt cannot derive an English plural from one
# source string. Everything else there is a copy of the source and unnecessary.
TRANSLATED=(ru_RU zh_CN fa_IR)
status=0

sources_at() { # ref, locale — empty ref means the working tree
    if [ -z "$1" ]; then
        cat "res/translations/$2.ts"
    else
        git show "$1:res/translations/$2.ts" 2>/dev/null
    fi | grep -o '<source>[^<]*</source>' | sort -u
}

# ^{commit} forces a lookup: a bare 40-hex base verifies fine while its object is
# gone, and git show then yields nothing, which reads as "every string is new".
if ! git rev-parse --verify --quiet "$BASE^{commit}" >/dev/null; then
    echo "warning: base ref '$BASE' not found; checking the backlog only"
    BASE=""
fi

if [ -n "$BASE" ]; then
    for locale in "${TRANSLATED[@]}"; do
        sources_at "$BASE" "$locale"
        sources_at "" "$locale"
    done | sort -u > /tmp/tr_all_now.$$
    for locale in "${TRANSLATED[@]}"; do sources_at "$BASE" "$locale"; done | sort -u > /tmp/tr_all_base.$$
    comm -13 /tmp/tr_all_base.$$ /tmp/tr_all_now.$$ > /tmp/tr_new.$$

    added=$(wc -l < /tmp/tr_new.$$)
    if [ "$added" -gt 0 ]; then
        echo "strings added since $BASE: $added"
        for locale in "${TRANSLATED[@]}"; do
            sources_at "" "$locale" > /tmp/tr_$locale.$$
            missing=$(comm -23 /tmp/tr_new.$$ /tmp/tr_$locale.$$)
            if [ -n "$missing" ]; then
                status=1
                echo "error: missing from $locale.ts:"
                echo "$missing" | sed 's/<source>/  /; s|</source>||' | head -20
            fi
        done
    fi
    rm -f /tmp/tr_*.$$
fi

echo
echo "backlog (not gated):"
for locale in "${TRANSLATED[@]}"; do
    total=$(grep -c '<source>' "res/translations/$locale.ts")
    unfinished=$(grep -c 'type="unfinished"' "res/translations/$locale.ts")
    printf '  %-6s %5d strings, %4d unfinished\n' "$locale" "$total" "$unfinished"
done

((status)) && echo && echo "Translation check failed. Add the string with <translation type=\"unfinished\"></translation> where you cannot translate it."
((status)) || echo && echo "Translation check passed."
exit $status
