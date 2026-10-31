#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream lxde-common package.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="0.99.2"
SHA256="1cd9bc900960c995d48ffbbdc86ecffda7c806168c67aaa50c53113794234856"
URL="https://deb.debian.org/debian/pool/main/l/lxde-common/lxde-common_0.99.2.orig.tar.xz"
WORKROOT="${AKAOS_LXDE_COMMON_WORKROOT:-$HOME/.cache/akaos/lxde-common}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"

for command_name in curl sha256sum tar make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream lxde-common build tool: $command_name" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/lxde-common-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[lxde-common] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/lxde-common-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[lxde-common] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/lxde-common-$VERSION"
if [[ ! -f "$STAGE/usr/local/bin/startlxde" ]]; then
    echo "[lxde-common] configuring lxde-common $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        "$SOURCE_DIR/configure"             --prefix=/usr/local             --sysconfdir=/etc
    ) >"$LOG_DIR/lxde-common-configure.log" 2>&1 || {
        echo "[lxde-common] configure failed" >&2
        tail -30 "$LOG_DIR/lxde-common-configure.log" >&2
        exit 1
    }

    echo "[lxde-common] building lxde-common $VERSION"
    make -C "$BUILD_DIR_PATH" -j1 >"$LOG_DIR/lxde-common-build.log" 2>&1 || {
        echo "[lxde-common] build failed" >&2
        tail -30 "$LOG_DIR/lxde-common-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/lxde-common-install.log" 2>&1 || {
        echo "[lxde-common] install failed" >&2
        tail -30 "$LOG_DIR/lxde-common-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE/usr/local/bin/startlxde" ]]; then
    echo "[lxde-common] failed to produce startlxde" >&2
    exit 1
fi

# Publish to graphics sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local/bin" "$GRAPHICS_SYSROOT/usr/local/share" "$GRAPHICS_SYSROOT/etc"
for subtree in bin share; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
if [[ -d "$STAGE/etc" ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc"
    cp -a "$STAGE/etc/." "$GRAPHICS_SYSROOT/etc/"
fi

echo "[DONE] Upstream LXDE common $VERSION published to graphics sysroot"
