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
SOURCE_DIR="$BUILD_DIR/upstream-source/gtk-4.22.5"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/gtk-4.22.5/x86_64-akaos-wayland"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
HOST_GLIB_TOOLS="$BUILD_DIR/host-tools/glib-2.88"
HOST_GLIB_PKGCONFIG="$BUILD_DIR/host-tools/gtk4-glib-pkgconfig"
WAYLAND_HOST_PREFIX="$BUILD_DIR/host-tools/wayland-1.26.0"
GTK_BUILD_JOBS="${AKAOS_GTK_BUILD_JOBS:-4}"

if [[ ! "$GTK_BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_GTK_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for command_name in curl sha256sum tar clang clang++ meson ninja pkg-config patch; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream GTK 4 build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld meson ninja-build pkg-config patch" >&2
        exit 2
    fi
done

# GdkPixbuf's Meson build needs the build-machine resource compiler. A
# cross-compiled akaOS glib-compile-resources cannot run on the Ubuntu host, so
# stage Ubuntu's signed archive package locally instead of installing it into
# the WSL system.
if ! command -v glib-compile-resources >/dev/null 2>&1; then
    for command_name in apt-get dpkg-deb; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            echo "Missing host tool $command_name; install libgio-2.0-dev-bin on Ubuntu." >&2
            exit 2
        fi
    done
    mkdir -p "$HOST_GLIB_TOOLS"
    if [[ ! -x "$HOST_GLIB_TOOLS/root/usr/bin/glib-compile-resources" ]]; then
        (cd "$HOST_GLIB_TOOLS" && apt-get download libgio-2.0-dev-bin)
        dpkg-deb -x "$HOST_GLIB_TOOLS"/libgio-2.0-dev-bin_*.deb \
            "$HOST_GLIB_TOOLS/root"
    fi
    export PATH="$HOST_GLIB_TOOLS/root/usr/bin:$PATH"
fi

# Keep target library/header paths from the cross sysroot, but make Meson's
# build-time GIO resource compiler resolve to the native Ubuntu host program.
mkdir -p "$HOST_GLIB_PKGCONFIG"
cp "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" \
    "$HOST_GLIB_PKGCONFIG/glib-2.0.pc"
cp "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gio-2.0.pc" \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_compile_resources=.*#glib_compile_resources=glib-compile-resources#' \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_compile_schemas=.*#glib_compile_schemas=glib-compile-schemas#' \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/pango.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/cairo-gobject.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libcairo-gobject.so.2.11806.6" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/wayland-client.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/wayland-cursor.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/wayland-egl.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/xkbcommon.pc" \
    "$GRAPHICS_SYSROOT/usr/local/share/pkgconfig/wayland-protocols.pc"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS upstream GTK 4 prerequisite: $required_file" >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/gtk-4.22.5.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://download.gnome.org/sources/gtk/4.22/gtk-4.22.5.tar.xz \
        -o "$SOURCE_DIR/gtk-4.22.5.tar.xz"
fi
printf '%s  %s\n' \
    7fd725deb2cb3f8dc218ad862c5056ff8548f49d3b0e4081796e444c22d19686 \
    "$SOURCE_DIR/gtk-4.22.5.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/gtk-4.22.5.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi
if ! grep -q '#include <linux/input-event-codes.h>' \
    "$SOURCE_TREE/gdk/wayland/gdkseat-wayland.c"; then
    patch -d "$SOURCE_TREE" -p1 < "$ROOT_DIR/tools/gtk-wayland-input-codes.patch"
fi
if ! grep -q '^typedef unsigned int uint;$' \
    "$SOURCE_TREE/gdk/wayland/gdkwaylandcolor.c"; then
    patch -d "$SOURCE_TREE" -p1 < "$ROOT_DIR/tools/gtk-wayland-uint.patch"
fi
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/include/epoxy/egl.h" ||
      ! -f "$GRAPHICS_SYSROOT/usr/local/include/epoxy/egl_generated.h" ]]; then
    echo "Building libepoxy's EGL dispatch headers required by GTK's Wayland backend"
    bash "$ROOT_DIR/tools/build-upstream-epoxy.sh"
fi

export PKG_CONFIG_LIBDIR="$HOST_GLIB_PKGCONFIG:$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export PATH="$WAYLAND_HOST_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$WAYLAND_HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH_FOR_BUILD="$WAYLAND_HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR_FOR_BUILD=""
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
export CXXFLAGS="-nostdinc -nostdinc++ -I$BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/include/c++/v1 -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Dx11-backend=false -Dwayland-backend=true -Dbroadway-backend=true
    -Dmedia-gstreamer=disabled -Dprint-cups=disabled -Dprint-cpdb=disabled
    -Dvulkan=disabled -Dcloudproviders=disabled -Dsysprof=disabled
    -Dtracker=disabled -Dcolord=disabled -Daccesskit=disabled
    -Dintrospection=disabled -Ddocumentation=false -Dscreenshots=false
    -Dman-pages=false -Dbuild-demos=false -Dbuild-testsuite=false
    -Dbuild-examples=false -Dbuild-tests=false
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" && -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi

# libepoxy includes the BSD err(3) header in this file even though no err(3)
# function is used anywhere in the source. akaOS is not BSD and does not expose
# that header; keep the upstream code otherwise unchanged with this tiny port
# patch, then let Meson compile its own selected GLX/EGL/Broadway-independent
# dispatch implementation.
if [[ -f "$SOURCE_TREE/subprojects/libepoxy/src/dispatch_common.c" ]] &&
   grep -q '^#include <err.h>$' "$SOURCE_TREE/subprojects/libepoxy/src/dispatch_common.c"; then
    patch -d "$SOURCE_TREE/subprojects/libepoxy" -p1 \
        < "$ROOT_DIR/tools/libepoxy-akaos.patch"
fi

meson compile -C "$BUILD_TREE" --jobs "$GTK_BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"
if [[ ! -f "$BUILD_TREE/gtk/libgtk-4.so.1.2200.5" ]]; then
    echo "Upstream GTK 4.22.5 did not produce its expected library" >&2
    exit 1
fi
touch "$BUILD_TREE/.akaos-wayland-enabled"

echo "[DONE] Upstream GTK 4.22.5 x86_64 build: $BUILD_TREE"
