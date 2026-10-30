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
SOURCE_DIR="$BUILD_DIR/upstream-source/gtk-3.24.52"
PROJECT_SOURCE_TREE="$SOURCE_DIR/extracted"
PROJECT_BUILD_TREE="$BUILD_DIR/upstream-build/gtk-3.24.52/x86_64-akaos-wayland"
if [[ -n "${AKAOS_GTK3_WORKROOT:-}" ]]; then
    SOURCE_TREE="$AKAOS_GTK3_WORKROOT/source"
    BUILD_TREE="$AKAOS_GTK3_WORKROOT/build"
else
    SOURCE_TREE="$PROJECT_SOURCE_TREE"
    BUILD_TREE="$PROJECT_BUILD_TREE"
fi
HOST_GLIB_TOOLS="$BUILD_DIR/host-tools/glib-2.88"
HOST_GLIB_PKGCONFIG="$BUILD_DIR/host-tools/gtk4-glib-pkgconfig"
WAYLAND_HOST_PREFIX="$BUILD_DIR/host-tools/wayland-1.26.0"
GTK_BUILD_JOBS="${AKAOS_GTK3_BUILD_JOBS:-4}"

if [[ ! "$GTK_BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_GTK3_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for command_name in curl sha256sum tar clang meson ninja pkg-config patch python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream GTK 3 build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld meson ninja-build pkg-config patch python3" >&2
        exit 2
    fi
done

bash "$ROOT_DIR/tools/build-upstream-atk.sh"
# The X11 backend of GTK 3 links against the accessibility bridge, which
# at-spi2-core provides.  It also vendors its own ATK, and the bridge needs
# the symbols that ATK only grew later, so it has to be built after the
# standalone ATK package.
bash "$ROOT_DIR/tools/build-upstream-at-spi2-core.sh"
mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/gtk-3.24.52.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://download.gnome.org/sources/gtk/3.24/gtk-3.24.52.tar.xz \
        -o "$SOURCE_DIR/gtk-3.24.52.tar.xz"
fi
printf '%s  %s\n' \
    80931fa472a77b9a164f6740e3c0b444fac6770054632d35a7ff9d679e5e7b9f \
    "$SOURCE_DIR/gtk-3.24.52.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/gtk-3.24.52.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi
if ! grep -q '#include <linux/input-event-codes.h>' \
    "$SOURCE_TREE/gdk/wayland/gdkdevice-wayland.c"; then
    patch --fuzz=3 -d "$SOURCE_TREE" -p1 \
        < "$ROOT_DIR/tools/gtk3-wayland-input-codes.patch"
fi
if grep -q '#include <sys/syscall.h>' \
    "$SOURCE_TREE/gdk/wayland/gdkdisplay-wayland.c"; then
    patch -d "$SOURCE_TREE" -p1 \
        < "$ROOT_DIR/tools/gtk3-wayland-no-direct-syscall.patch"
fi
python3 "$ROOT_DIR/tools/patch-upstream-gtk3.py" "$SOURCE_TREE"

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

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$SYSROOT/usr/local/lib/pkgconfig/atk.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/pango.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/cairo-gobject.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/epoxy.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/gdk-pixbuf-2.0.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/wayland-client.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/wayland-cursor.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/wayland-egl.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xkbcommon.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/x11.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xext.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xi.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xrender.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xcursor.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xdamage.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xfixes.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xcomposite.pc" \
    "$SYSROOT/usr/local/lib/pkgconfig/xrandr.pc" \
    "$SYSROOT/usr/local/share/pkgconfig/wayland-protocols.pc"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing upstream GTK 3 prerequisite: $required_file" >&2
        exit 2
    fi
done

export PKG_CONFIG_LIBDIR="$HOST_GLIB_PKGCONFIG:$SYSROOT/usr/local/lib/pkgconfig:$SYSROOT/usr/local/share/pkgconfig:$SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PATH="$WAYLAND_HOST_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$WAYLAND_HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH_FOR_BUILD="$WAYLAND_HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR_FOR_BUILD=""
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$SYSROOT/usr/local/include/freetype2"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$SYSROOT/usr/lib -L$SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Dx11_backend=true -Dwayland_backend=true -Dbroadway_backend=false
    -Dwin32_backend=false -Dquartz_backend=false
    -Dcloudproviders=false -Dprofiler=false -Dtracker3=false
    -Dcolord=no -Dxinerama=no -Dprint_backends=file
    -Dgtk_doc=false -Dman=false -Dintrospection=false
    -Ddemos=false -Dexamples=false -Dtests=false -Dinstalled_tests=false
    -Dbuiltin_immodules=all
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" && -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi

meson compile -C "$BUILD_TREE" --jobs "$GTK_BUILD_JOBS"
DESTDIR="$SYSROOT" meson install -C "$BUILD_TREE"
if [[ ! -e "$SYSROOT/usr/local/lib/libgtk-3.so.0" ||
      ! -e "$SYSROOT/usr/local/lib/libgdk-3.so.0" ]]; then
    echo "Upstream GTK 3.24.52 did not install its GTK and GDK shared libraries" >&2
    exit 1
fi
touch "$BUILD_TREE/.akaos-wayland-enabled"
touch "$BUILD_TREE/.akaos-x11-enabled"
if [[ "$BUILD_TREE" != "$PROJECT_BUILD_TREE" ]]; then
    mkdir -p "$PROJECT_BUILD_TREE"
    touch "$PROJECT_BUILD_TREE/.akaos-wayland-enabled"
    touch "$PROJECT_BUILD_TREE/.akaos-x11-enabled"
fi
echo "[DONE] Upstream GTK 3.24.52 x86_64 Wayland and X11 build: $BUILD_TREE"
