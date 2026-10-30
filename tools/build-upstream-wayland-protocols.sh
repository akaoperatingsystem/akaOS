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
VERSION=1.46
SOURCE_DIR="$BUILD_DIR/upstream-source/wayland-protocols-$VERSION"
SOURCE_TREE="$SOURCE_DIR/extracted"
BUILD_TREE="$BUILD_DIR/upstream-build/wayland-protocols-$VERSION/native-data"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
WAYLAND_HOST_PREFIX="$BUILD_DIR/host-tools/wayland-1.26.0"
JOBS="${AKAOS_WAYLAND_PROTOCOLS_BUILD_JOBS:-4}"

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_WAYLAND_PROTOCOLS_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for command_name in curl sha256sum tar meson ninja pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required Wayland protocols build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar meson ninja-build pkg-config" >&2
        exit 2
    fi
done

if [[ ! -x "$WAYLAND_HOST_PREFIX/bin/wayland-scanner" ||
      ! -f "$WAYLAND_HOST_PREFIX/lib/pkgconfig/wayland-scanner.pc" ]]; then
    echo "Building the pinned native Wayland scanner required by the protocol package"
    bash "$ROOT_DIR/tools/build-upstream-wayland.sh"
fi

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$BUILD_TREE"
if [[ ! -f "$SOURCE_DIR/wayland-protocols-$VERSION.tar.gz" ]]; then
    curl -fL --retry 2 \
        "https://gitlab.freedesktop.org/wayland/wayland-protocols/-/archive/$VERSION/wayland-protocols-$VERSION.tar.gz" \
        -o "$SOURCE_DIR/wayland-protocols-$VERSION.tar.gz"
fi
printf '%s  %s\n' \
    83afedde1e63751578ba51b5f6c36052802f85634ed68be41ecfcc3a515ab03d \
    "$SOURCE_DIR/wayland-protocols-$VERSION.tar.gz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xzf "$SOURCE_DIR/wayland-protocols-$VERSION.tar.gz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

export PATH="$WAYLAND_HOST_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$WAYLAND_HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH_FOR_BUILD="$WAYLAND_HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR_FOR_BUILD=""

meson_args=(
    --prefix=/usr/local --libdir=lib --datadir=share
    -Dtests=false
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" &&
      -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi
meson compile -C "$BUILD_TREE" --jobs "$JOBS"
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$BUILD_TREE"

if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/share/pkgconfig/wayland-protocols.pc" ||
      ! -f "$GRAPHICS_SYSROOT/usr/local/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" ]]; then
    echo "Upstream Wayland protocol data was not installed into the target sysroot" >&2
    exit 1
fi

echo "[DONE] Upstream Wayland protocols $VERSION installed for GTK's cross-build"
