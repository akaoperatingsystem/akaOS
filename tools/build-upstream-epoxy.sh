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
SOURCE_TREE="$BUILD_DIR/upstream-source/gtk-4.22.5/extracted/subprojects/libepoxy"
BUILD_TREE="$BUILD_DIR/upstream-build/libepoxy-1.5.11/x86_64-akaos-egl-v2"
PROJECT_SOURCE_TREE="$SOURCE_TREE"
PROJECT_BUILD_TREE="$BUILD_TREE"
if [[ -n "${AKAOS_EPOXY_WORKROOT:-}" ]]; then
    SOURCE_TREE="$AKAOS_EPOXY_WORKROOT/source"
    BUILD_TREE="$AKAOS_EPOXY_WORKROOT/build"
    mkdir -p "$AKAOS_EPOXY_WORKROOT"
    if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
        mkdir -p "$SOURCE_TREE"
        cp -a "$PROJECT_SOURCE_TREE/." "$SOURCE_TREE/"
    fi
fi
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
EGL_HEADERS_DIR="$BUILD_DIR/upstream-source/egl-registry-headers"
EGL_PLATFORM_RAW="$EGL_HEADERS_DIR/eglplatform.upstream.h"
EGL_PLATFORM="$EGL_HEADERS_DIR/eglplatform.h"
JOBS="${AKAOS_EPOXY_BUILD_JOBS:-4}"

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_EPOXY_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for command_name in curl sha256sum install cp clang meson ninja pkg-config patch; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream libepoxy build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld meson ninja-build pkg-config patch" >&2
        exit 2
    fi
done
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    echo "Missing libepoxy source bundled with the verified GTK 4.22.5 source archive" >&2
    exit 2
fi
for required_file in "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS libepoxy prerequisite: $required_file" >&2
        exit 2
    fi
done

# libepoxy's generated EGL dispatch header consumes the upstream Khronos
# platform headers. Stage only those headers, with hashes pinned to the exact
# official registry files; no host EGL runtime or library is used.
mkdir -p "$EGL_HEADERS_DIR"
if [[ ! -f "$EGL_PLATFORM_RAW" ]]; then
    curl -fL --retry 2 \
        https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/main/api/EGL/eglplatform.h \
        -o "$EGL_PLATFORM_RAW"
fi
if [[ ! -f "$EGL_HEADERS_DIR/khrplatform.h" ]]; then
    curl -fL --retry 2 \
        https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/main/api/KHR/khrplatform.h \
        -o "$EGL_HEADERS_DIR/khrplatform.h"
fi
printf '%s  %s\n' \
    25b5391655effcf363a38fa00c3154f356a3eab3d35dc067847875513cf1e441 \
    "$EGL_PLATFORM_RAW" | sha256sum -c -
printf '%s  %s\n' \
    7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83 \
    "$EGL_HEADERS_DIR/khrplatform.h" | sha256sum -c -
cp "$EGL_PLATFORM_RAW" "$EGL_PLATFORM"
if ! grep -q '__AKAOS__' "$EGL_PLATFORM"; then
    patch -d "$EGL_HEADERS_DIR" -p0 < "$ROOT_DIR/tools/eglplatform-akaos.patch"
fi
install -D -m 0644 "$EGL_HEADERS_DIR/eglplatform.h" \
    "$GRAPHICS_SYSROOT/usr/local/include/EGL/eglplatform.h"
install -D -m 0644 "$EGL_HEADERS_DIR/khrplatform.h" \
    "$GRAPHICS_SYSROOT/usr/local/include/KHR/khrplatform.h"

# GTK's Wayland headers reference epoxy/egl.h even when a client selects its
# Cairo renderer. Build the upstream EGL dispatch declarations; this does not
# provide a hardware EGL implementation or enable X11/GLX.
if [[ -f "$SOURCE_TREE/src/dispatch_common.c" ]] &&
   grep -q '^#include <err.h>$' "$SOURCE_TREE/src/dispatch_common.c"; then
    patch -d "$SOURCE_TREE" -p1 < "$ROOT_DIR/tools/libepoxy-akaos.patch"
fi

mkdir -p "$BUILD_TREE"
# The xproto and kbproto package descriptions live beside the shared package
# descriptions, and the X11 package description needs them.
export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

# The GTK 3 X11 backend includes the GLX dispatch header, so the GLX
# declarations have to be built.  libepoxy generates them from its own
# registry and only needs the Xlib headers, not a GL library.
meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Degl=yes -Dglx=yes -Dx11=true -Dtests=false -Ddocs=false
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" &&
      -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs "$JOBS"
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

for required_file in \
    "$GRAPHICS_SYSROOT/usr/local/include/epoxy/egl.h" \
    "$GRAPHICS_SYSROOT/usr/local/include/epoxy/egl_generated.h" \
    "$GRAPHICS_SYSROOT/usr/local/include/epoxy/glx.h" \
    "$GRAPHICS_SYSROOT/usr/local/include/epoxy/glx_generated.h" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libepoxy.so.0.0.0"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Upstream libepoxy dispatch build is missing: $required_file" >&2
        exit 1
    fi
done

echo "[DONE] Upstream libepoxy EGL and GLX API dispatch built for the GTK backends"
