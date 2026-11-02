#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream dconf for akaOS.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="0.40.0"
SHA256="cf7f22a4c9200421d8d3325c5c1b8b93a36843650c9f95d6451e20f0bcb24533"
URL="https://deb.debian.org/debian/pool/main/d/dconf/dconf_0.40.0.orig.tar.xz"
WORKROOT="${AKAOS_DCONF_WORKROOT:-$HOME/.cache/akaos/dconf}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar meson ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream dconf build tool: $command_name" >&2
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
ARCHIVE="$SRC_DIR/dconf-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[dconf] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/dconf-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[dconf] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/dconf-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libdconf.so" ]]; then
    echo "[dconf] configuring dconf $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"

    export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
    export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
    export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

    meson setup         --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"         --prefix=/usr/local --libdir=lib --includedir=include         -Ddefault_library=shared -Dbuildtype=release -Doptimization=2         -Dbash_completion=false         -Dman=false         -Dgtk_doc=false         -Dvapi=false         "$BUILD_DIR_PATH" "$SOURCE_DIR" >"$LOG_DIR/dconf-configure.log" 2>&1 || {
        echo "[dconf] configure failed" >&2
        tail -30 "$LOG_DIR/dconf-configure.log" >&2
        exit 1
    }

    echo "[dconf] building dconf $VERSION"
    ninja -C "$BUILD_DIR_PATH" >"$LOG_DIR/dconf-build.log" 2>&1 || {
        echo "[dconf] build failed" >&2
        tail -30 "$LOG_DIR/dconf-build.log" >&2
        exit 1
    }

    echo "[dconf] relinking binaries with akaOS dynamic runtime"
    clang --target=x86_64-unknown-none-elf \
        -nostdlib -fuse-ld=lld \
        -Wl,--allow-shlib-undefined -Wl,--as-needed \
        -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 \
        -Wl,--dynamic-linker,/lib/ld-akaos.so \
        -Wl,-T,"$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" \
        "$BUILD_DIR_PATH/bin/dconf.p/dconf.c.o" \
        "$BUILD_DIR_PATH/gvdb/libgvdb.a" \
        "$BUILD_DIR_PATH/common/libdconf-common.a" \
        -L"$BUILD_DIR_PATH/client" -ldconf \
        -L"$GRAPHICS_SYSROOT/usr/local/lib" -L"$GRAPHICS_SYSROOT/usr/lib" -L"$BUILD_DIR" \
        -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl \
        -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 \
        -o "$BUILD_DIR_PATH/bin/dconf"

    clang --target=x86_64-unknown-none-elf \
        -nostdlib -fuse-ld=lld \
        -Wl,--allow-shlib-undefined -Wl,--as-needed \
        -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 \
        -Wl,--dynamic-linker,/lib/ld-akaos.so \
        -Wl,-T,"$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" \
        "$BUILD_DIR_PATH/service/dconf-service.p/main.c.o" \
        "$BUILD_DIR_PATH/service/libdconf-service.a" \
        "$BUILD_DIR_PATH/common/libdconf-common.a" \
        "$BUILD_DIR_PATH/gvdb/libgvdb.a" \
        "$BUILD_DIR_PATH/shm/libdconf-shm.a" \
        -L"$GRAPHICS_SYSROOT/usr/local/lib" -L"$GRAPHICS_SYSROOT/usr/lib" -L"$BUILD_DIR" \
        -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl \
        -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 \
        -o "$BUILD_DIR_PATH/service/dconf-service"

    DESTDIR="$STAGE" ninja -C "$BUILD_DIR_PATH" install >"$LOG_DIR/dconf-install.log" 2>&1 || {
        echo "[dconf] install failed" >&2
        tail -30 "$LOG_DIR/dconf-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE_PREFIX/lib/libdconf.so" ]]; then
    echo "[dconf] failed to produce libdconf.so" >&2
    exit 1
fi

find "$STAGE_PREFIX" -name "*.la" -delete

mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin include lib libexec share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[DONE] Upstream dconf $VERSION built and published to graphics sysroot"
