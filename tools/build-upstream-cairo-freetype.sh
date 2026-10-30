#!/usr/bin/env bash
set -euo pipefail
# The build host can carry include and pkg-config search paths for an unrelated
# toolchain.  Clang honours CPATH even under -nostdinc and pkg-config prefers
# PKG_CONFIG_PATH over PKG_CONFIG_LIBDIR, so both are cleared to keep the cross
# build self contained.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
SOURCE_TREE="$BUILD_DIR/upstream-source/cairo-1.18.6"
BUILD_TREE="$BUILD_DIR/upstream-build/cairo-1.18.6/x86_64-akaos-freetype"
PROJECT_SOURCE_TREE="$SOURCE_TREE"
PROJECT_BUILD_TREE="$BUILD_TREE"
PIXMAN_BUILD="$BUILD_DIR/upstream-build/pixman-0.46.4/x86_64-akaos"

# The WSL DrvFS mount is very slow for repeated compiler runs, so the build
# accepts an ext4 work root that holds the source and build trees.
if [[ -n "${AKAOS_CAIRO_WORKROOT:-}" ]]; then
    SOURCE_TREE="$AKAOS_CAIRO_WORKROOT/source"
    BUILD_TREE="$AKAOS_CAIRO_WORKROOT/build"
    mkdir -p "$AKAOS_CAIRO_WORKROOT"
    if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
        mkdir -p "$SOURCE_TREE"
        cp -a "$PROJECT_SOURCE_TREE/." "$SOURCE_TREE/"
    fi
fi

for command_name in clang meson ninja pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld meson ninja-build pkg-config" >&2
        exit 2
    fi
done

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$PIXMAN_BUILD/pixman/libpixman-1.so.0.46.4" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libfontconfig.so.1.17.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libfreetype.so.6.20.6" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libX11.so.6" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libXext.so.6" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libXrender.so.1"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing upstream Cairo/Fontconfig prerequisite: $required_file" >&2
        echo "Build the upstream X11 client stack first." >&2
        exit 2
    fi
done

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"

# The Xlib surface is required by the GTK 3 X11 backend, so the graphics
# sysroot copy of Cairo carries it too.  Cairo includes the System V shared
# memory headers unconditionally even though it no longer calls the shared
# memory API, so the build provides empty headers for Cairo alone.
CAIRO_STUB_INCLUDE="$BUILD_DIR/cairo-x11-stub-include"
mkdir -p "$CAIRO_STUB_INCLUDE/sys"
cat > "$CAIRO_STUB_INCLUDE/sys/ipc.h" <<'STUB'
#ifndef _AKAOS_SYS_IPC_H_
#define _AKAOS_SYS_IPC_H_
#endif
STUB
cat > "$CAIRO_STUB_INCLUDE/sys/shm.h" <<'STUB'
#ifndef _AKAOS_SYS_SHM_H_
#define _AKAOS_SYS_SHM_H_
#endif
STUB

export CFLAGS="-nostdinc -I/usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2 -I$CAIRO_STUB_INCLUDE -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6 $BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Dtests=disabled
    -Dfontconfig=enabled -Dfreetype=enabled -Dpng=enabled -Dzlib=enabled
    -Dquartz=disabled -Dtee=disabled -Dxcb=disabled -Dxlib=enabled
    -Dxlib-xcb=disabled -Dglib=enabled -Ddwrite=disabled
    -Dspectre=disabled -Dlzo=disabled -Dsymbol-lookup=disabled
    -Dgtk_doc=false
)
LDFLAGS_STAMP="$BUILD_TREE/.akaos-ldflags-stamp"
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" ]]; then
    # Meson records the compiler and linker environment when the build tree is
    # created and does not re-read it on a reconfigure.  A change to the
    # linker flags would therefore keep linking the previous way, so the build
    # tree is recreated when the recorded environment no longer matches.
    if [[ -f "$LDFLAGS_STAMP" ]] && [[ "$(cat "$LDFLAGS_STAMP")" != "$LDFLAGS" ]]; then
        echo "[BUILD] Cairo linker environment changed, recreating the build tree"
        rm -rf "$BUILD_TREE"
    fi
fi
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" ]]; then
    meson setup --reconfigure "${meson_args[@]}" \
        "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
mkdir -p "$BUILD_TREE"
printf '%s' "$LDFLAGS" > "$LDFLAGS_STAMP"
meson compile -C "$BUILD_TREE" --jobs 2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

if ! grep -q '^#define CAIRO_HAS_FC_FONT 1$' "$BUILD_TREE/src/cairo-features.h"; then
    echo "Cairo was built without its Fontconfig font backend" >&2
    exit 1
fi
if [[ ! -f "$BUILD_TREE/util/cairo-gobject/libcairo-gobject.so.2.11806.6" ]]; then
    echo "Cairo was built without the upstream cairo-gobject shared library" >&2
    exit 1
fi
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/include/cairo/cairo-pdf.h" ]]; then
    echo "Cairo PDF surface support was enabled but cairo-pdf.h was not installed" >&2
    exit 1
fi
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/include/cairo/cairo-svg.h" ]]; then
    echo "Cairo SVG surface support was enabled but cairo-svg.h was not installed" >&2
    exit 1
fi
if [[ ! -f "$GRAPHICS_SYSROOT/usr/lib/libz.so.1.3.1" ]]; then
    echo "Cairo's enabled zlib backend is missing libz.so.1 from the graphics sysroot" >&2
    exit 1
fi
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/cairo-xlib.pc" ]]; then
    echo "Cairo was built without its Xlib surface backend" >&2
    exit 1
fi
if ! grep -q 'cairo_xlib_surface_create' \
    "$GRAPHICS_SYSROOT/usr/local/lib/libcairo.so.2.11806.6" 2>/dev/null; then
    echo "The installed Cairo does not export the Xlib surface API" >&2
    exit 1
fi

echo "[DONE] Upstream Cairo 1.18.6 Fontconfig/FreeType/GObject/Xlib build: $BUILD_TREE/src/libcairo.so.2.11806.6"
