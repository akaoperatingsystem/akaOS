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
SOURCE_DIR="$BUILD_DIR/upstream-source/harfbuzz-14.5.0"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/harfbuzz-14.5.0/x86_64-akaos-shared-runtime"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
CXX_RUNTIME="$BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64"

for command_name in curl sha256sum tar clang clang++ meson ninja llvm-ar; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld llvm meson ninja-build" >&2
        exit 2
    fi
done

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$BUILD_DIR/libc++.so.1" "$BUILD_DIR/libc++abi.so.1" \
    "$BUILD_DIR/libunwind.so.1" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libfreetype.so.6.20.6"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS x86_64 graphics/C++ prerequisite: $required_file" >&2
        echo "Build akaOS GLib/Cairo/FreeType and the shared C++ runtime first." >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/harfbuzz-14.5.0.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://github.com/harfbuzz/harfbuzz/releases/download/14.5.0/harfbuzz-14.5.0.tar.xz \
        -o "$SOURCE_DIR/harfbuzz-14.5.0.tar.xz"
fi
printf '%s  %s\n' \
    b7132e148358a45185c9feafd049dbaf243649d3c44414b3534d9c95d18592b9 \
    "$SOURCE_DIR/harfbuzz-14.5.0.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/harfbuzz-14.5.0.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export CXXFLAGS="-nostdinc -nostdinc++ -I$CXX_RUNTIME/include/c++/v1 -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR -L$CXX_RUNTIME $BUILD_DIR/libc++.so.1 $BUILD_DIR/libc++abi.so.1 $BUILD_DIR/libunwind.so.1 $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6 $CXX_RUNTIME/libakaos-compiler-builtins.a"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-harfbuzz-x86_64.ini"
    -Ddefault_library=shared -Dbuildtype=release
    -Dglib=enabled -Dgobject=disabled -Dcairo=disabled
    -Dchafa=disabled -Dpng=disabled -Dzlib=disabled -Dicu=disabled
    -Dgraphite2=disabled -Dfreetype=enabled -Dfontations=disabled
    -Draster=disabled -Dvector=disabled -Dgpu=disabled -Dsubset=enabled
    -Dtests=disabled -Dintrospection=disabled -Ddocs=disabled
    -Dutilities=disabled -Dbenchmark=disabled -Dwith_libstdcxx=false
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs 2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

if [[ ! -f "$BUILD_TREE/src/libharfbuzz-subset.so.0.61450.0" ]]; then
    echo "Upstream HarfBuzz was built without the required subset DSO" >&2
    exit 1
fi

echo "[DONE] Upstream HarfBuzz 14.5.0 x86_64 DSO: $BUILD_TREE/src/libharfbuzz.so"
echo "Run ./build.sh --arch x86_64 --firmware both --disk-type iso to package it."
