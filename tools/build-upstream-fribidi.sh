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
SOURCE_DIR="$BUILD_DIR/upstream-source/fribidi-1.0.17"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/fribidi-1.0.17/x86_64-akaos"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"

for command_name in curl sha256sum tar clang meson ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld meson ninja-build" >&2
        exit 2
    fi
done

if [[ ! -f "$BUILD_DIR/libc.so.6" ]]; then
    echo "Missing akaOS x86_64 libc: $BUILD_DIR/libc.so.6" >&2
    echo "Build akaOS x86_64 first." >&2
    exit 2
fi

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/fribidi-1.0.17.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://github.com/fribidi/fribidi/releases/download/v1.0.17/fribidi-1.0.17.tar.xz \
        -o "$SOURCE_DIR/fribidi-1.0.17.tar.xz"
fi
printf '%s  %s\n' \
    6949dcde27d41cebad1fd741fcafc36d55a1020d2d872d4a6eb3914caabbada2 \
    "$SOURCE_DIR/fribidi-1.0.17.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/fribidi-1.0.17.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$BUILD_DIR $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release
    -Ddeprecated=true -Ddocs=false -Dbin=false -Dtests=false
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs 2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

echo "[DONE] Upstream FriBidi 1.0.17 x86_64 DSO: $BUILD_TREE/lib/libfribidi.so.0.4.0"
echo "Run ./build.sh --arch x86_64 --firmware both --disk-type iso to package it."
