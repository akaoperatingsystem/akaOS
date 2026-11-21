#!/usr/bin/env bash
set -euo pipefail
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
VERSION="2.90.0"
ARCHIVE_SHA256="17d15cac2af80a33271127408e0abc2748eb297c595c2a26409e81e14e7d1b8f"
SOURCE_ROOT="$BUILD_DIR/upstream-source/glib-$VERSION"
SOURCE_TREE="$SOURCE_ROOT/glib-$VERSION"
BUILD_TREE="$BUILD_DIR/upstream-build/glib-$VERSION/x86_64-akaos-v7"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
LIBICONV_SYSROOT="$BUILD_DIR/upstream-sysroot/glib-x86_64"
PCRE2_SYSROOT="$BUILD_DIR/upstream-sysroot/pcre2-10.49-x86_64"
CROSS_FILE="$ROOT_DIR/tools/meson-akaos-x86_64.ini"
ARCHIVE="$SOURCE_ROOT/glib-$VERSION.tar.xz"

for command_name in curl sha256sum tar clang meson ninja llvm-ar llvm-strip pkg-config patch; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing official GLib build tool: $command_name" >&2
        exit 2
    }
done
for required in "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6"                 "$CROSS_FILE"                 "$PCRE2_SYSROOT/usr/lib/libpcre2-8.so.0.16.1"                 "$PCRE2_SYSROOT/usr/lib/pkgconfig/libpcre2-8.pc"                 "$PCRE2_SYSROOT/usr/include/pcre2.h"; do
    [[ -f "$required" ]] || { echo "Missing official GLib prerequisite: $required" >&2; exit 2; }
done
mkdir -p "$SOURCE_ROOT" "$BUILD_TREE" "$GRAPHICS_SYSROOT"
if [[ ! -s "$ARCHIVE" ]]; then
    curl -fL --retry 3 --max-time 600 \
        "https://download.gnome.org/sources/glib/2.90/glib-$VERSION.tar.xz" \
        -o "$ARCHIVE.part"
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    mkdir -p "$SOURCE_TREE"
    tar -xf "$ARCHIVE" -C "$SOURCE_TREE" --strip-components=1
    touch "$SOURCE_TREE/.akaos-extracted"
fi
if ! grep -q "akaos_mount_fields" "$SOURCE_TREE/gio/gunixmounts.c"; then
    patch -d "$SOURCE_TREE" -p1 < "$ROOT_DIR/tools/glib-2.90.0-akaos.patch"
fi
grep -q "g_unix_fd_query_path() is not supported" "$SOURCE_TREE/glib/glib-unix.c" || {
    echo "Missing akaOS g_unix_fd_query_path port hunk" >&2; exit 1;
}
grep -q "akaos_mount_fields" "$SOURCE_TREE/gio/gunixmounts.c" || {
    echo "Missing akaOS Unix mount port hunk" >&2; exit 1;
}
grep -q "G_CREDENTIALS_USE_LINUX_UCRED" "$SOURCE_TREE/gio/gcredentialsprivate.h" || {
    echo "Missing akaOS SO_PEERCRED credential port hunk" >&2; exit 1;
}
export PKG_CONFIG_LIBDIR="$PCRE2_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$PCRE2_SYSROOT"
export CFLAGS="-Os -ffunction-sections -fdata-sections -nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-Wl,--gc-sections -s -nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$PCRE2_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"
meson_args=(--cross-file "$CROSS_FILE" --prefix=/usr --libdir=lib --includedir=include \
    --libexecdir=/usr/libexec -Ddefault_library=shared -Dbuildtype=release -Doptimization=2 \
    -Ddocumentation=false -Dman-pages=disabled -Dintrospection=disabled \
    -Dtests=false -Dinstalled_tests=false -Dglib_debug=disabled \
    -Dlibmount=disabled -Dselinux=disabled -Dxattr=false \
    -Dlibelf=disabled -Dsysprof=disabled -Dsystemtap=disabled \
    -Dnls=enabled)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" && -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs "${AKAOS_GLIB_BUILD_JOBS:-2}"
mkdir -p "$PCRE2_SYSROOT/usr/lib"
ln -sfn libpcre2-8.so.0.16.1 "$PCRE2_SYSROOT/usr/lib/libpcre2-8.so.0"
ln -sfn libpcre2-8.so.0 "$PCRE2_SYSROOT/usr/lib/libpcre2-8.so"
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"
for required in     "$GRAPHICS_SYSROOT/usr/lib/libglib-2.0.so.0.9000.0"     "$GRAPHICS_SYSROOT/usr/lib/libgobject-2.0.so.0.9000.0"     "$GRAPHICS_SYSROOT/usr/lib/libgio-2.0.so.0.9000.0"     "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc"; do
    [[ -f "$required" ]] || { echo "Official GLib install is incomplete: $required" >&2; exit 1; }
done
printf '[DONE] Official GLib %s built and installed into %s\\n' "$VERSION" "$GRAPHICS_SYSROOT"
