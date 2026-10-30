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
VERSION=1.13.2
TAG="xkbcommon-$VERSION"
SOURCE_DIR="$BUILD_DIR/upstream-source/libxkbcommon-$VERSION"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/libxkbcommon-$VERSION/x86_64-akaos"
HOST_TOOLS_DIR="$BUILD_DIR/host-tools/xkbcommon-$VERSION"
HOST_TOOLS_ROOT="$HOST_TOOLS_DIR/root"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
JOBS="${AKAOS_XKBCOMMON_BUILD_JOBS:-4}"

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_XKBCOMMON_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for command_name in curl sha256sum tar clang meson ninja pkg-config dpkg-deb apt-get patch; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required xkbcommon build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld meson ninja-build pkg-config dpkg-dev" >&2
        exit 2
    fi
done

# Bison is a build-machine generator; stage Ubuntu's signed packages locally
# instead of installing tools or host libraries into the target sysroot.
if ! command -v bison >/dev/null 2>&1 || ! command -v m4 >/dev/null 2>&1; then
    mkdir -p "$HOST_TOOLS_DIR"
    if [[ ! -x "$HOST_TOOLS_ROOT/usr/bin/bison" ||
          ! -x "$HOST_TOOLS_ROOT/usr/bin/m4" ]]; then
        (cd "$HOST_TOOLS_DIR" && apt-get download bison m4)
        mkdir -p "$HOST_TOOLS_ROOT"
        for package in "$HOST_TOOLS_DIR"/bison_*.deb "$HOST_TOOLS_DIR"/m4_*.deb; do
            [[ -f "$package" ]] || continue
            dpkg-deb -x "$package" "$HOST_TOOLS_ROOT"
        done
    fi
    export PATH="$HOST_TOOLS_ROOT/usr/bin:$PATH"
    export LD_LIBRARY_PATH="$HOST_TOOLS_ROOT/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export BISON_PKGDATADIR="$HOST_TOOLS_ROOT/usr/share/bison"
    export M4="$HOST_TOOLS_ROOT/usr/bin/m4"
fi
if ! command -v bison >/dev/null 2>&1 || ! command -v m4 >/dev/null 2>&1; then
    echo "xkbcommon requires host bison >= 3.6 and m4; install Ubuntu packages bison and m4." >&2
    exit 2
fi

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/libxkbcommon-$VERSION.tar.gz" ]]; then
    curl -fL --retry 2 \
        "https://codeload.github.com/xkbcommon/libxkbcommon/tar.gz/refs/tags/$TAG" \
        -o "$SOURCE_DIR/libxkbcommon-$VERSION.tar.gz"
fi
printf '%s  %s\n' \
    acc4d5f7c3cbba5f9f8d08d8bdbeede84ecede46792f47929aa9321873385528 \
    "$SOURCE_DIR/libxkbcommon-$VERSION.tar.gz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xzf "$SOURCE_DIR/libxkbcommon-$VERSION.tar.gz" \
        -C "$SOURCE_TREE" --strip-components=1
fi
if ! grep -q '^#include <inttypes.h>$' "$SOURCE_TREE/src/xkbcomp/expr.c"; then
    patch -d "$SOURCE_TREE" -p1 < "$ROOT_DIR/tools/libxkbcommon-akaos.patch"
fi

for required_file in "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS xkbcommon prerequisite: $required_file" >&2
        exit 2
    fi
done

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Denable-tools=false -Denable-x11=false
    -Denable-wayland=false -Denable-xkbregistry=false
    -Denable-docs=false -Denable-bash-completion=false
    -Dxkb-config-root=/usr/share/X11/xkb
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" &&
      -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
# Upstream registers its test programs unconditionally and has no Meson
# `tests` option. Build and install the production library target only; its
# own full test suite is a separate porting task.
ninja -C "$BUILD_TREE" -j "$JOBS" libxkbcommon.so.0.13.2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE" --no-rebuild

if [[ ! -f "$BUILD_TREE/libxkbcommon.so.0.13.2" ||
      ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/xkbcommon.pc" ]]; then
    echo "Upstream libxkbcommon $VERSION build or pkg-config install is incomplete" >&2
    exit 1
fi

echo "[DONE] Upstream libxkbcommon $VERSION x86_64 DSO: $BUILD_TREE/libxkbcommon.so.0.13.2"
