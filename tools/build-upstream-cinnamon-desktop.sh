#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream cinnamon-desktop for Cinnamon.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="5.6.1"
SHA256="5c86407f35e21ab20f6d077de615d82a10ae74f438ffb2d1a0d817560de98e1f"
URL="https://deb.debian.org/debian/pool/main/c/cinnamon-desktop/cinnamon-desktop_5.6.1.orig.tar.gz"
WORKROOT="${AKAOS_CINNAMON_DESKTOP_WORKROOT:-$HOME/.cache/akaos/cinnamon-desktop}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar meson ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream cinnamon-desktop build tool: $command_name" >&2
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

for required_package in gtk+-3.0 glib-2.0 gio-2.0 x11 xrandr xkbfile xkeyboard-config; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream cinnamon-desktop needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/cinnamon-desktop-$VERSION.tar.gz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[cinnamon-desktop] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/cinnamon-desktop-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[cinnamon-desktop] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/cinnamon-desktop-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libcinnamon-desktop.so" ]]; then
    echo "[cinnamon-desktop] configuring cinnamon-desktop $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"

    export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
    export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
    export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

    meson setup         --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"         --prefix=/usr/local --libdir=lib --includedir=include         -Ddefault_library=shared -Dbuildtype=release -Doptimization=2         -Ddeprecation_warnings=false         -Dalsa=false         "$BUILD_DIR_PATH" "$SOURCE_DIR" >"$LOG_DIR/cinnamon-desktop-configure.log" 2>&1 || {
        echo "[cinnamon-desktop] configure failed" >&2
        tail -30 "$LOG_DIR/cinnamon-desktop-configure.log" >&2
        exit 1
    }

    echo "[cinnamon-desktop] building shared libraries"
    ninja -C "$BUILD_DIR_PATH" libcinnamon-desktop/libcinnamon-desktop.so.4.0.0 libcvc/libcvc.so.0.0.0 >"$LOG_DIR/cinnamon-desktop-build.log" 2>&1 || {
        echo "[cinnamon-desktop] build failed" >&2
        tail -30 "$LOG_DIR/cinnamon-desktop-build.log" >&2
        exit 1
    }

    mkdir -p "$STAGE_PREFIX/lib/pkgconfig" "$STAGE_PREFIX/include/cinnamon-desktop" "$STAGE_PREFIX/include/cvc" "$STAGE_PREFIX/share/glib-2.0/schemas"
    cp -a "$BUILD_DIR_PATH/libcinnamon-desktop/libcinnamon-desktop.so"* "$STAGE_PREFIX/lib/"
    cp -a "$BUILD_DIR_PATH/libcvc/libcvc.so"* "$STAGE_PREFIX/lib/"
    cp -a "$SOURCE_DIR/libcinnamon-desktop/"*.h "$STAGE_PREFIX/include/cinnamon-desktop/"
    cp -a "$SOURCE_DIR/libcvc/"*.h "$STAGE_PREFIX/include/cvc/"
    ln -sf cinnamon-desktop "$STAGE_PREFIX/include/libcinnamon-desktop"
    ln -sf . "$STAGE_PREFIX/include/cinnamon-desktop/libcinnamon-desktop"
    cp -a "$BUILD_DIR_PATH/meson-private/cinnamon-desktop.pc" "$STAGE_PREFIX/lib/pkgconfig/"
    cp -a "$BUILD_DIR_PATH/meson-private/cvc.pc" "$STAGE_PREFIX/lib/pkgconfig/"
    cp -a "$BUILD_DIR_PATH/schemas/"*.xml "$STAGE_PREFIX/share/glib-2.0/schemas/"
    if [[ -f "$BUILD_DIR_PATH/libcinnamon-desktop/org.cinnamon.desktop.enums.xml" ]]; then
        cp -a "$BUILD_DIR_PATH/libcinnamon-desktop/org.cinnamon.desktop.enums.xml" "$STAGE_PREFIX/share/glib-2.0/schemas/"
    fi

    if command -v glib-compile-schemas >/dev/null 2>&1; then
        glib-compile-schemas "$STAGE_PREFIX/share/glib-2.0/schemas"
    fi
fi

if [[ ! -f "$STAGE_PREFIX/lib/libcinnamon-desktop.so" ]]; then
    echo "[cinnamon-desktop] failed to produce libcinnamon-desktop.so" >&2
    exit 1
fi

find "$STAGE_PREFIX" -name "*.la" -delete

mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[DONE] Upstream cinnamon-desktop $VERSION built and published to graphics sysroot"
