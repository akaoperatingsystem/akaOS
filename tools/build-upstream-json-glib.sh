#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream json-glib for akaOS.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="1.6.6"
SHA256="3c48f0d52b4c39fbfa440bfc729d9972cf02036b63f9c64cf5e880c46d9fa2c4"
URL="https://deb.debian.org/debian/pool/main/j/json-glib/json-glib_1.6.6.orig.tar.xz"
WORKROOT="${AKAOS_JSON_GLIB_WORKROOT:-$HOME/.cache/akaos/json-glib}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar meson ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream json-glib build tool: $command_name" >&2
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

for required_package in glib-2.0 gio-2.0; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream json-glib needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/json-glib-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[json-glib] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/json-glib-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[json-glib] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/json-glib-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libjson-glib-1.0.so" ]]; then
    echo "[json-glib] configuring json-glib $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"

    export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
    export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
    export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

    meson setup         --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"         --prefix=/usr/local --libdir=lib --includedir=include         -Ddefault_library=shared -Dbuildtype=release -Doptimization=2         -Dintrospection=disabled         -Dgtk_doc=disabled         -Dtests=false         -Dman=false         "$BUILD_DIR_PATH" "$SOURCE_DIR" >"$LOG_DIR/json-glib-configure.log" 2>&1 || {
        echo "[json-glib] configure failed" >&2
        tail -30 "$LOG_DIR/json-glib-configure.log" >&2
        exit 1
    }

    echo "[json-glib] building json-glib $VERSION"
    ninja -C "$BUILD_DIR_PATH" >"$LOG_DIR/json-glib-build.log" 2>&1 || {
        echo "[json-glib] build failed" >&2
        tail -30 "$LOG_DIR/json-glib-build.log" >&2
        exit 1
    }

    DESTDIR="$STAGE" ninja -C "$BUILD_DIR_PATH" install >"$LOG_DIR/json-glib-install.log" 2>&1 || {
        echo "[json-glib] install failed" >&2
        tail -30 "$LOG_DIR/json-glib-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE_PREFIX/lib/libjson-glib-1.0.so" ]]; then
    echo "[json-glib] failed to produce libjson-glib-1.0.so" >&2
    exit 1
fi

find "$STAGE_PREFIX" -name "*.la" -delete

mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[DONE] Upstream json-glib $VERSION built and published to graphics sysroot"
