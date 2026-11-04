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
SOURCE_DIR="$BUILD_DIR/upstream-source"
SYSROOT_DIR="$BUILD_DIR/upstream-sysroot"
PIXMAN_SOURCE="$SOURCE_DIR/pixman-0.46.4"
CAIRO_SOURCE="$SOURCE_DIR/cairo-1.18.6"
PIXMAN_BUILD="$BUILD_DIR/upstream-build/pixman-0.46.4/x86_64-akaos"
CAIRO_BUILD="$BUILD_DIR/upstream-build/cairo-1.18.6/x86_64-akaos"
PIXMAN_SYSROOT="$SYSROOT_DIR/pixman-0.46.4-x86_64"
CAIRO_SYSROOT="$SYSROOT_DIR/cairo-1.18.6-x86_64"

for command_name in curl sha256sum sha512sum tar clang meson ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang meson ninja-build" >&2
        exit 2
    fi
done

for required_file in \
    "$BUILD_DIR/libc.so.6" \
    "$BUILD_DIR/libm.so.6" \
    "$BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$SYSROOT_DIR/glib-x86_64/usr/lib/libiconv.a" \
    "$SYSROOT_DIR/pcre2-10.49-x86_64/usr/lib/libpcre2-8.so.0.16.1"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS x86_64/upstream prerequisite: $required_file" >&2
        echo "Build akaOS x86_64 and upstream GLib/PCRE2 first." >&2
        exit 2
    fi
done

mkdir -p "$PIXMAN_SOURCE" "$CAIRO_SOURCE" "$PIXMAN_BUILD" "$CAIRO_BUILD"

