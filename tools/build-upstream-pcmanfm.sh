#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream PCManFM file and desktop manager for LXDE.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="1.3.2"
SHA256="14cb7b247493c4cce65fbb5902611e3ad00a7a870fbc1e50adc50428c5140cf7"
URL="https://deb.debian.org/debian/pool/main/p/pcmanfm/pcmanfm_1.3.2.orig.tar.xz"
WORKROOT="${AKAOS_PCMANFM_WORKROOT:-$HOME/.cache/akaos/pcmanfm}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream PCManFM build tool: $command_name" >&2
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
if [[ ! -f "$BUILD_DIR/user-dynamic-crt0.o" ]]; then
    echo "Missing the akaOS startup object in $BUILD_DIR; run build.sh first" >&2
    exit 2
fi
for required_package in gtk+-3.0 glib-2.0 libfm libfm-gtk3 x11; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream PCManFM needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/pcmanfm-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[pcmanfm] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/pcmanfm-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[pcmanfm] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/pcmanfm-$VERSION"
if [[ ! -x "$STAGE/usr/local/bin/pcmanfm" ]]; then
    echo "[pcmanfm] configuring pcmanfm $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --with-gtk=3
    ) >"$LOG_DIR/pcmanfm-configure.log" 2>&1 || {
        echo "[pcmanfm] configure failed" >&2
        tail -30 "$LOG_DIR/pcmanfm-configure.log" >&2
        exit 1
    }

    echo "[pcmanfm] building pcmanfm $VERSION"
    make -C "$BUILD_DIR_PATH/src" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >"$LOG_DIR/pcmanfm-build.log" 2>&1 || {
        echo "[pcmanfm] build failed" >&2
        tail -30 "$LOG_DIR/pcmanfm-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/pcmanfm-build.log" 2>&1 || {
        echo "[pcmanfm] build root failed" >&2
        tail -30 "$LOG_DIR/pcmanfm-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/pcmanfm-install.log" 2>&1 || {
        echo "[pcmanfm] install failed" >&2
        tail -30 "$LOG_DIR/pcmanfm-install.log" >&2
        exit 1
    }
fi

if [[ ! -x "$STAGE/usr/local/bin/pcmanfm" ]]; then
    echo "[pcmanfm] did not install pcmanfm binary" >&2
    exit 1
fi

if ! llvm-readelf -l "$STAGE/usr/local/bin/pcmanfm" 2>/dev/null | grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[pcmanfm] binary does not use the akaOS dynamic linker" >&2
    exit 1
fi

# Publish to graphics sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin share; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
if [[ -d "$STAGE/etc" ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc"
    cp -a "$STAGE/etc/." "$GRAPHICS_SYSROOT/etc/"
fi

echo "[DONE] Upstream PCManFM $VERSION built and published to graphics sysroot"
