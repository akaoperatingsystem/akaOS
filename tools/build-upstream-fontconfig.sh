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
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
GPERF_SOURCE="$BUILD_DIR/upstream-source/gperf-3.3/extracted"
GPERF_ARCHIVE="$BUILD_DIR/upstream-source/gperf-3.3/gperf-3.3.tar.gz"
GPERF_BUILD="$BUILD_DIR/upstream-build/gperf-3.3/host"
GPERF_PREFIX="$BUILD_DIR/host-tools/gperf-3.3"
MESON_WHEEL_DIR="$BUILD_DIR/upstream-source/meson-1.11.0"
MESON_PREFIX="$BUILD_DIR/host-tools/meson-1.11.0"
FONTCONFIG_SOURCE="$BUILD_DIR/upstream-source/fontconfig-2.18.3"
FONTCONFIG_TREE="$FONTCONFIG_SOURCE/extracted"
FONTCONFIG_BUILD="$BUILD_DIR/upstream-build/fontconfig-2.18.3/x86_64-akaos-current-libc"

for command_name in curl sha256sum tar cmake make g++ clang meson ninja pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream Fontconfig build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar cmake build-essential clang meson ninja-build pkg-config" >&2
        exit 2
    fi
done

for required_file in \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libfreetype.so.6.20.6"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS graphics prerequisite: $required_file" >&2
        echo "Build the x86_64 libc and upstream FreeType stack first." >&2
        exit 2
    fi
done

mkdir -p "$GPERF_SOURCE" "$GPERF_BUILD" "$GPERF_PREFIX"
if [[ ! -f "$GPERF_ARCHIVE" ]]; then
    curl -fL --retry 2 https://ftp.gnu.org/gnu/gperf/gperf-3.3.tar.gz \
        -o "$GPERF_ARCHIVE"
fi
printf '%s  %s\n' \
    fd87e0aba7e43ae054837afd6cd4db03a3f2693deb3619085e6ed9d8d9604ad8 \
    "$GPERF_ARCHIVE" | sha256sum -c -
if [[ ! -f "$GPERF_SOURCE/configure" ]]; then
    tar -xf "$GPERF_ARCHIVE" -C "$GPERF_SOURCE" --strip-components=1
fi
if [[ ! -x "$GPERF_PREFIX/bin/gperf" ]]; then
    mkdir -p "$GPERF_BUILD"
    (cd "$GPERF_BUILD" && "$GPERF_SOURCE/configure" \
        --prefix="$GPERF_PREFIX" --disable-nls)
    make -C "$GPERF_BUILD" --jobs 2
    make -C "$GPERF_BUILD" install
fi
export PATH="$GPERF_PREFIX/bin:$PATH"

if ! python3 -m pip --version >/dev/null 2>&1; then
    echo "Python pip is required to provide Meson 1.11.0 for Fontconfig 2.18.3." >&2
    echo "On Ubuntu: sudo apt install python3-pip" >&2
    exit 2
fi
mkdir -p "$MESON_WHEEL_DIR" "$MESON_PREFIX"
if [[ ! -f "$MESON_WHEEL_DIR/meson-1.11.0-py3-none-any.whl" ]]; then
    python3 -m pip download --no-deps --only-binary=:all: \
        --dest "$MESON_WHEEL_DIR" meson==1.11.0
fi
printf '%s  %s\n' \
    9774eb62bbf2cb8043aa3feb771c09b510d112c0c382221e29aea7453ecce753 \
    "$MESON_WHEEL_DIR/meson-1.11.0-py3-none-any.whl" | sha256sum -c -
if [[ ! -f "$MESON_PREFIX/mesonbuild/mesonmain.py" ]]; then
    python3 -m pip install --no-deps --no-index \
        --find-links "$MESON_WHEEL_DIR" --target "$MESON_PREFIX" \
        meson==1.11.0
fi
export PYTHONPATH="$MESON_PREFIX${PYTHONPATH:+:$PYTHONPATH}"
export PATH="$MESON_PREFIX/bin:$PATH"
if [[ "$(meson --version)" != "1.11.0" ]]; then
    echo "Fontconfig requires the pinned Meson 1.11.0 host tool." >&2
    exit 2
fi

bash "$ROOT_DIR/tools/build-upstream-expat.sh"

mkdir -p "$FONTCONFIG_SOURCE" "$FONTCONFIG_TREE" "$FONTCONFIG_BUILD"
if [[ ! -f "$FONTCONFIG_SOURCE/fontconfig-2.18.3.tar.xz" ]]; then
    curl -fL --retry 2 \
        https://gitlab.freedesktop.org/api/v4/projects/890/packages/generic/fontconfig/2.18.3/fontconfig-2.18.3.tar.xz \
        -o "$FONTCONFIG_SOURCE/fontconfig-2.18.3.tar.xz"
fi
printf '%s  %s\n' \
    4f7b554a38cdf78c033f666c8871f3749e14a094f65a07f630c91ed0b43d35e3 \
    "$FONTCONFIG_SOURCE/fontconfig-2.18.3.tar.xz" | sha256sum -c -
if [[ ! -f "$FONTCONFIG_TREE/meson.build" ]]; then
    tar -xf "$FONTCONFIG_SOURCE/fontconfig-2.18.3.tar.xz" \
        -C "$FONTCONFIG_TREE" --strip-components=1
fi

export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--no-undefined -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"
export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini"
    -Ddefault_library=shared -Dbuildtype=release
    -Dxml-backend=expat -Dfontations=disabled -Diconv=disabled
    -Dtests=disabled -Dtests-bwrap=disabled -Dtests-external-fonts=disabled
    -Dtools=disabled -Ddoc=disabled -Ddoc-txt=disabled -Ddoc-man=disabled
    -Ddoc-pdf=disabled -Ddoc-html=disabled -Dnls=disabled
    -Dcache-build=disabled -Dcache-dir=/tmp/fontconfig-cache
    -Dbaseconfig-dir=/etc/fonts -Dconfig-dir=/etc/fonts/conf.d
    -Dtemplate-dir=/usr/share/fontconfig/conf.avail
    -Dxml-dir=/usr/share/xml/fontconfig
    -Dadditional-fonts-dirs=no
)
if [[ -f "$FONTCONFIG_BUILD/meson-private/coredata.dat" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$FONTCONFIG_BUILD" "$FONTCONFIG_TREE"
else
    meson setup "${meson_args[@]}" "$FONTCONFIG_BUILD" "$FONTCONFIG_TREE"
fi
meson compile -C "$FONTCONFIG_BUILD" --jobs 2
DESTDIR="$GRAPHICS_SYSROOT" meson install -C "$FONTCONFIG_BUILD"

echo "[DONE] Upstream Fontconfig 2.18.3 built for akaOS x86_64: $FONTCONFIG_BUILD"