if [[ ! -f "$PIXMAN_SOURCE/pixman-0.46.4.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://cairographics.org/releases/pixman-0.46.4.tar.xz \
        -o "$PIXMAN_SOURCE/pixman-0.46.4.tar.xz"
fi
printf '%s  %s\n' \
    83b133e7969ba34f883f4e08dcc5d388c4397f43ce836c191c05945fe77c16ff501d531600780c12678a0d08105828a6bdeff2156b63f9c1a84087bc7f40ae9f \
    "$PIXMAN_SOURCE/pixman-0.46.4.tar.xz" | sha512sum -c -
if [[ ! -f "$PIXMAN_SOURCE/meson.build" ]]; then
    tar -xf "$PIXMAN_SOURCE/pixman-0.46.4.tar.xz" \
        -C "$PIXMAN_SOURCE" --strip-components=1
fi

if [[ ! -f "$CAIRO_SOURCE/cairo-1.18.6.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://cairographics.org/releases/cairo-1.18.6.tar.xz \
        -o "$CAIRO_SOURCE/cairo-1.18.6.tar.xz"
fi
printf '%s  %s\n' \
    1c767308174337a74694da0f3ec069c271452163a1ef4540964c50c301f157d4 \
    "$CAIRO_SOURCE/cairo-1.18.6.tar.xz" | sha256sum -c -
if [[ ! -f "$CAIRO_SOURCE/meson.build" ]]; then
    tar -xf "$CAIRO_SOURCE/cairo-1.18.6.tar.xz" \
        -C "$CAIRO_SOURCE" --strip-components=1
fi

# Match the freestanding x86_64 ABI used by GLib/GIO, and route all package
# discovery through the target Pixman sysroot rather than host libraries.
export CFLAGS="-nostdinc -I/usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$SYSROOT_DIR/glib-x86_64/usr/include -I$SYSROOT_DIR/pcre2-10.49-x86_64/usr/include -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-e,main -Wl,--no-as-needed -L$SYSROOT_DIR/glib-x86_64/usr/lib -L$SYSROOT_DIR/pcre2-10.49-x86_64/usr/lib -L$PIXMAN_SYSROOT/usr/local/lib -L$BUILD_DIR -liconv $BUILD_DIR/libc.so.6 $BUILD_DIR/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a"

# The Xlib surface needs the upstream X11 client libraries, which live in the
# graphics sysroot next to the other desktop libraries.
GRAPHICS_SYSROOT="$SYSROOT_DIR/graphics-x86_64"

# Cairo includes the System V shared memory headers unconditionally, while
# the code that uses them stays behind the protocol header checks.  akaOS has
# no System V shared memory, so the build provides empty headers for cairo
# alone and drops the two protocol header checks, which keeps the shared
# memory code out of the library.
CAIRO_STUB_INCLUDE="$BUILD_DIR/cairo-x11-stub-include"
mkdir -p "$CAIRO_STUB_INCLUDE/sys"
cat > "$CAIRO_STUB_INCLUDE/sys/ipc.h" <<'STUB'
#ifndef _AKAOS_SYS_IPC_H_
#define _AKAOS_SYS_IPC_H_
#endif
STUB
cat > "$CAIRO_STUB_INCLUDE/sys/shm.h" <<'STUB'
#ifndef _AKAOS_SYS_SHM_H_
#define _AKAOS_SYS_SHM_H_
#endif
STUB
if ! grep -q "akaOS has no System V shared memory" \
    "$CAIRO_SOURCE/meson.build" 2>/dev/null; then
    if ! patch -d "$CAIRO_SOURCE" -p1 --forward \
        < "$ROOT_DIR/tools/cairo-x11-no-sysv-shm.patch"; then
        echo "Upstream Cairo shared memory patch did not apply" >&2
        exit 1
    fi
fi

export CFLAGS="$CFLAGS -I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2 -I$CAIRO_STUB_INCLUDE"
export LDFLAGS="$LDFLAGS -L$GRAPHICS_SYSROOT/usr/local/lib"

if [[ ! -f "$PIXMAN_BUILD/meson-private/coredata.dat" ]]; then
    meson setup --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini" \
        -Ddefault_library=shared -Dbuildtype=release \
        -Dtests=disabled -Ddemos=disabled \
        "$PIXMAN_BUILD" "$PIXMAN_SOURCE"
fi
meson compile -C "$PIXMAN_BUILD" --jobs 2
DESTDIR="$PIXMAN_SYSROOT" meson install -C "$PIXMAN_BUILD"

export PKG_CONFIG_LIBDIR="$PIXMAN_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$PIXMAN_SYSROOT"
CAIRO_MESON_ARGS=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini"
    -Ddefault_library=shared -Dbuildtype=release -Dtests=disabled
    -Dfontconfig=enabled -Dfreetype=enabled -Dpng=disabled
    -Dzlib=enabled -Dquartz=disabled -Dtee=disabled -Dxcb=enabled
    -Dxlib=enabled -Dxlib-xcb=enabled -Dglib=disabled
    -Ddwrite=disabled -Dspectre=disabled -Dlzo=disabled
    -Dsymbol-lookup=disabled -Dgtk_doc=false
)
if [[ -f "$CAIRO_BUILD/meson-private/coredata.dat" &&
      -f "$CAIRO_BUILD/build.ninja" ]]; then
    meson setup --reconfigure "${CAIRO_MESON_ARGS[@]}" \
        "$CAIRO_BUILD" "$CAIRO_SOURCE"
else
    meson setup "${CAIRO_MESON_ARGS[@]}" \
        "$CAIRO_BUILD" "$CAIRO_SOURCE"
fi
meson compile -C "$CAIRO_BUILD" --jobs 2
DESTDIR="$CAIRO_SYSROOT" meson install -C "$CAIRO_BUILD"
if [[ ! -f "$CAIRO_SYSROOT/usr/local/include/cairo/cairo-pdf.h" ]]; then
    echo "Cairo PDF surface support was enabled but cairo-pdf.h was not installed" >&2
    exit 1
fi

echo "[DONE] Upstream Pixman 0.46.4 and Cairo 1.18.6 x86_64 DSOs:"
echo "       $PIXMAN_BUILD/pixman/libpixman-1.so.0.46.4"
echo "       $CAIRO_BUILD/src/libcairo.so.2.11806.6"
echo "Run ./build.sh --arch x86_64 --firmware both --disk-type iso to package them."
