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
SOURCE_DIR="$BUILD_DIR/upstream-source/expat-2.8.5"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/expat-2.8.5/x86_64-akaos-linux"
SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"

for command_name in curl sha256sum tar cmake clang llvm-ar; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream Expat build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar cmake clang llvm" >&2
        exit 2
    fi
done

for required_file in "$BUILD_DIR/libc.so.6"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS x86_64 libc prerequisite: $required_file" >&2
        echo "Build akaOS x86_64 userspace first." >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/expat-2.8.5.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://github.com/libexpat/libexpat/releases/download/R_2_8_5/expat-2.8.5.tar.xz \
        -o "$SOURCE_DIR/expat-2.8.5.tar.xz"
fi
printf '%s  %s\n' \
    1e727b8933ec51a77a9a9d9afcf8e688bce45d907c13e36ab7393fe36e703182 \
    "$SOURCE_DIR/expat-2.8.5.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/CMakeLists.txt" ]]; then
    tar -xf "$SOURCE_DIR/expat-2.8.5.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

cmake_flags=(
    -DCMAKE_SYSTEM_NAME=Linux
    -DCMAKE_SYSTEM_PROCESSOR=x86_64
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_C_COMPILER_TARGET=x86_64-unknown-none-elf
    -DCMAKE_AR="$(command -v llvm-ar)"
    -DCMAKE_RANLIB="$(command -v llvm-ranlib)"
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_STANDARD_LIBRARIES=
    -DCMAKE_INSTALL_PREFIX=/usr/local
    -DCMAKE_INSTALL_LIBDIR=lib
    -DEXPAT_SHARED_LIBS=ON
    -DEXPAT_BUILD_TOOLS=OFF
    -DEXPAT_BUILD_EXAMPLES=OFF
    -DEXPAT_BUILD_TESTS=OFF
    -DEXPAT_BUILD_DOCS=OFF
    -DEXPAT_BUILD_FUZZERS=OFF
    -DEXPAT_BUILD_PKGCONFIG=ON
    -DEXPAT_DEV_URANDOM=ON
    "-DCMAKE_C_FLAGS=-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
    "-DCMAKE_SHARED_LINKER_FLAGS=-nostdlib -fuse-ld=lld -Wl,--no-undefined -L$BUILD_DIR $BUILD_DIR/libc.so.6"
)
cmake -S "$SOURCE_TREE" -B "$BUILD_TREE" "${cmake_flags[@]}"
cmake --build "$BUILD_TREE" --parallel 2
DESTDIR="$SYSROOT" cmake --install "$BUILD_TREE"

echo "[DONE] Upstream Expat 2.8.5 x86_64 DSO: $SYSROOT/usr/local/lib/libexpat.so.1"
