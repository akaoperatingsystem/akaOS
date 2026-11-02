#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream mate-panel for MATE.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="1.26.4"
SHA256="2070f9d515657e5ddcda0d87ef729713dba9cb7b2ad06223bd674a21cc6b3daf"
URL="https://pub.mate-desktop.org/releases/1.26/mate-panel-1.26.4.tar.xz"
WORKROOT="${AKAOS_MATE_PANEL_WORKROOT:-$HOME/.cache/akaos/mate-panel}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
HOST_TOOLS="$HOME/.cache/akaos/host-glib-tools"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream mate-panel build tool: $command_name" >&2
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

for required_package in gtk+-3.0 glib-2.0 mate-desktop-2.0 libmate-menu libwnck-3.0 dconf x11 xrandr mateweather; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig" \
        PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT" \
        pkg-config --exists "$required_package"; then
        echo "The upstream mate-panel needs the $required_package package" >&2
        exit 2
    fi
done

if [[ -d "$HOST_TOOLS/usr/bin" ]]; then
    export PATH="$HOST_TOOLS/usr/bin:$PATH"
fi

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/mate-panel-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[mate-panel] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/mate-panel-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[mate-panel] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/mate-panel-$VERSION"
if [[ ! -x "$STAGE_PREFIX/bin/mate-panel" ]]; then
    echo "[mate-panel] configuring mate-panel $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
        env \
            CC="$CC_WRAPPER" \
            CFLAGS="-O2 -g0 -fno-strict-aliasing" \
            CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2" \
            LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
            LIBS="" \
            PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig" \
            PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT" \
            "$SOURCE_DIR/configure" \
                --host=x86_64-pc-linux-gnu \
                --prefix=/usr/local \
                --sysconfdir=/etc \
                --disable-static \
                --enable-shared \
                --enable-x11 \
                --disable-wayland \
                --disable-nls \
                --enable-introspection=no \
                --disable-schemas-compile
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             CLOCK_CFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include"             CLOCK_LIBS="-L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -lgtk-3 -lmate-desktop-2"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --disable-static                 --enable-shared                 --enable-x11                 --disable-wayland                 --disable-nls                 --enable-introspection=no                 --disable-schemas-compile
    ) >"$LOG_DIR/mate-panel-configure.log" 2>&1 || {
        echo "[mate-panel] configure failed" >&2
        tail -30 "$LOG_DIR/mate-panel-configure.log" >&2
        exit 1
    }

    echo "[mate-panel] building libmate-panel-applet"
    make -C "$BUILD_DIR_PATH/libmate-panel-applet" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build libmate-panel-applet failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    echo "[mate-panel] building mate-panel submodules"
    make -C "$BUILD_DIR_PATH/mate-panel/mate-submodules/libegg" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build libegg failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/mate-panel" panel-marshal.h panel-marshal.c
    make -C "$BUILD_DIR_PATH/mate-panel/libmate-panel-applet-private" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build libmate-panel-applet-private failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/mate-panel/libpanel-util" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build libpanel-util failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    echo "[mate-panel] building mate-panel binary"
    make -C "$BUILD_DIR_PATH/mate-panel" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build mate-panel failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    echo "[mate-panel] building applets"
    make -C "$BUILD_DIR_PATH/applets/notification_area" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build notification_area failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/applets/wncklet" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build wncklet failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/applets/fish" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build fish failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    make -C "$BUILD_DIR_PATH/applets/clock" -j1         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >>"$LOG_DIR/mate-panel-build.log" 2>&1 || {
        echo "[mate-panel] build clock failed" >&2
        tail -30 "$LOG_DIR/mate-panel-build.log" >&2
        exit 1
    }

    echo "[mate-panel] installing"
    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/mate-panel-install.log" 2>&1 || {
        echo "[mate-panel] install failed" >&2
        tail -30 "$LOG_DIR/mate-panel-install.log" >&2
        exit 1
    }
fi

if [[ ! -x "$STAGE_PREFIX/bin/mate-panel" ]]; then
    echo "[mate-panel] failed to produce mate-panel binary" >&2
    exit 1
fi

if ! llvm-readelf -l "$STAGE_PREFIX/bin/mate-panel" 2>/dev/null | grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[mate-panel] binary does not use the akaOS dynamic linker" >&2
    exit 1
fi

find "$STAGE_PREFIX" -name "*.la" -delete

mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin include lib libexec share; do
    if [[ -d "$STAGE_PREFIX/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE_PREFIX/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
if [[ -d "$STAGE/etc" ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc"
    cp -a "$STAGE/etc/." "$GRAPHICS_SYSROOT/etc/"
fi

echo "[DONE] Upstream mate-panel $VERSION built and published to graphics sysroot"
