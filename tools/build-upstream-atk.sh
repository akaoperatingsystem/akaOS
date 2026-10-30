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
SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
SOURCE_DIR="$BUILD_DIR/upstream-source/atk-2.38.0"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/atk-2.38.0/x86_64-akaos"
HOST_GLIB_TOOLS="$BUILD_DIR/host-tools/glib-2.88"
HOST_GLIB_PKGCONFIG="$BUILD_DIR/host-tools/gtk4-glib-pkgconfig"

for command_name in curl sha256sum tar clang meson ninja pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream ATK build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld meson ninja-build pkg-config" >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/atk-2.38.0.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://download.gnome.org/sources/atk/2.38/atk-2.38.0.tar.xz \
        -o "$SOURCE_DIR/atk-2.38.0.tar.xz"
fi
printf '%s  %s\n' \
    ac4de2a4ef4bd5665052952fe169657e65e895c5057dffb3c2a810f6191a0c36 \
    "$SOURCE_DIR/atk-2.38.0.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/atk-2.38.0.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

if ! command -v glib-compile-resources >/dev/null 2>&1; then
    if [[ ! -x "$HOST_GLIB_TOOLS/root/usr/bin/glib-compile-resources" ]]; then
        mkdir -p "$HOST_GLIB_TOOLS"
        (cd "$HOST_GLIB_TOOLS" && apt-get download libgio-2.0-dev-bin)
        dpkg-deb -x "$HOST_GLIB_TOOLS"/libgio-2.0-dev-bin_*.deb \
            "$HOST_GLIB_TOOLS/root"
    fi
    export PATH="$HOST_GLIB_TOOLS/root/usr/bin:$PATH"
fi

mkdir -p "$HOST_GLIB_PKGCONFIG"
cp "$SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" \
    "$HOST_GLIB_PKGCONFIG/glib-2.0.pc"
cp "$SYSROOT/usr/lib/pkgconfig/gio-2.0.pc" \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_compile_resources=.*#glib_compile_resources=glib-compile-resources#' \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_compile_schemas=.*#glib_compile_schemas=glib-compile-schemas#' \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"

export PKG_CONFIG_LIBDIR="$HOST_GLIB_PKGCONFIG:$SYSROOT/usr/local/lib/pkgconfig:$SYSROOT/usr/local/share/pkgconfig:$SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$SYSROOT/usr/lib -L$SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Dintrospection=false -Ddocs=false
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" && -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi

meson compile -C "$BUILD_TREE" --jobs "${AKAOS_ATK_BUILD_JOBS:-4}"
DESTDIR="$SYSROOT" meson install -C "$BUILD_TREE"
if [[ ! -e "$SYSROOT/usr/local/lib/libatk-1.0.so.0" ||
      ! -f "$SYSROOT/usr/local/lib/pkgconfig/atk.pc" ]]; then
    echo "Upstream ATK 2.38.0 did not install its shared library and pkg-config file" >&2
    exit 1
fi
echo "[DONE] Upstream ATK 2.38.0 x86_64 build: $BUILD_TREE"
