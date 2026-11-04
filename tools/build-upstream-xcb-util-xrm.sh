#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="1.3"
SHA256="0129f74c327ae65e2f4ad4002f300b4f02c9aff78c00997f1f1c5a430f922f34"
URL="https://github.com/Airblader/xcb-util-xrm/releases/download/v$VERSION/xcb-util-xrm-$VERSION.tar.gz"
WORKROOT="${AKAOS_XCB_XRM_WORKROOT:-$HOME/.cache/akaos/xcb-util-xrm}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

ARCHIVE="$SRC_DIR/xcb-util-xrm-$VERSION.tar.gz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[xcb-util-xrm] fetching xcb-util-xrm $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[xcb-util-xrm] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi

actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[xcb-util-xrm] checksum mismatch" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/xcb-util-xrm-$VERSION"
if [[ ! -f "$SOURCE_DIR/.akaos-extracted" ]]; then
    echo "[xcb-util-xrm] extracting xcb-util-xrm $VERSION"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
    tar -xf "$ARCHIVE" -C "$SOURCE_DIR" --strip-components=1
    touch "$SOURCE_DIR/.akaos-extracted"
fi

BUILD_DIR_PATH="$BUILD_ROOT/xcb-util-xrm-$VERSION"
if [[ -f "$STAGE_PREFIX/lib/libxcb-xrm.so" ]]; then
    echo "[xcb-util-xrm] already staged"
else
    echo "[xcb-util-xrm] configuring xcb-util-xrm $VERSION"
    rm -rf "$BUILD_DIR_PATH"
    mkdir -p "$BUILD_DIR_PATH"
    (
        cd "$BUILD_DIR_PATH" || exit 1
        env \
            CC="$CC_WRAPPER" \
            CFLAGS="-O2 -g0" \
            CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include" \
            LDFLAGS="-nostdlib -fuse-ld=lld -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6" \
            PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig" \
            PKG_CONFIG_SYSROOT_DIR="" \
            "$SOURCE_DIR/configure" \
                --host=x86_64-unknown-linux-gnu \
                --prefix="$STAGE_PREFIX" \
                --disable-static \
                --enable-shared
    ) >"$LOG_DIR/xcb-xrm-configure.log" 2>&1 || {
        echo "[xcb-util-xrm] configure failed; see $LOG_DIR/xcb-xrm-configure.log" >&2
        tail -40 "$LOG_DIR/xcb-xrm-configure.log" >&2
        exit 1
    }

    echo "[xcb-util-xrm] building xcb-util-xrm"
    make -C "$BUILD_DIR_PATH" -j"$(nproc)" >"$LOG_DIR/xcb-xrm-build.log" 2>&1 || {
        echo "[xcb-util-xrm] build failed; see $LOG_DIR/xcb-xrm-build.log" >&2
        tail -40 "$LOG_DIR/xcb-xrm-build.log" >&2
        exit 1
    }

    echo "[xcb-util-xrm] installing xcb-util-xrm"
    make -C "$BUILD_DIR_PATH" install >"$LOG_DIR/xcb-xrm-install.log" 2>&1 || {
        echo "[xcb-util-xrm] install failed; see $LOG_DIR/xcb-xrm-install.log" >&2
        tail -40 "$LOG_DIR/xcb-xrm-install.log" >&2
        exit 1
    }
fi

# Publish to sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[xcb-util-xrm] DONE"
