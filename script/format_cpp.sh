#!/usr/bin/env bash
# Formats first-party C and C++ with clang-format, or reports what is unformatted.
#
# clang-format's output changes between major versions, so CI pins one and every
# workstation should use the same. The MSVC build tools ship a copy under
# <VS install>/VC/Tools/Llvm/x64/bin.
#
#   ./script/format_cpp.sh            rewrite the files
#   ./script/format_cpp.sh --check    exit non-zero if anything would change
set -uo pipefail

cd "$(dirname "$0")/.."

CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
if ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
    echo "error: $CLANG_FORMAT not found; set CLANG_FORMAT to its path" >&2
    exit 2
fi

check=0
case "${1:-}" in
    --check) check=1 ;;
    "") ;;
    *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac

mapfile -t files < <(
    git ls-files --cached --others --exclude-standard -- '*.c' '*.cc' '*.cpp' '*.h' '*.hpp' \
        | grep -v '^3rdparty/' \
        | grep -v '^core/server/gen/'
)

if ((check)); then
    unformatted=()
    for file in "${files[@]}"; do
        "$CLANG_FORMAT" --style=file "$file" | diff -q - "$file" >/dev/null 2>&1 || unformatted+=("$file")
    done
    if ((${#unformatted[@]})); then
        printf 'unformatted (%d of %d):\n' "${#unformatted[@]}" "${#files[@]}"
        printf '  %s\n' "${unformatted[@]}"
        echo
        echo "Run ./script/format_cpp.sh"
        exit 1
    fi
    echo "Formatting check passed: ${#files[@]} files."
    exit 0
fi

printf '%s\0' "${files[@]}" | xargs -0 "$CLANG_FORMAT" -i
echo "Formatted ${#files[@]} files."
