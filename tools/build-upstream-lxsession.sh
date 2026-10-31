#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

# Build the unmodified upstream lxsession package.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="0.5.5"
SHA256="e43e0d9c033095559ab57c8821c2b84fea58009d267db1324d32dca8bd4dbb46"
URL="https://deb.debian.org/debian/pool/main/l/lxsession/lxsession_0.5.5.orig.tar.xz"
WORKROOT="${AKAOS_LXSESSION_WORKROOT:-$HOME/.cache/akaos/lxsession}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream lxsession build tool: $command_name" >&2
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
for required_package in gtk+-3.0 glib-2.0 x11; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"         PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"         pkg-config --exists "$required_package"; then
        echo "The upstream lxsession needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"
ARCHIVE="$SRC_DIR/lxsession-$VERSION.tar.xz"

if [[ ! -f "$ARCHIVE" ]]; then
    echo "[lxsession] downloading $VERSION"
    curl -fsSL "$URL" -o "$ARCHIVE.tmp"
    mv "$ARCHIVE.tmp" "$ARCHIVE"
fi

archive_sha="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
if [[ "$archive_sha" != "$SHA256" ]]; then
    echo "Checksum mismatch for $ARCHIVE" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/lxsession-$VERSION"
if [[ ! -d "$SOURCE_DIR" ]]; then
    echo "[lxsession] extracting $VERSION"
    tar -xf "$ARCHIVE" -C "$SRC_DIR"
fi

BUILD_DIR_PATH="$BUILD_ROOT/lxsession-$VERSION"
if [[ ! -x "$STAGE/usr/local/bin/lxsession" ]]; then
    echo "[lxsession] configuring lxsession $VERSION"
    rm -rf "$BUILD_DIR_PATH" "$STAGE"
    mkdir -p "$BUILD_DIR_PATH" "$STAGE"
    (
        cd "$BUILD_DIR_PATH"
        env             CC="$CC_WRAPPER"             CFLAGS="-O2 -g0 -fno-strict-aliasing"             CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"             LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6"             LIBS=""             POLKIT_CFLAGS=" "             POLKIT_LIBS=" "             PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"             PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"             "$SOURCE_DIR/configure"                 --host=x86_64-pc-linux-gnu                 --prefix=/usr/local                 --sysconfdir=/etc                 --enable-gtk3                 --disable-man                 --disable-buildin-clipboard                 --disable-buildin-polkit                 --disable-advanced-notifications
    ) >"$LOG_DIR/lxsession-configure.log" 2>&1 || {
        echo "[lxsession] configure failed" >&2
        tail -30 "$LOG_DIR/lxsession-configure.log" >&2
        exit 1
    }

    echo "[lxsession] building session programs"
    cd "$BUILD_DIR_PATH"
    make lxsession/lxsession lxsettings-daemon/lxsettings-daemon xdg-autostart/lxsession-xdg-autostart lxsession-edit/lxsession-edit lxsession-logout/lxsession-logout lxsession-db/lxsession-db         LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6 -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o"         >"$LOG_DIR/lxsession-build.log" 2>&1 || {
        echo "[lxsession] build failed" >&2
        tail -30 "$LOG_DIR/lxsession-build.log" >&2
        exit 1
    }

    echo "[lxsession] staging session files"
    mkdir -p "$STAGE/usr/local/bin" "$STAGE/usr/local/share" "$STAGE/usr/local/libexec"
    make -C data install DESTDIR="$STAGE" >>"$LOG_DIR/lxsession-install.log" 2>&1 || true
    make -C po install DESTDIR="$STAGE" >>"$LOG_DIR/lxsession-install.log" 2>&1 || true

    for prog in lxsession/lxsession lxsettings-daemon/lxsettings-daemon xdg-autostart/lxsession-xdg-autostart lxsession-edit/lxsession-edit lxsession-logout/lxsession-logout lxsession-db/lxsession-db; do
        cp "$prog" "$STAGE/usr/local/bin/"
    done

    cp "$SOURCE_DIR/lxlock/lxlock" "$STAGE/usr/local/bin/"
    cp "$SOURCE_DIR/lxsession-default/lxsession-default" "$STAGE/usr/local/bin/"
    cp "$SOURCE_DIR/lxsession-default/lxsession-default-terminal" "$STAGE/usr/local/bin/"
    cp "$SOURCE_DIR/lxsession-utils/lxsession-xsettings" "$STAGE/usr/local/libexec/"
fi

if [[ ! -x "$STAGE/usr/local/bin/lxsession" ]]; then
    echo "[lxsession] failed to produce lxsession" >&2
    exit 1
fi

if ! llvm-readelf -l "$STAGE/usr/local/bin/lxsession" 2>/dev/null | grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[lxsession] binary does not use the akaOS dynamic linker" >&2
    exit 1
fi

# Publish to graphics sysroot
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in bin share libexec; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
if [[ -d "$STAGE/etc" ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc"
    cp -a "$STAGE/etc/." "$GRAPHICS_SYSROOT/etc/"
fi

echo "[DONE] Upstream LXSession $VERSION built and published to graphics sysroot"
