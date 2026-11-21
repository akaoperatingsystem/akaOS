#!/usr/bin/env bash
set -euo pipefail
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
VERSION="10.49"
ARCHIVE_SHA256="53c156e1ba416a20da8e65395daa132da0d80e76910424caca3fcdae7831d384"
WORKROOT="${AKAOS_PCRE2_WORKROOT:-$HOME/.cache/akaos/pcre2-10.49/x86_64-akaos}"
SOURCE_ROOT="$WORKROOT/source-root"
SOURCE_TREE="$SOURCE_ROOT/source"
BUILD_TREE="$WORKROOT/build"
TARGET_SYSROOT="$BUILD_DIR/upstream-sysroot/pcre2-$VERSION-x86_64"
ARCHIVE="$SOURCE_ROOT/pcre2-$VERSION.tar.bz2"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang make; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing official PCRE2 build tool: $command_name" >&2
        exit 2
    }
done
[[ -f "$BUILD_DIR/libc.so.6" && -f "$BUILD_DIR/libm.so.6" && -x "$CC_WRAPPER" ]] || {
    echo "Missing akaOS libc/libm/cross-wrapper for PCRE2." >&2
    exit 2
}
mkdir -p "$SOURCE_ROOT" "$BUILD_TREE" "$TARGET_SYSROOT"
if [[ ! -s "$ARCHIVE" ]]; then
    curl -fL --retry 3 --max-time 600 \
        "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-$VERSION/pcre2-$VERSION.tar.bz2" \
        -o "$ARCHIVE.part"
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/configure" ]]; then
    mkdir -p "$SOURCE_TREE"
    tar -xf "$ARCHIVE" -C "$SOURCE_TREE" --strip-components=1
fi
export PKG_CONFIG_LIBDIR="$TARGET_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$TARGET_SYSROOT"
export CPPFLAGS="-I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export CFLAGS="-Os -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-stack-protector -fPIC -nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include"
export LDFLAGS="-Wl,--allow-shlib-undefined -Wl,--as-needed -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"
if [[ ! -f "$BUILD_TREE/Makefile" ]]; then
    (
        cd "$BUILD_TREE"
        ac_cv_prog_cc_cross=yes ac_cv_func_malloc_0_nonnull=yes \
        "$SOURCE_TREE/configure" \
            --host=x86_64-akaos-linux-gnu \
            --build=x86_64-pc-linux-gnu \
            --prefix=/usr --libdir=/usr/lib \
            --enable-shared --disable-static \
            --disable-pcre2-16 --disable-pcre2-32 \
            --disable-jit --disable-pcre2grep-libz --disable-pcre2grep-libbz2 \
            CC="$CC_WRAPPER" CCLD="$CC_WRAPPER" LD=ld.lld \
            AR=llvm-ar RANLIB=llvm-ranlib STRIP=llvm-strip \
            libpcre2_8_la_LDFLAGS="-nostdlib -Wl,--allow-shlib-undefined" \
            libpcre2_posix_la_LDFLAGS="-nostdlib -Wl,--allow-shlib-undefined"
    ) >"$BUILD_TREE/configure.log" 2>&1 || {
        tail -50 "$BUILD_TREE/configure.log" >&2
        exit 1
    }
fi
# GNU libtool sees the wrapped Clang driver as GCC and appends the host crtbeginS.
# The akaOS libc provides __dso_handle itself; suppress the host CRT injection.
for libtool_file in "$BUILD_TREE/libtool" "$BUILD_DIR/upstream-build/pcre2-$VERSION/x86_64-akaos/libtool"; do
    if [[ -f "$libtool_file" ]]; then
        sed -i -e 's/^with_gcc=yes$/with_gcc=no/' \
               -e 's/^build_libtool_need_lc=yes$/build_libtool_need_lc=no/' \
               -e 's|^sys_lib_search_path_spec=.*$|sys_lib_search_path_spec=""|' \
            "$libtool_file"
    fi
done
make -C "$BUILD_TREE" -j"${AKAOS_PCRE2_BUILD_JOBS:-1}" >"$BUILD_TREE/build.log" 2>&1 || {
    tail -50 "$BUILD_TREE/build.log" >&2
    exit 1
}
make -C "$BUILD_TREE" install DESTDIR="$TARGET_SYSROOT" >"$BUILD_TREE/install.log" 2>&1 || {
    tail -30 "$BUILD_TREE/install.log" >&2
    exit 1
}
if [[ ! -f "$TARGET_SYSROOT/usr/lib/libpcre2-8.so.0.16.1" ||
      ! -f "$TARGET_SYSROOT/usr/include/pcre2.h" ||
      ! -f "$TARGET_SYSROOT/usr/lib/pkgconfig/libpcre2-8.pc" ]]; then
    echo "Official PCRE2 10.49 target install is incomplete" >&2
    exit 1
fi
printf '[DONE] Official PCRE2 %s built for akaOS: %s\n' "$VERSION" "$TARGET_SYSROOT"
