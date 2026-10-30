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
SOURCE_DIR="$BUILD_DIR/upstream-source/freetype-2.14.3"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/freetype-2.14.3/x86_64-akaos"
SYSROOT="$BUILD_DIR/upstream-sysroot/freetype-2.14.3-x86_64"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
PIXMAN_SOURCE="$BUILD_DIR/upstream-source/pixman-0.46.4"
PIXMAN_BUILD="$BUILD_DIR/upstream-build/pixman-0.46.4/x86_64-akaos"
CAIRO_SOURCE="$BUILD_DIR/upstream-source/cairo-1.18.6"
CAIRO_FREETYPE_BUILD="$BUILD_DIR/upstream-build/cairo-1.18.6/x86_64-akaos-freetype"

for command_name in curl sha256sum tar clang meson ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang meson ninja-build" >&2
        exit 2
    fi
done

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS x86_64 prerequisite: $required_file" >&2
        echo "Build the akaOS x86_64 userspace before FreeType." >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/freetype-2.14.3.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz \
        -o "$SOURCE_DIR/freetype-2.14.3.tar.xz"
fi
printf '%s  %s\n' \
    36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f \
    "$SOURCE_DIR/freetype-2.14.3.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/freetype-2.14.3.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

export CFLAGS="-nostdinc -I/usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,-e,main -Wl,--no-as-needed -L$BUILD_DIR $BUILD_DIR/libc.so.6 $BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a"

if [[ ! -f "$BUILD_TREE/meson-private/coredata.dat" ]]; then
    meson setup --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini" \
        -Ddefault_library=shared -Dbuildtype=release \
        -Dbrotli=disabled -Dbzip2=disabled -Dharfbuzz=disabled \
        -Dmmap=enabled -Dpng=disabled -Dtests=disabled -Dzlib=disabled \
        -Derror_strings=true "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs 2
DESTDIR="$SYSROOT" meson install -C "$BUILD_TREE"
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

# Use one target sysroot for the FreeType-aware Cairo build so pkg-config can
# resolve both its Pixman and FreeType dependencies without host libraries.
if [[ ! -f "$PIXMAN_BUILD/meson-private/coredata.dat" ||
      ! -f "$CAIRO_SOURCE/meson.build" ]]; then
    echo "Build upstream Pixman and unpack upstream Cairo first with tools/build-upstream-cairo.sh" >&2
    exit 2
fi
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$PIXMAN_BUILD"
export CFLAGS="-nostdinc -I/usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$GRAPHICS_SYSROOT/usr/local/include/pixman-1 -I$GRAPHICS_SYSROOT/usr/local/include/freetype2 -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,-e,main -Wl,--no-as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libc.so.6 $BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a"
export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"

if [[ ! -f "$CAIRO_FREETYPE_BUILD/meson-private/coredata.dat" ]]; then
    meson setup --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini" \
        -Ddefault_library=shared -Dbuildtype=release -Dtests=disabled \
        -Dfontconfig=disabled -Dfreetype=enabled -Dpng=disabled \
        -Dzlib=disabled -Dquartz=disabled -Dtee=disabled -Dxcb=disabled \
        -Dxlib=disabled -Dxlib-xcb=disabled -Dglib=disabled \
        -Ddwrite=disabled -Dspectre=disabled -Dlzo=disabled \
        -Dsymbol-lookup=disabled -Dgtk_doc=false \
        "$CAIRO_FREETYPE_BUILD" "$CAIRO_SOURCE"
fi
meson compile -C "$CAIRO_FREETYPE_BUILD" --jobs 2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$CAIRO_FREETYPE_BUILD"

echo "[DONE] Upstream FreeType 2.14.3 x86_64 DSO: $BUILD_TREE/libfreetype.so.6.20.6"
echo "[DONE] FreeType-aware Cairo build: $CAIRO_FREETYPE_BUILD"
echo "Run ./build.sh --arch x86_64 --firmware both --disk-type iso to package them."
