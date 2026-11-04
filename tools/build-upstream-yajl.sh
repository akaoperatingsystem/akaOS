#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="2.1.0"
SHA256="3fb73364a5a30efe615046d07e6db9d09fd2b41c763c5f7d3bfb121cd5c5ac5a"
URL="https://github.com/lloyd/yajl/archive/refs/tags/$VERSION.tar.gz"
WORKROOT="${AKAOS_YAJL_WORKROOT:-$HOME/.cache/akaos/yajl}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

ARCHIVE="$SRC_DIR/yajl-$VERSION.tar.gz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[yajl] fetching yajl $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[yajl] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi

actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[yajl] checksum mismatch" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/yajl-$VERSION"
if [[ ! -f "$SOURCE_DIR/.akaos-extracted" ]]; then
    echo "[yajl] extracting yajl $VERSION"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
    tar -xf "$ARCHIVE" -C "$SOURCE_DIR" --strip-components=1
    sed -i 's/ADD_SUBDIRECTORY(test)/# ADD_SUBDIRECTORY(test)/' "$SOURCE_DIR/CMakeLists.txt"
    sed -i 's/ADD_SUBDIRECTORY(reformatter)/# ADD_SUBDIRECTORY(reformatter)/' "$SOURCE_DIR/CMakeLists.txt"
    sed -i 's/ADD_SUBDIRECTORY(verify)/# ADD_SUBDIRECTORY(verify)/' "$SOURCE_DIR/CMakeLists.txt"
    sed -i 's/ADD_SUBDIRECTORY(example)/# ADD_SUBDIRECTORY(example)/' "$SOURCE_DIR/CMakeLists.txt"
    sed -i 's/ADD_SUBDIRECTORY(perf)/# ADD_SUBDIRECTORY(perf)/' "$SOURCE_DIR/CMakeLists.txt"
    touch "$SOURCE_DIR/.akaos-extracted"
fi

BUILD_DIR_PATH="$BUILD_ROOT/yajl-$VERSION"
if [[ -f "$STAGE_PREFIX/lib/libyajl.so" ]]; then
    echo "[yajl] already staged"
else
    echo "[yajl] configuring yajl $VERSION"
    rm -rf "$BUILD_DIR_PATH"
    mkdir -p "$BUILD_DIR_PATH"

    cmake -B "$BUILD_DIR_PATH" -S "$SOURCE_DIR" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_POLICY_DEFAULT_CMP0026=OLD \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
        -DCMAKE_C_COMPILER="$CC_WRAPPER" \
        -DCMAKE_INSTALL_PREFIX="$STAGE_PREFIX" \
        -DCMAKE_C_FLAGS="-O2 -g0" \
        -DCMAKE_EXE_LINKER_FLAGS="-nostdlib -fuse-ld=lld -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6" \
        -DCMAKE_SHARED_LINKER_FLAGS="-nostdlib -fuse-ld=lld -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6" \
        -DBUILD_STATIC=OFF \
        >"$LOG_DIR/yajl-configure.log" 2>&1 || {
            echo "[yajl] configure failed; see $LOG_DIR/yajl-configure.log" >&2
            tail -40 "$LOG_DIR/yajl-configure.log" >&2
            exit 1
        }

    echo "[yajl] building yajl"
    cmake --build "$BUILD_DIR_PATH" -j"$(nproc)" >"$LOG_DIR/yajl-build.log" 2>&1 || {
        echo "[yajl] build failed; see $LOG_DIR/yajl-build.log" >&2
        tail -40 "$LOG_DIR/yajl-build.log" >&2
        exit 1
    }

    echo "[yajl] installing yajl"
    cmake --install "$BUILD_DIR_PATH" >"$LOG_DIR/yajl-install.log" 2>&1 || {
        echo "[yajl] install failed; see $LOG_DIR/yajl-install.log" >&2
        tail -40 "$LOG_DIR/yajl-install.log" >&2
        exit 1
    }
fi

# Publish to sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[yajl] DONE"
