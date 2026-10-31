#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream libxfce4util library for XFCE.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="4.18.1"
SHA256="8a52063a5adc66252238cad9ee6997909b59983ed21c77eb83c5e67829d1b01f"
URL="https://deb.debian.org/debian/pool/main/libx/libxfce4util/libxfce4util_4.18.1.orig.tar.bz2"
WORKROOT="${AKAOS_LIBXFCE4UTIL_WORKROOT:-$HOME/.cache/akaos/libxfce4util}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream libxfce4util build tool: $command_name" >&2
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
for required_package in glib-2.0; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream libxfce4util needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/libxfce4util-$VERSION.tar.bz2"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[libxfce4util] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/libxfce4util-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[libxfce4util] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/libxfce4util-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libxfce4util.so" ]]; then
    echo "[libxfce4util] configuring libxfce4util $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --disable-static                 --enable-shared                 --disable-introspection                 --disable-vala
    ) >"$LOG_DIR/libxfce4util-configure.log" 2>&1 || {
        echo "[libxfce4util] configure failed" >&2
        tail -30 "$LOG_DIR/libxfce4util-configure.log" >&2
        exit 1
    }

    echo "[libxfce4util] building libxfce4util $VERSION"
    make -C "$BUILD_DIR_PATH" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >"$LOG_DIR/libxfce4util-build.log" 2>&1 || {
        echo "[libxfce4util] build failed" >&2
        tail -30 "$LOG_DIR/libxfce4util-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/libxfce4util-install.log" 2>&1 || {
        echo "[libxfce4util] install failed" >&2
        tail -30 "$LOG_DIR/libxfce4util-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE_PREFIX/lib/libxfce4util.so" ]]; then
    echo "[libxfce4util] failed to produce libxfce4util.so" >&2
    exit 1
fi

# Clean up libtool .la files to prevent hardcoded host library paths
find "$STAGE_PREFIX" -name "*.la" -delete

# Publish to graphics sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[DONE] Upstream libxfce4util $VERSION built and published to graphics sysroot"
