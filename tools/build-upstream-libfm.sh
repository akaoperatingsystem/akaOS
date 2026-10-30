#!/usr/bin/env bash
set -eo pipefail
# Build the unmodified upstream LXDE file-management stack for akaOS x86-64:
#
#   1. libfm-extra   the non-GTK part of libfm, needed by menu-cache
#   2. menu-cache    the application menu cache library
#   3. libfm-gtk3    libfm with its GTK 3 widgets
#
# The LXDE panel's launch bar and the LXDE session use the menu cache and the
# libfm desktop integration; the panel's configure script requires
# libfm-gtk3.  The packages are unmodified upstream release tarballs cross
# built with the same akaOS toolchain wrapper as the X11 stack and installed
# directly into the graphics sysroot under the runtime prefix /usr/local.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
WORKROOT="$AKAOS_LIBFM_WORKROOT"
[[ -n "$WORKROOT" ]] || WORKROOT="$HOME/.cache/akaos/libfm"
JOBS="$AKAOS_LIBFM_BUILD_JOBS"
[[ -n "$JOBS" ]] || JOBS=4
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
LOG_DIR="$WORKROOT/logs"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

LIBFM_VERSION="1.3.2"
LIBFM_SHA256="a5042630304cf8e5d8cff9d565c6bd546f228b48c960153ed366a34e87cad1e5"
LIBFM_URL="https://archive.ubuntu.com/ubuntu/pool/universe/libf/libfm/libfm_1.3.2.orig.tar.xz"
LIBFM_ARCHIVE="libfm-1.3.2.tar.xz"
MENU_CACHE_VERSION="1.1.0"
MENU_CACHE_SHA256="ed02eb459dcb398f69b9fa5bf4dd813020405afc84331115469cdf7be9273ec7"
MENU_CACHE_URL="https://archive.ubuntu.com/ubuntu/pool/universe/m/menu-cache/menu-cache_1.1.0.orig.tar.xz"
MENU_CACHE_ARCHIVE="menu-cache-1.1.0.tar.xz"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream libfm build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld pkg-config make" >&2
        exit 2
    fi
done
if [[ ! -x "$CC_WRAPPER" ]]; then
    echo "Missing cross compiler wrapper: $CC_WRAPPER" >&2
    exit 2
fi
if [[ ! -f "$BUILD_DIR/libc.so.6" || ! -f "$BUILD_DIR/libm.so.6" ]]; then
    echo "Missing shared akaOS libc in $BUILD_DIR; run build.sh first" >&2
    exit 2
fi
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/gtk+-3.0.pc" ]]; then
    echo "Missing upstream GTK 3 in the graphics sysroot; build it first." >&2
    exit 2
fi

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$LOG_DIR"

fetch_verified() {
    local url="$1"
    local archive="$2"
    local expected_sha="$3"
    local name="$4"
    if [[ ! -s "$archive" ]]; then
        echo "[libfm] fetching $name"
        curl -fL --retry 3 --max-time 600 -o "$archive.part" "$url" || { echo "[libfm] download failed: $url" >&2; exit 1; }
        mv "$archive.part" "$archive"
    fi
    local actual_sha
    actual_sha="$(sha256sum "$archive" | cut -d' ' -f1)"
    if [[ "$actual_sha" != "$expected_sha" ]]; then
        echo "[libfm] checksum mismatch for $name" >&2
        echo "  expected $expected_sha" >&2
        echo "  actual   $actual_sha" >&2
        exit 1
    fi
}

extract_archive() {
    local archive="$1"
    local destination="$2"
    if [[ -f "$destination/.akaos-extracted" ]]; then
        return 0
    fi
    rm -rf "$destination"
    mkdir -p "$destination"
    tar -xf "$archive" -C "$destination" --strip-components=1
    touch "$destination/.akaos-extracted"
}

