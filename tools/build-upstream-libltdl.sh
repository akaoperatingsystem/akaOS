#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream libltdl for akaOS.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="2.4.7"
SHA256="dd637e270439b208907ceead3f163470ed2ce5723ef97ffbda6463c64b57128a"
URL="https://deb.debian.org/debian/pool/main/libt/libtool/libtool_2.4.7.orig.tar.xz"
WORKROOT="${AKAOS_LIBLTDL_WORKROOT:-$HOME/.cache/akaos/libltdl}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream libltdl build tool: $command_name" >&2
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

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/libtool-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[libltdl] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/libtool-$VERSION/libltdl"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[libltdl] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/libltdl-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libltdl.so" ]]; then
    echo "[libltdl] configuring libltdl $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --disable-static                 --enable-shared                 --enable-ltdl-install
    ) >"$LOG_DIR/libltdl-configure.log" 2>&1 || {
        echo "[libltdl] configure failed" >&2
        tail -30 "$LOG_DIR/libltdl-configure.log" >&2
        exit 1
    }

    echo "[libltdl] building libltdl $VERSION"
    make -C "$BUILD_DIR_PATH" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >"$LOG_DIR/libltdl-build.log" 2>&1 || {
        echo "[libltdl] build failed" >&2
        tail -30 "$LOG_DIR/libltdl-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/libltdl-install.log" 2>&1 || {
        echo "[libltdl] install failed" >&2
        tail -30 "$LOG_DIR/libltdl-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE_PREFIX/lib/libltdl.so" ]]; then
    echo "[libltdl] failed to produce libltdl.so" >&2
    exit 1
fi

find "$STAGE_PREFIX" -name "*.la" -delete

mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[DONE] Upstream libltdl $VERSION built and published to graphics sysroot"
