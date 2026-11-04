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
VERSION="4.33"
SHA256="507eb7b8d1015fbec5b935f34ebed15bf346bed04a11ab82b8eee848c4205aea"
URL="http://dist.schmorp.de/libev/libev-$VERSION.tar.gz"
WORKROOT="${AKAOS_LIBEV_WORKROOT:-$HOME/.cache/akaos/libev}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

ARCHIVE="$SRC_DIR/libev-$VERSION.tar.gz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[libev] fetching libev $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[libev] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi

actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[libev] checksum mismatch" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/libev-$VERSION"
if [[ ! -f "$SOURCE_DIR/.akaos-extracted" ]]; then
    echo "[libev] extracting libev $VERSION"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
    tar -xf "$ARCHIVE" -C "$SOURCE_DIR" --strip-components=1
    touch "$SOURCE_DIR/.akaos-extracted"
fi

BUILD_DIR_PATH="$BUILD_ROOT/libev-$VERSION"
if [[ -f "$STAGE_PREFIX/lib/libev.so" ]]; then
    echo "[libev] already staged"
else
    echo "[libev] configuring libev $VERSION"
    rm -rf "$BUILD_DIR_PATH"
    mkdir -p "$BUILD_DIR_PATH"
    (
        cd "$BUILD_DIR_PATH" || exit 1
        env \
            CC="$CC_WRAPPER" \
            CFLAGS="-O2 -g0" \
            CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include" \
            LDFLAGS="-nostdlib -fuse-ld=lld -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6" \
            PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig" \
            PKG_CONFIG_SYSROOT_DIR="" \
            "$SOURCE_DIR/configure" \
                --host=x86_64-unknown-linux-gnu \
                --prefix="$STAGE_PREFIX" \
                --disable-static \
                --enable-shared
    ) >"$LOG_DIR/libev-configure.log" 2>&1 || {
        echo "[libev] configure failed; see $LOG_DIR/libev-configure.log" >&2
        tail -40 "$LOG_DIR/libev-configure.log" >&2
        exit 1
    }

    echo "[libev] building libev"
    make -C "$BUILD_DIR_PATH" -j"$(nproc)" >"$LOG_DIR/libev-build.log" 2>&1 || {
        echo "[libev] build failed; see $LOG_DIR/libev-build.log" >&2
        tail -40 "$LOG_DIR/libev-build.log" >&2
        exit 1
    }

    echo "[libev] installing libev"
    make -C "$BUILD_DIR_PATH" install >"$LOG_DIR/libev-install.log" 2>&1 || {
        echo "[libev] install failed; see $LOG_DIR/libev-install.log" >&2
        tail -40 "$LOG_DIR/libev-install.log" >&2
        exit 1
    }
fi

# Publish pkg-config and files to sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

# Create libev.pc if not present
mkdir -p "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig"
cat > "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/libev.pc" << 'EOF'
prefix=/usr/local
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: libev
Description: High-performance event loop library
Version: 4.33
Libs: -L${libdir} -lev
Cflags: -I${includedir}
EOF

echo "[libev] DONE"
