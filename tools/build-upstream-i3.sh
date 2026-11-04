#!/usr/bin/env bash
set -euo pipefail
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="4.23"
SHA256="61026a7196c9139d0f3aadd27197e8b320c576e3a450e01d74c1aca484044c46"
URL="https://i3wm.org/downloads/i3-$VERSION.tar.xz"
WORKROOT="${AKAOS_I3_WORKROOT:-$HOME/.cache/akaos/i3}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$GRAPHICS_SYSROOT/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

ARCHIVE="$SRC_DIR/i3-$VERSION.tar.xz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[i3] fetching i3 $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[i3] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi

actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[i3] checksum mismatch" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/i3-$VERSION"
if [[ ! -f "$SOURCE_DIR/.akaos-extracted" ]]; then
    echo "[i3] extracting i3 $VERSION"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
    tar -xf "$ARCHIVE" -C "$SOURCE_DIR" --strip-components=1
    touch "$SOURCE_DIR/.akaos-extracted"
fi

BUILD_DIR_PATH="$BUILD_ROOT"
if [[ -f "$STAGE_PREFIX/bin/i3" ]] &&
   llvm-readelf -l "$STAGE_PREFIX/bin/i3" 2>/dev/null |
       grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[i3] already staged"
else
    echo "[i3] configuring i3 $VERSION"
    export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"

    CROSS_FILE="$BUILD_DIR_PATH/cross-akaos.txt"
    cat > "$CROSS_FILE" <<EOF
[binaries]
c = '$CC_WRAPPER'
strip = 'llvm-strip'
pkg-config = 'pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

    meson setup "$BUILD_DIR_PATH" "$SOURCE_DIR" \
        --cross-file "$CROSS_FILE" \
        --prefix=/usr/local \
        -Ddocs=false \
        -Dmans=false \
        -Dc_args="-O2 -g0 -I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2 -I$GRAPHICS_SYSROOT/usr/local/include/pango-1.0 -I$GRAPHICS_SYSROOT/usr/local/include/cairo -I$GRAPHICS_SYSROOT/usr/include/glib-2.0 -I$GRAPHICS_SYSROOT/usr/lib/glib-2.0/include" \
        -Dc_link_args="-Wl,--allow-shlib-undefined -nostdlib -fuse-ld=lld -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

    echo "[i3] building i3 with ninja"
    ninja -C "$BUILD_DIR_PATH"

    echo "[i3] relinking binaries with akaOS dynamic runtime"
    LINK_FLAGS=(
        --target=x86_64-unknown-none-elf
        -nostdlib -fuse-ld=lld
        -Wl,--allow-shlib-undefined -Wl,--as-needed
        -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000
        -Wl,--dynamic-linker,/lib/ld-akaos.so
        -Wl,-T,"$ROOT_DIR/userspace/linker/user-dynamic.ld"
        "$BUILD_DIR/user-dynamic-crt0.o"
    )

    SHARED_LIBS=(
        -L"$BUILD_DIR_PATH" -li3
        -L"$GRAPHICS_SYSROOT/usr/local/lib" -L"$GRAPHICS_SYSROOT/usr/lib" -L"$BUILD_DIR"
        -lstartup-notification-1 -lxcb -lxcb-xkb -lxcb-xinerama -lxcb-randr -lxcb-shape
        -lxcb-util -lxcb-cursor -lxcb-keysyms -lxcb-icccm -lxcb-xrm
        -lxkbcommon -lxkbcommon-x11 -lyajl -lpcre2-8 -lcairo -lpangocairo-1.0 -lpango-1.0
        -lgobject-2.0 -lglib-2.0 -lev -Wl,-l:libm.so.6 -Wl,-l:libc.so.6
    )

    mkdir -p "$STAGE_PREFIX/bin" "$STAGE_PREFIX/etc/i3"
    clang "${LINK_FLAGS[@]}" "$BUILD_DIR_PATH/i3.p/"*.o "${SHARED_LIBS[@]}" -o "$STAGE_PREFIX/bin/i3"
    clang "${LINK_FLAGS[@]}" "$BUILD_DIR_PATH/i3bar.p/"*.o "${SHARED_LIBS[@]}" -o "$STAGE_PREFIX/bin/i3bar"
    clang "${LINK_FLAGS[@]}" "$BUILD_DIR_PATH/i3-msg.p/"*.o "${SHARED_LIBS[@]}" -o "$STAGE_PREFIX/bin/i3-msg"
    clang "${LINK_FLAGS[@]}" "$BUILD_DIR_PATH/i3-input.p/"*.o "${SHARED_LIBS[@]}" -o "$STAGE_PREFIX/bin/i3-input"
    clang "${LINK_FLAGS[@]}" "$BUILD_DIR_PATH/i3-nagbar.p/"*.o "${SHARED_LIBS[@]}" -o "$STAGE_PREFIX/bin/i3-nagbar"
    clang "${LINK_FLAGS[@]}" "$BUILD_DIR_PATH/i3-config-wizard.p/"*.o "${SHARED_LIBS[@]}" -o "$STAGE_PREFIX/bin/i3-config-wizard"

    cp "$SOURCE_DIR/etc/config" "$STAGE_PREFIX/etc/i3/config"
    cp "$SOURCE_DIR/etc/config.keycodes" "$STAGE_PREFIX/etc/i3/config.keycodes"
fi

echo "[i3] DONE"
