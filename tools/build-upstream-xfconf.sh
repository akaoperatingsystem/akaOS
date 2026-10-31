#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream xfconf configuration daemon and library for XFCE.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="4.18.0"
SHA256="2e8c50160bf800a807aea094fc9dad81f9f361f42db56607508ed5b4855d2906"
URL="https://deb.debian.org/debian/pool/main/x/xfconf/xfconf_4.18.0.orig.tar.bz2"
WORKROOT="${AKAOS_XFCONF_WORKROOT:-$HOME/.cache/akaos/xfconf}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
HOST_TOOLS="$HOME/.cache/akaos/host-glib-tools"

for command_name in curl sha256sum tar clang pkg-config make python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream xfconf build tool: $command_name" >&2
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
for required_package in glib-2.0 gio-2.0 libxfce4util-1.0; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream xfconf needs the $required_package package" >&2
        exit 2
    fi
done

# Ensure host-side glib tools (gdbus-codegen and glib-genmarshal) are available in PATH
if [[ ! -x "$HOST_TOOLS/usr/bin/glib-genmarshal" || ! -x "$HOST_TOOLS/usr/bin/gdbus-codegen" ]]; then
    mkdir -p "$HOST_TOOLS"
    if [[ ! -f "/tmp/glib-dev-bin-deb12.deb" ]]; then
        curl -fsSL "https://deb.debian.org/debian/pool/main/g/glib2.0/libglib2.0-dev-bin_2.74.6-2+deb12u9_amd64.deb" -o "/tmp/glib-dev-bin-deb12.deb"
    fi
    dpkg-deb -x /tmp/glib-dev-bin-deb12.deb "$HOST_TOOLS"
fi
export PATH="$HOST_TOOLS/usr/bin:$PATH"

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/xfconf-$VERSION.tar.bz2"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[xfconf] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/xfconf-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[xfconf] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/xfconf-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libxfconf-0.so" ]]; then
    echo "[xfconf] configuring xfconf $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --disable-static                 --enable-shared                 --disable-introspection                 --disable-vala                 --disable-gsettings-backend
    ) >"$LOG_DIR/xfconf-configure.log" 2>&1 || {
        echo "[xfconf] configure failed" >&2
        tail -30 "$LOG_DIR/xfconf-configure.log" >&2
        exit 1
    }

    echo "[xfconf] building xfconf $VERSION"
    glib-genmarshal --prefix=_xfconf_marshal "$SOURCE_DIR/common/xfconf-marshal.list" --header > "$BUILD_DIR_PATH/common/xfconf-marshal.h"
    echo '#include "xfconf-marshal.h"' > "$BUILD_DIR_PATH/common/xfconf-marshal.c"
    glib-genmarshal --prefix=_xfconf_marshal "$SOURCE_DIR/common/xfconf-marshal.list" --body >> "$BUILD_DIR_PATH/common/xfconf-marshal.c"

    make -C "$BUILD_DIR_PATH" -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
        >"$LOG_DIR/xfconf-build.log" 2>&1 || {
        echo "[xfconf] build failed" >&2
        tail -30 "$LOG_DIR/xfconf-build.log" >&2
        exit 1
    }

    # Re-link binaries with the akaOS dynamic linker and startup object
    rm -f "$BUILD_DIR_PATH/xfconf-query/xfconf-query" "$BUILD_DIR_PATH/xfconf-query/.libs/xfconf-query"           "$BUILD_DIR_PATH/xfconfd/xfconfd" "$BUILD_DIR_PATH/xfconfd/.libs/xfconfd"

    make -C "$BUILD_DIR_PATH/xfconf-query"         xfconf_query_LDFLAGS="-pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/xfconf-build.log" 2>&1

    make -C "$BUILD_DIR_PATH/xfconfd"         xfconfd_LDFLAGS="-pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/xfconf-build.log" 2>&1

    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/xfconf-install.log" 2>&1 || {
        echo "[xfconf] install failed" >&2
        tail -30 "$LOG_DIR/xfconf-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE_PREFIX/lib/libxfconf-0.so" ]]; then
    echo "[xfconf] failed to produce libxfconf-0.so" >&2
    exit 1
fi

# Clean up libtool .la files to prevent hardcoded host library paths
find "$STAGE_PREFIX" -name "*.la" -delete

# Publish to graphics sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[DONE] Upstream xfconf $VERSION built and published to graphics sysroot"
