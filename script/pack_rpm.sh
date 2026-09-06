#!/bin/bash
set -ex

# Wraps an already-deployed portable build into an RPM using fpm.
# Arguments mirror pack_debian.sh:
#   pack_rpm.sh <version> <arch> [systemqt]
# where <arch> is the Throned name (amd64 / arm64), not the RPM one.

VERSION="$1"
ARCH="$2"
SYSTEM_QT="$3"

case "$ARCH" in
    amd64) RPM_ARCH="x86_64" ;;
    arm64) RPM_ARCH="aarch64" ;;
    *) echo "unknown architecture: $ARCH" >&2; exit 1 ;;
esac

SUFFIX=""
if [[ "$SYSTEM_QT" == "systemqt" ]]; then
    SUFFIX="-system-qt"
fi
SOURCE="linux-$ARCH$SUFFIX"
[[ -d "$SOURCE" ]] || { echo "missing deployment directory: $SOURCE" >&2; exit 1; }

STAGE="$PWD/rpm-stage-$ARCH$SUFFIX"
rm -rf "$STAGE"
mkdir -p "$STAGE/opt" "$STAGE/usr/share/applications" "$STAGE/usr/share/pixmaps"
cp -r "$SOURCE" "$STAGE/opt/Throned"
rm -f "$STAGE/opt/Throned/Throned.debug"
cp "$STAGE/opt/Throned/Throned.png" "$STAGE/usr/share/pixmaps/Throned.png"

cat >"$STAGE/usr/share/applications/Throned.desktop" <<-EOF
[Desktop Entry]
Name=Throned
Comment=Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
Exec=sh -c "PATH=/opt/Throned:\$PATH /opt/Throned/Throned -appdata"
Icon=/opt/Throned/Throned.png
Terminal=false
Type=Application
Categories=Network;Application;
EOF

POST_INSTALL="$PWD/rpm-postinst.sh"
cat >"$POST_INSTALL" <<-EOF
#!/bin/sh
update-desktop-database &>/dev/null || :
EOF
chmod +x "$POST_INSTALL"

DEP_ARGS=("-d" "desktop-file-utils")
if [[ "$SYSTEM_QT" == "systemqt" ]]; then
    DEP_ARGS+=("-d" "qt6-qtbase-gui" "-d" "qt6-qtsvg" "-d" "qt6-qtwayland" "-d" "xcb-util-cursor" "-d" "google-noto-emoji-color-fonts")
fi

fpm -s dir -t rpm \
    --verbose \
    -n throned \
    -v "$VERSION" \
    -a "$RPM_ARCH" \
    --license "GPL-3.0-or-later" \
    --url "https://github.com/troshkindm/throned" \
    --description "Qt based cross-platform GUI proxy configuration manager (backend: sing-box)" \
    --no-rpm-autoreqprov \
    --rpm-rpmbuild-define "_build_id_links none" \
    --rpm-rpmbuild-define "__strip /bin/true" \
    --rpm-rpmbuild-define "__brp_strip %{nil}" \
    --rpm-rpmbuild-define "__brp_strip_comment_note %{nil}" \
    --rpm-rpmbuild-define "__brp_strip_static_archive %{nil}" \
    --rpm-rpmbuild-define "__brp_check_rpaths %{nil}" \
    "${DEP_ARGS[@]}" \
    --after-install "$POST_INSTALL" \
    --after-remove "$POST_INSTALL" \
    --force \
    -p ./Throned.rpm \
    -C "$STAGE" \
    opt usr

rm -rf "$STAGE" "$POST_INSTALL"
