#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream mate-session-manager for MATE.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="1.26.1"
SHA256="5b8c7d6441fd9c293c863882ab67a7493c53cdf64eab27c094575f423ebd4278"
URL="https://pub.mate-desktop.org/releases/1.26/mate-session-manager-1.26.1.tar.xz"
WORKROOT="${AKAOS_MATE_SESSION_WORKROOT:-$HOME/.cache/akaos/mate-session}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
HOST_TOOLS="$HOME/.cache/akaos/host-glib-tools"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream mate-session build tool: $command_name" >&2
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
if [[ ! -f "$BUILD_DIR/user-dynamic-crt0.o" ]]; then
    echo "Missing the akaOS startup object in $BUILD_DIR; run build.sh first" >&2
    exit 2
fi

for required_package in gtk+-3.0 glib-2.0 gio-2.0 dbus-glib-1 sm ice x11; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream mate-session needs the $required_package package" >&2
        exit 2
    fi
done

if [[ -d "$HOST_TOOLS/usr/bin" ]]; then
    export PATH="$HOST_TOOLS/usr/bin:$PATH"
fi

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/mate-session-manager-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[mate-session] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/mate-session-manager-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[mate-session] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/mate-session-manager-$VERSION"
if [[ ! -x "$STAGE_PREFIX/bin/mate-session" ]]; then
    echo "[mate-session] configuring mate-session-manager $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             GL_TEST_CFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include"             GL_TEST_LIBS="-L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -lepoxy -lX11 -lXcomposite"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --disable-static                 --enable-shared                 --disable-nls                 --disable-schemas-compile                 --without-systemd                 --without-elogind                 --with-default-wm=marco
    ) >"$LOG_DIR/mate-session-configure.log" 2>&1 || {
        echo "[mate-session] configure failed" >&2
        tail -30 "$LOG_DIR/mate-session-configure.log" >&2
        exit 1
    }

    echo "[mate-session] building libegg"
    make -C "$BUILD_DIR_PATH/mate-submodules/libegg" -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
        >"$LOG_DIR/mate-session-build.log" 2>&1 || {
        echo "[mate-session] build libegg failed" >&2
        tail -30 "$LOG_DIR/mate-session-build.log" >&2
        exit 1
    }

    echo "[mate-session] building libgsmutil"
    make -C "$BUILD_DIR_PATH/mate-session" libgsmutil.la -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
        >>"$LOG_DIR/mate-session-build.log" 2>&1 || {
        echo "[mate-session] build libgsmutil failed" >&2
        tail -30 "$LOG_DIR/mate-session-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/mate-session" gsm-marshal.c gsm-marshal.h gsm-manager-glue.h gsm-presence-glue.h gsm-inhibitor-glue.h gsm-client-glue.h gsm-app-glue.h
    echo "[mate-session] building mate-session binary"
    make -C "$BUILD_DIR_PATH/mate-session" mate-session -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o" \
        >>"$LOG_DIR/mate-session-build.log" 2>&1 || {
        echo "[mate-session] build mate-session failed" >&2
        tail -30 "$LOG_DIR/mate-session-build.log" >&2
        exit 1
    }

    echo "[mate-session] building tools"
    make -C "$BUILD_DIR_PATH/tools" mate-session-save mate-session-inhibit -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o" \
        >>"$LOG_DIR/mate-session-build.log" 2>&1 || {
        echo "[mate-session] build tools failed" >&2
        tail -30 "$LOG_DIR/mate-session-build.log" >&2
        exit 1
    }

    echo "[mate-session] installing"
    make -C "$BUILD_DIR_PATH/mate-session" install DESTDIR="$STAGE" >"$LOG_DIR/mate-session-install.log" 2>&1
    make -C "$BUILD_DIR_PATH/tools" install-binPROGRAMS DESTDIR="$STAGE" >>"$LOG_DIR/mate-session-install.log" 2>&1
    make -C "$BUILD_DIR_PATH/data" install DESTDIR="$STAGE" >>"$LOG_DIR/mate-session-install.log" 2>&1
fi

if [[ ! -x "$STAGE_PREFIX/bin/mate-session" ]]; then
    echo "[mate-session] failed to produce mate-session binary" >&2
    exit 1
fi

if ! llvm-readelf -l "$STAGE_PREFIX/bin/mate-session" 2>/dev/null | grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[mate-session] binary does not use the akaOS dynamic linker" >&2
    exit 1
fi

find "$STAGE_PREFIX" -name "*.la" -delete

mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin include lib share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
if [[ -d "$STAGE/etc" ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc"
    cp -a "$STAGE/etc/." "$GRAPHICS_SYSROOT/etc/"
fi

echo "[DONE] Upstream mate-session-manager $VERSION built and published to graphics sysroot"
