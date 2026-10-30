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
VERSION=1.26.0
SOURCE_DIR="$BUILD_DIR/upstream-source/wayland-$VERSION"
SOURCE_TREE="$SOURCE_DIR/extracted"
NATIVE_BUILD="$BUILD_DIR/upstream-build/wayland-$VERSION/native-scanner"
TARGET_BUILD="$BUILD_DIR/upstream-build/wayland-$VERSION/x86_64-akaos-wayland"
HOST_PREFIX="$BUILD_DIR/host-tools/wayland-$VERSION"
HOST_EXPAT_DIR="$BUILD_DIR/host-tools/wayland-$VERSION/expat-dev"
HOST_EXPAT_ROOT="$HOST_EXPAT_DIR/root"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
JOBS="${AKAOS_WAYLAND_BUILD_JOBS:-4}"

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_WAYLAND_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for command_name in curl sha256sum tar clang gcc meson ninja pkg-config dpkg-deb apt-get python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required upstream Wayland build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang gcc meson ninja-build pkg-config dpkg-dev python3" >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$SOURCE_TREE" "$NATIVE_BUILD" "$TARGET_BUILD" \
    "$HOST_PREFIX" "$HOST_EXPAT_DIR"
if [[ ! -f "$SOURCE_DIR/wayland-$VERSION.tar.xz" ]]; then
    curl -fL --retry 2 \
        "https://gitlab.freedesktop.org/wayland/wayland/-/releases/$VERSION/downloads/wayland-$VERSION.tar.xz" \
        -o "$SOURCE_DIR/wayland-$VERSION.tar.xz"
fi
printf '%s  %s\n' \
    64176eaa46e4969903e286f8e5ef8331affc17fdf03ac9b58381d2b23162b7a3 \
    "$SOURCE_DIR/wayland-$VERSION.tar.xz" | sha256sum -c -
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    tar -xf "$SOURCE_DIR/wayland-$VERSION.tar.xz" \
        -C "$SOURCE_TREE" --strip-components=1
fi

python3 "$ROOT_DIR/tools/patch-upstream-wayland.py" "$SOURCE_TREE"

# Build-machine scanner tools are native executables. Stage Ubuntu's signed
# expat development archive locally instead of adding host libraries to the
# akaOS target sysroot or installing packages into the user's WSL system.
if [[ ! -f "$HOST_EXPAT_ROOT/usr/include/expat.h" ||
      ! -f "$HOST_EXPAT_ROOT/usr/lib/x86_64-linux-gnu/pkgconfig/expat.pc" ||
      ! -e "$HOST_EXPAT_ROOT/usr/lib/x86_64-linux-gnu/libexpat.so" ]]; then
    for command_name in apt-get dpkg-deb; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            echo "Missing host tool $command_name; install dpkg-dev and stage libexpat1-dev." >&2
            exit 2
        fi
    done
    (cd "$HOST_EXPAT_DIR" && apt-get download libexpat1-dev)
    mkdir -p "$HOST_EXPAT_ROOT"
    dpkg-deb -x "$HOST_EXPAT_DIR"/libexpat1-dev_*.deb "$HOST_EXPAT_ROOT"
fi

HOST_EXPAT_PC="$HOST_EXPAT_ROOT/usr/lib/x86_64-linux-gnu/pkgconfig"
HOST_EXPAT_CFLAGS="-I$HOST_EXPAT_ROOT/usr/include"
HOST_EXPAT_LDFLAGS="-L$HOST_EXPAT_ROOT/usr/lib/x86_64-linux-gnu"
native_pkg_env=(
    "PKG_CONFIG_PATH=$HOST_EXPAT_PC"
    "CFLAGS=$HOST_EXPAT_CFLAGS"
    "LDFLAGS=$HOST_EXPAT_LDFLAGS"
)
native_meson_args=(
    "--prefix=$HOST_PREFIX" --libdir=lib --bindir=bin
    -Dlibraries=false -Dscanner=true -Dtests=false
    -Ddocumentation=false -Ddtd_validation=false
)
if [[ -f "$NATIVE_BUILD/meson-private/coredata.dat" &&
      -f "$NATIVE_BUILD/build.ninja" ]]; then
    env "${native_pkg_env[@]}" meson setup --reconfigure \
        "${native_meson_args[@]}" "$NATIVE_BUILD" "$SOURCE_TREE"
else
    env "${native_pkg_env[@]}" meson setup \
        "${native_meson_args[@]}" "$NATIVE_BUILD" "$SOURCE_TREE"
fi
env "${native_pkg_env[@]}" meson compile -C "$NATIVE_BUILD" --jobs "$JOBS"
env "${native_pkg_env[@]}" meson install -C "$NATIVE_BUILD"

# pkg-config applies PKG_CONFIG_SYSROOT_DIR to absolute variable values. Return
# the command name instead and let Meson's build-machine PATH resolve our
# staged host scanner without prefixing the akaOS target sysroot.
sed -i 's#^wayland_scanner=.*#wayland_scanner=wayland-scanner#' \
    "$HOST_PREFIX/lib/pkgconfig/wayland-scanner.pc"

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/libffi.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/libffi.so.8"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS upstream Wayland prerequisite: $required_file" >&2
        exit 2
    fi
done

export PATH="$HOST_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH_FOR_BUILD="$HOST_PREFIX/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export PKG_CONFIG_SYSROOT_DIR_FOR_BUILD=""
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

target_meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Dscanner=false -Dlibraries=true -Dtests=false
    -Ddocumentation=false -Ddtd_validation=false
)
if [[ -f "$TARGET_BUILD/meson-private/coredata.dat" &&
      -f "$TARGET_BUILD/build.ninja" ]]; then
    meson setup --reconfigure "${target_meson_args[@]}" \
        "$TARGET_BUILD" "$SOURCE_TREE"
else
    meson setup "${target_meson_args[@]}" "$TARGET_BUILD" "$SOURCE_TREE"
fi
meson compile -C "$TARGET_BUILD" --jobs "$JOBS"
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$TARGET_BUILD"

echo "[DONE] Upstream Wayland $VERSION x86_64 DSOs: $TARGET_BUILD"
