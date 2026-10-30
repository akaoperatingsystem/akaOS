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
SOURCE_DIR="$BUILD_DIR/upstream-source/pango-1.58.2"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/pango-1.58.2/x86_64-akaos"
PROJECT_SOURCE_TREE="$SOURCE_TREE"
PROJECT_BUILD_TREE="$BUILD_TREE"
# The WSL DrvFS mount is very slow for repeated compiler runs, so the build
# accepts an ext4 work root that holds the source and build trees.
if [[ -n "${AKAOS_PANGO_WORKROOT:-}" ]]; then
    SOURCE_TREE="$AKAOS_PANGO_WORKROOT/source"
    BUILD_TREE="$AKAOS_PANGO_WORKROOT/build"
    mkdir -p "$AKAOS_PANGO_WORKROOT"
    if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
        mkdir -p "$SOURCE_TREE"
        cp -a "$PROJECT_SOURCE_TREE/." "$SOURCE_TREE/"
    fi
fi
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
GLIB_BUILD="$BUILD_DIR/upstream-build/glib-2.90.0/x86_64-akaos-v7"
PCRE2_SYSROOT="$BUILD_DIR/upstream-sysroot/pcre2-10.49-x86_64"
CXX_RUNTIME="$BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64"

for command_name in curl sha256sum tar clang clang++ meson ninja llvm-ar pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld llvm meson ninja-build pkg-config" >&2
        exit 2
    fi
done

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$BUILD_DIR/libc++.so.1" "$BUILD_DIR/libc++abi.so.1" \
    "$BUILD_DIR/libunwind.so.1" \
    "$GLIB_BUILD/build.ninja" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libcairo.so.2.11806.6" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libfontconfig.so.1.17.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libfribidi.so.0.4.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libharfbuzz.so.0.61450.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libfreetype.so.6.20.6" \
    "$PCRE2_SYSROOT/usr/lib/libpcre2-8.so.0.16.1"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS x86_64 upstream prerequisite: $required_file" >&2
        echo "Build the x86_64 GLib, Cairo, Fontconfig, FriBidi, HarfBuzz, and FreeType stack first." >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/pango-1.58.2.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://download.gnome.org/sources/pango/1.58/pango-1.58.2.tar.xz \
        -o "$SOURCE_DIR/pango-1.58.2.tar.xz"
fi
printf '%s  %s\n' \
    342385b6ca3b7c73455d7c80a13b7dbe4489e00bc3bd4c5bd6ed4dce421e374a \
    "$SOURCE_DIR/pango-1.58.2.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/pango-1.58.2.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

# Pango's Meson probes need GLib's installed headers/pkg-config metadata in the
# combined target sysroot. Runtime packaging still uses build.sh's sonames.
if [[ ! -f "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" ]]; then
    DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$GLIB_BUILD"
fi
for library in \
    "$GLIB_BUILD/glib/libglib-2.0.so.0.9000.0" \
    "$GLIB_BUILD/gobject/libgobject-2.0.so.0.9000.0" \
    "$GLIB_BUILD/gio/libgio-2.0.so.0.9000.0" \
    "$GLIB_BUILD/gmodule/libgmodule-2.0.so.0.9000.0" \
    "$GLIB_BUILD/gthread/libgthread-2.0.so.0.9000.0" \
    "$GLIB_BUILD/subprojects/libffi-3.5.2/src/libffi.so.8.2.0" \
    "$GLIB_BUILD/subprojects/zlib-1.3.1/libz.so.1.3.1" \
    "$GLIB_BUILD/subprojects/proxy-libintl-0.5/libintl.so.8"; do
    if [[ -f "$library" ]]; then
        install -m 0644 "$library" "$GRAPHICS_SYSROOT/usr/lib/${library##*/}"
    fi
done

# GLib's public pkg-config file names PCRE2 as a private dependency. Make that
# dependency discoverable during Meson configure without using host libraries.
mkdir -p "$GRAPHICS_SYSROOT/usr/lib/pkgconfig" "$GRAPHICS_SYSROOT/usr/include"
install -m 0644 "$PCRE2_SYSROOT/usr/lib/pkgconfig/libpcre2-8.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/libpcre2-8.pc"
install -m 0644 "$PCRE2_SYSROOT/usr/include/pcre2.h" \
    "$GRAPHICS_SYSROOT/usr/include/pcre2.h"
install -m 0644 "$PCRE2_SYSROOT/usr/lib/libpcre2-8.so.0.16.1" \
    "$GRAPHICS_SYSROOT/usr/lib/libpcre2-8.so.0.16.1"
ln -sfn libpcre2-8.so.0.16.1 "$GRAPHICS_SYSROOT/usr/lib/libpcre2-8.so.0"
ln -sfn libpcre2-8.so.0 "$GRAPHICS_SYSROOT/usr/lib/libpcre2-8.so"

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
export CXXFLAGS="-nostdinc -nostdinc++ -I$CXX_RUNTIME/include/c++/v1 -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR -L$CXX_RUNTIME $BUILD_DIR/libc++.so.1 $BUILD_DIR/libc++abi.so.1 $BUILD_DIR/libunwind.so.1 $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6 $CXX_RUNTIME/libakaos-compiler-builtins.a"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Ddocumentation=false -Dman-pages=false -Dintrospection=disabled
    -Dbuild-testsuite=false -Dbuild-examples=false
    -Dfontconfig=enabled -Dfreetype=enabled -Dcairo=enabled
    -Dlibthai=disabled -Dxft=enabled
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" &&
      -f "$BUILD_TREE/build.ninja" &&
      -f "$BUILD_TREE/pango/pango-features.h" ]]; then
    echo "[INFO] Reusing the configured upstream Pango cross-build"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs 2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

echo "[DONE] Upstream Pango 1.58.2 x86_64 DSOs: $BUILD_TREE/pango"
echo "Run ./build.sh --arch x86_64 --firmware both --disk-type iso to package it."
