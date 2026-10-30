#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream LXDE menu data package.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="0.1.5"
SHA256="9fe3218d2ef50b91190162f4f923d6524c364849f87bcda8b4ed8eb59b80bab8"
URL="https://deb.debian.org/debian/pool/main/l/lxmenu-data/lxmenu-data_0.1.5.orig.tar.xz"
WORKROOT="${AKAOS_LXMENU_DATA_WORKROOT:-$HOME/.cache/akaos/lxmenu-data}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"

for command_name in curl sha256sum tar intltool-merge make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream lxmenu-data build tool: $command_name" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/lxmenu-data-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[lxmenu-data] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/lxmenu-data-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[lxmenu-data] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/lxmenu-data-$VERSION"
if [[ ! -f "$STAGE/etc/xdg/menus/lxde-applications.menu" ]]; then
    echo "[lxmenu-data] configuring lxmenu-data $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        "$SOURCE_DIR/configure"             --prefix=/usr/local             --sysconfdir=/etc
    ) >"$LOG_DIR/lxmenu-data-configure.log" 2>&1 || {
        echo "[lxmenu-data] configure failed" >&2
        tail -30 "$LOG_DIR/lxmenu-data-configure.log" >&2
        exit 1
    }

    echo "[lxmenu-data] building lxmenu-data $VERSION"
    make -C "$BUILD_DIR_PATH" -j1 >"$LOG_DIR/lxmenu-data-build.log" 2>&1 || {
        echo "[lxmenu-data] build failed" >&2
        tail -30 "$LOG_DIR/lxmenu-data-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/lxmenu-data-install.log" 2>&1 || {
        echo "[lxmenu-data] install failed" >&2
        tail -30 "$LOG_DIR/lxmenu-data-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE/etc/xdg/menus/lxde-applications.menu" ]]; then
    echo "[lxmenu-data] failed to produce lxde-applications.menu" >&2
    exit 1
fi

# Publish to graphics sysroot
mkdir -p "$GRAPHICS_SYSROOT/etc/xdg/menus" "$GRAPHICS_SYSROOT/usr/local/share/desktop-directories"
cp -a "$STAGE/etc/xdg/menus/." "$GRAPHICS_SYSROOT/etc/xdg/menus/"
cp -a "$STAGE/usr/local/share/desktop-directories/." "$GRAPHICS_SYSROOT/usr/local/share/desktop-directories/"

echo "[DONE] Upstream LXDE menu data $VERSION published to graphics sysroot"
