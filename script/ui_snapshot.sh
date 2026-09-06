#!/usr/bin/env bash
# The single entry point for rendering and comparing UI snapshots.
#
# A screenshot is a function of the Qt build, the font files, the DPI and the
# platform plugin. Comparison is only meaningful when all four are identical on
# every machine that runs it, so they are pinned here and nowhere else: CI runs
# this script unchanged, which is what makes a baseline recorded locally a
# baseline the gate accepts.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build}
SCENARIOS=${SCENARIOS:-smoke}
UPDATE=OFF

for argument in "$@"; do
    case "$argument" in
        --update) UPDATE=ON ;;
        --all) SCENARIOS=all ;;
        *) echo "usage: $0 [--update] [--all]" >&2; exit 2 ;;
    esac
done

export QT_QPA_PLATFORM=offscreen
export QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu
export QT_FONT_DPI=96
export QT_SCALE_FACTOR=1
export QT_ENABLE_HIGHDPI_SCALING=0
export LC_ALL=C.UTF-8

cmake \
    -DTHRONED_EXECUTABLE="$PWD/$BUILD_DIR/Throned" \
    -DSNAPSHOT_COMPARE="$PWD/$BUILD_DIR/throned_snapshot_compare" \
    -DBASELINE_DIR="$PWD/tests/ui/baselines/linux-offscreen" \
    -DOUTPUT_DIR="$PWD/out/ui-scenarios" \
    -DSCENARIOS="$SCENARIOS" \
    -DPREVIEW_FONT="DejaVu Sans" \
    -DUPDATE_BASELINES="$UPDATE" \
    -P script/run_ui_scenarios.cmake
