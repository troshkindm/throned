#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy golang => .app ####
source "$(dirname "$0")/extract_core_artifact.sh"

mv deployment/$DEST_SUFFIX/* $GITHUB_WORKSPACE/build/Throned.app/Contents/MacOS

#### deploy qt & Dylib runtime => .app ####
pushd $GITHUB_WORKSPACE/build
macdeployqt Throned.app -verbose=3
popd

APP=$GITHUB_WORKSPACE/build/Throned.app

cp -a $GITHUB_WORKSPACE/skins "$APP/Contents/MacOS/skins"

# install-qt-action's Qt is universal; keep only the architecture this build targets.
find "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f | while read -r f; do
    [ "$(lipo -archs "$f" 2>/dev/null | wc -w)" -gt 1 ] || continue
    lipo "$f" -thin "$ARCH" -output "$f.thin"
    mv "$f.thin" "$f"
done

# Outside the bundle, so that packing the app without it does not break the seal.
dsymutil "$APP/Contents/MacOS/Throned" -o "$DEST/Throned.dSYM"
strip -S "$APP/Contents/MacOS/Throned"

# Last: rewriting any binary after this invalidates its signature.
codesign --force --deep --sign - "$APP"

mv "$APP" $DEST
