#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream muffin window manager for Cinnamon.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="5.6.4"
SHA256="1963562c1e0cdbefa8c5c2642d294efd799c302b730bbe2613869c4f2d4b300c"
URL="https://deb.debian.org/debian/pool/main/m/muffin/muffin_5.6.4.orig.tar.gz"
WORKROOT="${AKAOS_MUFFIN_WORKROOT:-$HOME/.cache/akaos/muffin}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
HOST_TOOLS="$HOME/.cache/akaos/host-glib-tools"

for command_name in curl sha256sum tar meson ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream muffin build tool: $command_name" >&2
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

for required_package in gtk+-3.0 glib-2.0 gio-2.0 cinnamon-desktop json-glib-1.0 x11 xcomposite xdamage xfixes xrender xcursor xinerama xrandr sm ice xkbcommon xkbcommon-x11 xkbfile; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream muffin needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/muffin-$VERSION.tar.gz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[muffin] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/muffin-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[muffin] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/muffin-$VERSION"
if [[ ! -x "$STAGE_PREFIX/bin/muffin" ]]; then
    echo "[muffin] configuring muffin $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"

    export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
    export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -D__unix__ -DUSE_X11 -Wno-error=void-pointer-to-enum-cast -Wno-error=int-conversion -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
    export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

    meson setup         --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"         --prefix=/usr/local --libdir=lib --includedir=include         -Ddefault_library=shared -Dbuildtype=release -Doptimization=2         -Dudev=false         -Dlibwacom=false         -Dremote_desktop=false         -Dstartup_notification=false         -Dsm=true         -Dintrospection=false         -Dtests=false         -Dprofiler=false         -Ddefault_driver=nop         "$BUILD_DIR_PATH" "$SOURCE_DIR" >"$LOG_DIR/muffin-configure.log" 2>&1 || {
        echo "[muffin] configure failed" >&2
        tail -30 "$LOG_DIR/muffin-configure.log" >&2
        exit 1
    }

    echo "[muffin] building muffin binary and libraries"
    ninja -C "$BUILD_DIR_PATH" src/muffin >"$LOG_DIR/muffin-build.log" 2>&1 || {
        echo "[muffin] build failed" >&2
        tail -30 "$LOG_DIR/muffin-build.log" >&2
        exit 1
    }

    echo "[muffin] relinking muffin with akaOS dynamic runtime"
    clang --target=x86_64-unknown-none-elf         -nostdlib -fuse-ld=lld         -Wl,--allow-shlib-undefined -Wl,--as-needed         -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000         -Wl,--dynamic-linker,/lib/ld-akaos.so         -Wl,-T,"$ROOT_DIR/userspace/linker/user-dynamic.ld"         "$BUILD_DIR/user-dynamic-crt0.o"         "$BUILD_DIR_PATH/src/muffin.p/meson-generated_.._meta-dbus-display-config.c.o"         "$BUILD_DIR_PATH/src/muffin.p/meson-generated_.._meta-dbus-idle-monitor.c.o"         "$BUILD_DIR_PATH/src/muffin.p/meson-generated_.._meta-marshal.c.o"         "$BUILD_DIR_PATH/src/muffin.p/meson-generated_.._meta_meta-enum-types.c.o"         "$BUILD_DIR_PATH/src/muffin.p/core_mutter.c.o"         -L"$BUILD_DIR_PATH/src" -lmuffin         -L"$BUILD_DIR_PATH/cogl/cogl" -lmuffin-cogl-0         -L"$BUILD_DIR_PATH/clutter/clutter" -lmuffin-clutter-0         -L"$GRAPHICS_SYSROOT/usr/local/lib" -L"$GRAPHICS_SYSROOT/usr/lib" -L"$BUILD_DIR"         -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lcairo-gobject -lgdk_pixbuf-2.0 -latk-1.0 -lpango-1.0 -lharfbuzz -lcairo         -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lgmodule-2.0 -lintl -lcanberra -ljson-glib-1.0 -lcinnamon-desktop         -lX11 -lXext -lXrandr -lXinerama -lXcursor -lXrender -lXcomposite -lXdamage -lXfixes -lXtst -lXi -lX11-xcb -lxcb -lxcb-randr -lxcb-res -lXau         -lxkbcommon -lxkbcommon-x11 -lxkbfile -lSM -lICE         -Wl,-l:libm.so.6 -Wl,-l:libc.so.6         -o "$BUILD_DIR_PATH/src/muffin"

    # Stage files
    mkdir -p "$STAGE_PREFIX/bin" "$STAGE_PREFIX/lib" "$STAGE_PREFIX/share/glib-2.0/schemas"
    cp -a "$BUILD_DIR_PATH/src/muffin" "$STAGE_PREFIX/bin/muffin"
    cp -a "$BUILD_DIR_PATH/src/libmuffin.so"* "$STAGE_PREFIX/lib/"
    cp -a "$BUILD_DIR_PATH/cogl/cogl/libmuffin-cogl-0.so"* "$STAGE_PREFIX/lib/"
    cp -a "$BUILD_DIR_PATH/clutter/clutter/libmuffin-clutter-0.so"* "$STAGE_PREFIX/lib/"
    if [[ -d "$BUILD_DIR_PATH/cogl/cogl-pango" ]]; then
        cp -a "$BUILD_DIR_PATH/cogl/cogl-pango/libmuffin-cogl-pango-0.so"* "$STAGE_PREFIX/lib/" 2>/dev/null || true
    fi
    if [[ -d "$BUILD_DIR_PATH/cogl/cogl-path" ]]; then
        cp -a "$BUILD_DIR_PATH/cogl/cogl-path/libmuffin-cogl-path-0.so"* "$STAGE_PREFIX/lib/" 2>/dev/null || true
    fi
    cp -a "$SOURCE_DIR/data/"*.xml "$STAGE_PREFIX/share/glib-2.0/schemas/" 2>/dev/null || true

    if command -v glib-compile-schemas >/dev/null 2>&1; then
        glib-compile-schemas "$STAGE_PREFIX/share/glib-2.0/schemas" 2>/dev/null || true
    fi
fi

if [[ ! -x "$STAGE_PREFIX/bin/muffin" ]]; then
    echo "[muffin] failed to produce muffin binary" >&2
    exit 1
fi

if ! llvm-readelf -l "$STAGE_PREFIX/bin/muffin" 2>/dev/null | grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[muffin] binary does not use the akaOS dynamic linker" >&2
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

echo "[DONE] Upstream muffin $VERSION built and published to graphics sysroot"
