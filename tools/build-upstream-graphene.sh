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
SOURCE_DIR="$BUILD_DIR/upstream-source/graphene-1.10.8"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/graphene-1.10.8/x86_64-akaos"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"

for command_name in curl sha256sum tar clang meson ninja pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream Graphene build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld meson ninja-build pkg-config" >&2
        exit 2
    fi
done

for required_file in "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gobject-2.0.pc"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS x86_64 Graphene prerequisite: $required_file" >&2
        echo "Build the x86_64 userspace and upstream GLib/GObject stack first." >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/graphene-1.10.8.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://download.gnome.org/sources/graphene/1.10/graphene-1.10.8.tar.xz \
        -o "$SOURCE_DIR/graphene-1.10.8.tar.xz"
fi
printf '%s  %s\n' \
    a37bb0e78a419dcbeaa9c7027bcff52f5ec2367c25ec859da31dfde2928f279a \
    "$SOURCE_DIR/graphene-1.10.8.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/graphene-1.10.8.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Dgobject_types=true -Dintrospection=disabled -Dtests=false
    -Dinstalled_tests=false -Dgtk_doc=false -Dgcc_vector=false
    -Dsse2=true -Darm_neon=false
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" && -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs 2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

if [[ ! -f "$BUILD_TREE/src/libgraphene-1.0.so.0.1000.8" ]]; then
    echo "Expected upstream Graphene shared library was not built" >&2
    exit 1
fi

echo "[DONE] Upstream Graphene 1.10.8 x86_64 build: $BUILD_TREE/src"
