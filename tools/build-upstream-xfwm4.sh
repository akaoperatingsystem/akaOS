#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream xfwm4 window manager for XFCE.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="4.18.0"
SHA256="92cd1b889bb25cb4bc06c1c6736c238d96e79c1e706b9f77fad0a89d6e5fc13f"
URL="https://deb.debian.org/debian/pool/main/x/xfwm4/xfwm4_4.18.0.orig.tar.bz2"
WORKROOT="${AKAOS_XFWM4_WORKROOT:-$HOME/.cache/akaos/xfwm4}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
HOST_TOOLS="$HOME/.cache/akaos/host-glib-tools"

for command_name in curl sha256sum tar clang pkg-config make python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream xfwm4 build tool: $command_name" >&2
        exit 2
    fi
done

if [[ ! -x "$CC_WRAPPER" ]]; then
    echo "Missing cross compiler wrapper: $CC_WRAPPER" >&2
    exit 2
fi
if [[ ! -f "$BUILD_DIR/libc.so.6" || ! -f "$BUILD_DIR/libm.so.6" ]]; then
    echo "Missing shared akaOS libc in $BUILD_DIR; run build.sh first" >&2
    exit 2
fi
if [[ ! -f "$BUILD_DIR/user-dynamic-crt0.o" ]]; then
    echo "Missing the akaOS startup object in $BUILD_DIR; run build.sh first" >&2
    exit 2
fi
for required_package in gtk+-3.0 glib-2.0 libxfce4util-1.0 libxfconf-0 libxfce4ui-2 libwnck-3.0 x11 xinerama; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream xfwm4 needs the $required_package package" >&2
        exit 2
    fi
done

if [[ -d "$HOST_TOOLS/usr/bin" ]]; then
    export PATH="$HOST_TOOLS/usr/bin:$PATH"
fi

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/xfwm4-$VERSION.tar.bz2"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[xfwm4] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/xfwm4-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[xfwm4] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/xfwm4-$VERSION"
if [[ ! -x "$STAGE_PREFIX/bin/xfwm4" ]]; then
    echo "[xfwm4] configuring xfwm4 $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --disable-static                 --enable-shared                 --disable-compositor
    ) >"$LOG_DIR/xfwm4-configure.log" 2>&1 || {
        echo "[xfwm4] configure failed" >&2
        tail -30 "$LOG_DIR/xfwm4-configure.log" >&2
        exit 1
    }

    echo "[xfwm4] building common library and binary"
    make -C "$BUILD_DIR_PATH/common" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >"$LOG_DIR/xfwm4-build.log" 2>&1 || {
        echo "[xfwm4] build common failed" >&2
        tail -30 "$LOG_DIR/xfwm4-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/src" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/xfwm4-build.log" 2>&1 || {
        echo "[xfwm4] build src failed" >&2
        tail -30 "$LOG_DIR/xfwm4-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/xfwm4-install.log" 2>&1 || {
        echo "[xfwm4] install failed" >&2
        tail -30 "$LOG_DIR/xfwm4-install.log" >&2
        exit 1
    }
fi

if [[ ! -x "$STAGE_PREFIX/bin/xfwm4" ]]; then
    echo "[xfwm4] failed to produce xfwm4" >&2
    exit 1
fi

if ! llvm-readelf -l "$STAGE_PREFIX/bin/xfwm4" 2>/dev/null | grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[xfwm4] binary does not use the akaOS dynamic linker" >&2
    exit 1
fi

# Publish to graphics sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin share lib; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
if [[ -d "$STAGE/etc" ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc"
    cp -a "$STAGE/etc/." "$GRAPHICS_SYSROOT/etc/"
fi

echo "[DONE] Upstream xfwm4 $VERSION built and published to graphics sysroot"
