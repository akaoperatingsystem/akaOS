#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream mate-desktop library for MATE.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="1.26.2"
SHA256="f6edd2ba096c6971833296de70bdd9ca487981649b5fd3075e294adca0de1193"
URL="https://deb.debian.org/debian/pool/main/m/mate-desktop/mate-desktop_1.26.2.orig.tar.xz"
WORKROOT="${AKAOS_MATE_DESKTOP_WORKROOT:-$HOME/.cache/akaos/mate-desktop}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar meson ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream mate-desktop build tool: $command_name" >&2
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

for required_package in gtk+-3.0 glib-2.0 dconf iso-codes x11 xrandr; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream mate-desktop needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/mate-desktop-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[mate-desktop] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/mate-desktop-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[mate-desktop] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/mate-desktop-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libmate-desktop-2.so" ]]; then
    echo "[mate-desktop] configuring mate-desktop $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"

    export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
    export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
    export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

    meson setup         --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"         --prefix=/usr/local --libdir=lib --includedir=include         -Ddefault_library=shared -Dbuildtype=release -Doptimization=2         -Dgtk-doc=false         -Dintrospection=false         -Dstartup-notification=false         -Dmate-about=false         "$BUILD_DIR_PATH" "$SOURCE_DIR" >"$LOG_DIR/mate-desktop-configure.log" 2>&1 || {
        echo "[mate-desktop] configure failed" >&2
        tail -30 "$LOG_DIR/mate-desktop-configure.log" >&2
        exit 1
    }

    echo "[mate-desktop] building mate-desktop $VERSION"
    ninja -C "$BUILD_DIR_PATH" >"$LOG_DIR/mate-desktop-build.log" 2>&1 || {
        echo "[mate-desktop] build failed" >&2
        tail -30 "$LOG_DIR/mate-desktop-build.log" >&2
        exit 1
    }

    echo "[mate-desktop] relinking mate-color-select with akaOS dynamic runtime"
    clang --target=x86_64-unknown-none-elf         -nostdlib -fuse-ld=lld         -Wl,--allow-shlib-undefined -Wl,--as-needed         -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000         -Wl,--dynamic-linker,/lib/ld-akaos.so         -Wl,-T,"$ROOT_DIR/userspace/linker/user-dynamic.ld"         "$BUILD_DIR/user-dynamic-crt0.o"         "$BUILD_DIR_PATH/tools/mate-color-select.p/mate-color-select.c.o"         -L"$BUILD_DIR_PATH/libmate-desktop" -lmate-desktop-2         -L"$GRAPHICS_SYSROOT/usr/local/lib" -L"$GRAPHICS_SYSROOT/usr/lib" -L"$BUILD_DIR"         -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lcairo-gobject -lgdk_pixbuf-2.0 -latk-1.0 -lpango-1.0 -lharfbuzz -lcairo         -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl         -Wl,-l:libm.so.6 -Wl,-l:libc.so.6         -o "$BUILD_DIR_PATH/tools/mate-color-select"

    DESTDIR="$STAGE" ninja -C "$BUILD_DIR_PATH" install >"$LOG_DIR/mate-desktop-install.log" 2>&1 || {
        echo "[mate-desktop] install failed" >&2
        tail -30 "$LOG_DIR/mate-desktop-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE_PREFIX/lib/libmate-desktop-2.so" ]]; then
    echo "[mate-desktop] failed to produce libmate-desktop-2.so" >&2
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

echo "[DONE] Upstream mate-desktop $VERSION built and published to graphics sysroot"
