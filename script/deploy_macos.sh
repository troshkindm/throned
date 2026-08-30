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

cp -a $GITHUB_WORKSPACE/skins $GITHUB_WORKSPACE/build/Throned.app/Contents/MacOS/skins

codesign --force --deep --sign - $GITHUB_WORKSPACE/build/Throned.app

dsymutil $GITHUB_WORKSPACE/build/Throned.app/Contents/MacOS/Throned
strip -S $GITHUB_WORKSPACE/build/Throned.app/Contents/MacOS/Throned

mv $GITHUB_WORKSPACE/build/Throned.app $DEST
