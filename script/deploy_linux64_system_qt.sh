#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy binary ####
cp $GITHUB_WORKSPACE/build/Throned $DEST

#### copy Throned.png ####
cp $GITHUB_WORKSPACE/res/public/Throned.png $DEST/Throned.png

#### copy Core ####
source "$(dirname "$0")/extract_core_artifact.sh"
cp deployment/${DEST_SUFFIX%-system-qt}/ThronedCore $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/updater $DEST
cp deployment/${DEST_SUFFIX%-system-qt}/Throne $DEST
rm -rf deployment/${DEST_SUFFIX%-system-qt}

# handle debug info
objcopy --only-keep-debug $DEST/Throned $DEST/Throned.debug
strip --strip-debug --strip-unneeded $DEST/Throned
objcopy --add-gnu-debuglink=$DEST/Throned.debug $DEST/Throned