run_configure_build() {
    local name="$1"
    local source_dir="$2"
    local build_dir="$3"
    local extra_cflags="$4"
    shift 4
    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    local -a configure_args=("$@")
    (
        cd "$build_dir" || exit 1
        env CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing $extra_cflags"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$source_dir/configure" --host=x86_64-pc-linux-gnu --prefix=/usr/local --disable-static --enable-shared --disable-nls --disable-dependency-tracking "${configure_args[@]}"
    ) >"$LOG_DIR/$name-configure.log" 2>&1 || {
        echo "[libfm] configure failed for $name; see $LOG_DIR/$name-configure.log" >&2
        tail -50 "$LOG_DIR/$name-configure.log" >&2
        exit 1
    }
    make -C "$build_dir" -j"$JOBS" >"$LOG_DIR/$name-build.log" 2>&1 || {
        echo "[libfm] build failed for $name; see $LOG_DIR/$name-build.log" >&2
        tail -60 "$LOG_DIR/$name-build.log" >&2
        exit 1
    }
    make -C "$build_dir" install DESTDIR="$GRAPHICS_SYSROOT" >"$LOG_DIR/$name-install.log" 2>&1 || {
        echo "[libfm] install failed for $name; see $LOG_DIR/$name-install.log" >&2
        tail -40 "$LOG_DIR/$name-install.log" >&2
        exit 1
    }
    echo "[libfm] staged $name"
}

fetch_verified "$LIBFM_URL" "$SRC_DIR/$LIBFM_ARCHIVE" "$LIBFM_SHA256" "libfm $LIBFM_VERSION"
fetch_verified "$MENU_CACHE_URL" "$SRC_DIR/$MENU_CACHE_ARCHIVE" "$MENU_CACHE_SHA256" "menu-cache $MENU_CACHE_VERSION"
extract_archive "$SRC_DIR/$LIBFM_ARCHIVE" "$SRC_DIR/libfm-$LIBFM_VERSION"
extract_archive "$SRC_DIR/$MENU_CACHE_ARCHIVE" "$SRC_DIR/menu-cache-$MENU_CACHE_VERSION"

echo "[libfm] libfm-extra $LIBFM_VERSION"
run_configure_build libfm-extra "$SRC_DIR/libfm-$LIBFM_VERSION" "$BUILD_ROOT/libfm-extra" "" --with-extra-only
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/libfm-extra.pc" ]]; then
    echo "[libfm] libfm-extra did not install its package description" >&2
    exit 1
fi

echo "[libfm] menu-cache $MENU_CACHE_VERSION"
# menu-cache 1.1.0 defines the shared menu tag objects as tentative
# definitions in more than one translation unit.  GCC historically merged
# them; modern compilers use -fno-common, so the build asks for the historic
# merging behaviour instead of changing the upstream sources.
run_configure_build menu-cache "$SRC_DIR/menu-cache-$MENU_CACHE_VERSION" "$BUILD_ROOT/menu-cache" "-fcommon"
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/libmenu-cache.pc" ]]; then
    echo "[libfm] menu-cache did not install its package description" >&2
    exit 1
fi

echo "[libfm] libfm-gtk3 $LIBFM_VERSION"
run_configure_build libfm-gtk3 "$SRC_DIR/libfm-$LIBFM_VERSION" "$BUILD_ROOT/libfm-gtk3" "" --with-gtk=3 --disable-exif --disable-udisks --disable-old-actions
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/libfm-gtk3.pc" ]]; then
    echo "[libfm] libfm-gtk3 did not install its package description" >&2
    exit 1
fi
if [[ -z "$(find "$GRAPHICS_SYSROOT/usr/local/lib" -name 'libfm-gtk3.so.*' -print -quit)" ]]; then
    echo "[libfm] libfm-gtk3 did not install its shared library" >&2
    exit 1
fi
# DESTDIR installs write /usr/local/lib into .la archives, which makes
# downstream libtool links look on the Ubuntu host instead of the sysroot.
rm -f "$GRAPHICS_SYSROOT/usr/local/lib"/libfm*.la "$GRAPHICS_SYSROOT/usr/local/lib/libmenu-cache.la"

echo "[DONE] Upstream libfm-extra, menu-cache and libfm-gtk3 built for akaOS"
