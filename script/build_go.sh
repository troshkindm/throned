#!/bin/bash
set -e

TAGS="with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_dhcp,with_tailscale,with_openvpn,with_openconnect,badlinkname,tfogo_checklinkname0"

rm -rf $DEST
mkdir -p $DEST

[[ "$GOOS" =~ legacy$ ]] && IS_LEGACY=true && GOCMD="$PWD/golang.org/go/bin/go" && GOOS="${GOOS%legacy}" || { IS_LEGACY=false; GOCMD="go"; }

if [[ "$GOOS" == "windows" || "$GOOS" == "linux" ]]; then
    EXE_SUFFIX=""
    if [[ "$GOOS" == "windows" ]]; then
        EXE_SUFFIX=".exe"
    fi
    UPDATER_NAME="updater${EXE_SUFFIX}"
    LEGACY_NAME="Throne${EXE_SUFFIX}"
    UPDATER_DIR="$(cd "$(dirname "$0")/../updater" && pwd)"
    UPDATER_LDFLAGS="-w -s"
    if [[ "$GOOS" == "windows" ]]; then
        UPDATER_LDFLAGS+=" -H windowsgui"
    fi
    # The Linux core job exports Chromium's cross-C compiler.  The updater is
    # pure Go, so isolate it from that CGO toolchain before the core enables CGO.
    (cd "$UPDATER_DIR" && CGO_ENABLED=0 go build -trimpath -ldflags "$UPDATER_LDFLAGS" -o "$DEST/$UPDATER_NAME" .)
    (cd "$UPDATER_DIR" && CGO_ENABLED=0 go build -trimpath -ldflags "$UPDATER_LDFLAGS" -o "$DEST/$LEGACY_NAME" ./cmd/legacy-launcher)
    [[ "$GOOS" == "linux" ]] && chmod +x "$DEST/$UPDATER_NAME"
    [[ "$GOOS" == "linux" ]] && chmod +x "$DEST/$LEGACY_NAME"
fi

case "$GOOS" in
  windows)
    export CGO_ENABLED=0
    if ! $IS_LEGACY; then
      TAGS+=",with_purego,with_naive_outbound"
    fi
    ;;
  darwin)
    TAGS+=",with_naive_outbound"
    export CGO_ENABLED=1 CGO_LDFLAGS="-weak_framework UniformTypeIdentifiers"
    # cgo otherwise builds for the runner's SDK and hard-links every API newer than 10.15
    if $IS_LEGACY; then
      export MACOSX_DEPLOYMENT_TARGET=10.15
    fi
    ;;
  linux)
    TAGS+=",with_naive_outbound"
    export CGO_ENABLED=1
    ;;
esac

#### Go: core ####
pushd core
pushd gen
protoc -I . --go_out=. --go-grpc_out=. libcore.proto
popd
if [[ "$GOOS" == "windows" ]] && ! $IS_LEGACY; then
  # The lib module ships the DLL, so it is always the binding's generation.
  CRONET_LIB=github.com/sagernet/cronet-go/lib/windows_$GOARCH
  go mod download $CRONET_LIB
  install -m 644 "$(go list -m -f '{{.Dir}}' $CRONET_LIB)/libcronet.dll" $DEST/
fi
if [[ "$GOOS" == "darwin" ]]; then
  # cgo cannot detect a binding/lib ABI skew, so compare the headers the darwin lib was built against.
  CRONET_LIB=github.com/sagernet/cronet-go/lib/darwin_$GOARCH
  go mod download github.com/sagernet/cronet-go $CRONET_LIB
  CRONET_LIB_INCLUDE="$(go list -m -f '{{.Dir}}' $CRONET_LIB)/include"
  for header in "$(go list -m -f '{{.Dir}}' github.com/sagernet/cronet-go)"/include/*.h; do
    cmp -s "$header" "$CRONET_LIB_INCLUDE/${header##*/}" || { echo "cronet-go binding and $CRONET_LIB differ in ${header##*/}" >&2; exit 1; }
  done
fi
VERSION_SINGBOX=$(go list -m -f '{{.Version}}' github.com/sagernet/sing-box)
CORE_NAME="ThronedCore${EXE_SUFFIX:-}"
$GOCMD build -v -o "$DEST/$CORE_NAME" -trimpath -ldflags "-w -s -X 'github.com/sagernet/sing-box/constant.Version=${VERSION_SINGBOX}' -X 'internal/godebug.defaultGODEBUG=multipathtcp=0' -checklinkname=0" -tags "$TAGS"
popd
