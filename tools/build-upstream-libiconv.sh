#!/usr/bin/env bash
set -euo pipefail
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
VERSION="1.18"
ARCHIVE_SHA256="3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8"
WORKROOT="${AKAOS_LIBICONV_WORKROOT:-$HOME/.cache/akaos/libiconv-$VERSION/x86_64-akaos}"
SOURCE_ROOT="$BUILD_DIR/upstream-source/libiconv-$VERSION"
SOURCE_TREE="$WORKROOT/source"
BUILD_TREE="$WORKROOT/build"
TARGET_SYSROOT="$BUILD_DIR/upstream-sysroot/glib-x86_64"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
ARCHIVE="$SOURCE_ROOT/libiconv-$VERSION.tar.gz"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar make; do
    command -v "$command_name" >/dev/null 2>&1 || { echo "Missing libiconv build tool: $command_name" >&2; exit 2; }
done
[[ -f "$BUILD_DIR/libc.so.6" && -f "$BUILD_DIR/libm.so.6" && -x "$CC_WRAPPER" ]] || { echo "Missing akaOS libiconv toolchain" >&2; exit 2; }
mkdir -p "$SOURCE_ROOT" "$WORKROOT" "$TARGET_SYSROOT/usr/lib" "$GRAPHICS_SYSROOT/usr/lib"
export CPPFLAGS="-I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export CFLAGS="-Os -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-stack-protector -fPIC -nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"
if [[ ! -s "$ARCHIVE" ]]; then
    curl -fL --retry 3 --max-time 600 "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$VERSION.tar.gz" -o "$ARCHIVE.part"
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/configure" ]]; then
    mkdir -p "$SOURCE_TREE"
    tar -xf "$ARCHIVE" -C "$SOURCE_TREE" --strip-components=1
fi
if [[ ! -f "$BUILD_TREE/Makefile" ]]; then
    mkdir -p "$BUILD_TREE"
    (
        cd "$BUILD_TREE"
        ac_cv_prog_cc_cross=yes "$SOURCE_TREE/configure" \
            --host=x86_64-akaos-linux-gnu --build=x86_64-pc-linux-gnu \
            --prefix=/usr --libdir=/usr/lib --enable-shared --disable-static \
            --disable-nls --disable-rpath \
            CC="$CC_WRAPPER" CCLD="$CC_WRAPPER" LD=ld.lld \
            AR=llvm-ar RANLIB=llvm-ranlib STRIP=llvm-strip LIBS="$BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"
    ) >"$BUILD_TREE/configure.log" 2>&1 || { tail -40 "$BUILD_TREE/configure.log" >&2; exit 1; }
fi
make -C "$BUILD_TREE" -j1 >"$BUILD_TREE/build.log" 2>&1 || { tail -40 "$BUILD_TREE/build.log" >&2; exit 1; }
make -C "$BUILD_TREE" install DESTDIR="$TARGET_SYSROOT" >"$BUILD_TREE/install.log" 2>&1 || { tail -30 "$BUILD_TREE/install.log" >&2; exit 1; }
install -m 0644 "$TARGET_SYSROOT/usr/lib/libiconv.a" "$GRAPHICS_SYSROOT/usr/lib/libiconv.a"
install -m 0644 "$TARGET_SYSROOT/usr/lib/iconv.h" "$GRAPHICS_SYSROOT/usr/include/iconv.h"
for library in "$TARGET_SYSROOT"/usr/lib/libiconv.so*; do
    [[ -f "$library" ]] || continue
    install -m 0644 "$library" "$GRAPHICS_SYSROOT/usr/lib/${library##*/}"
done
[[ -f "$GRAPHICS_SYSROOT/usr/lib/libiconv.so" ]] || { echo "libiconv DSO missing after install" >&2; exit 1; }
printf '[DONE] Official libiconv %s staged for GLib/Cairo\n' "$VERSION"
