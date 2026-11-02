#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream dbus-glib for akaOS.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="0.112"
SHA256="7d550dccdfcd286e33895501829ed971eeb65c614e73aadb4a08aeef719b143a"
URL="https://deb.debian.org/debian/pool/main/d/dbus-glib/dbus-glib_0.112.orig.tar.gz"
WORKROOT="${AKAOS_DBUS_GLIB_WORKROOT:-$HOME/.cache/akaos/dbus-glib}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make dbus-binding-tool; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream dbus-glib build tool: $command_name" >&2
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

for required_package in glib-2.0 gio-2.0 dbus-1; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream dbus-glib needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/dbus-glib-$VERSION.tar.gz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[dbus-glib] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/dbus-glib-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[dbus-glib] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/dbus-glib-$VERSION"
if [[ ! -f "$STAGE_PREFIX/lib/libdbus-glib-1.so" ]]; then
    echo "[dbus-glib] configuring dbus-glib $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --disable-static                 --enable-shared                 --disable-tests                 --disable-gtk-doc                 --with-dbus-binding-tool="$(which dbus-binding-tool)"
    ) >"$LOG_DIR/dbus-glib-configure.log" 2>&1 || {
        echo "[dbus-glib] configure failed" >&2
        tail -30 "$LOG_DIR/dbus-glib-configure.log" >&2
        exit 1
    }

    echo "[dbus-glib] building dbus-gmain"
    make -C "$BUILD_DIR_PATH/dbus-gmain" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >"$LOG_DIR/dbus-glib-build.log" 2>&1 || {
        echo "[dbus-glib] build dbus-gmain failed" >&2
        tail -30 "$LOG_DIR/dbus-glib-build.log" >&2
        exit 1
    }

    echo "[dbus-glib] building dbus"
    make -C "$BUILD_DIR_PATH/dbus" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >>"$LOG_DIR/dbus-glib-build.log" 2>&1 || {
        echo "[dbus-glib] build dbus failed" >&2
        tail -30 "$LOG_DIR/dbus-glib-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/dbus" install DESTDIR="$STAGE" >"$LOG_DIR/dbus-glib-install.log" 2>&1 || {
        echo "[dbus-glib] install dbus failed" >&2
        tail -30 "$LOG_DIR/dbus-glib-install.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH" install-pkgconfigDATA DESTDIR="$STAGE" >>"$LOG_DIR/dbus-glib-install.log" 2>&1 || {
        echo "[dbus-glib] install pkgconfig failed" >&2
        tail -30 "$LOG_DIR/dbus-glib-install.log" >&2
        exit 1
    }
fi

if [[ ! -f "$STAGE_PREFIX/lib/libdbus-glib-1.so" ]]; then
    echo "[dbus-glib] failed to produce libdbus-glib-1.so" >&2
    exit 1
fi

if [[ -f /usr/include/dbus-1.0/dbus/dbus-glib-bindings.h ]]; then
    mkdir -p "$STAGE_PREFIX/include/dbus-1.0/dbus"
    cp -a /usr/include/dbus-1.0/dbus/dbus-glib-bindings.h "$STAGE_PREFIX/include/dbus-1.0/dbus/"
fi

find "$STAGE_PREFIX" -name "*.la" -delete

mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

echo "[DONE] Upstream dbus-glib $VERSION built and published to graphics sysroot"
