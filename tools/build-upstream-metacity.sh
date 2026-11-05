#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
METACITY_SOURCE="$BUILD_DIR/upstream-source/metacity-3.50.0"
METACITY_BUILD="$BUILD_DIR/upstream-build/metacity-3.50.0/x86_64-akaos"

mkdir -p "$METACITY_BUILD" "$GRAPHICS_SYSROOT/usr/local/bin" "$GRAPHICS_SYSROOT/usr/local/lib" "$GRAPHICS_SYSROOT/usr/local/share/glib-2.0/schemas"

# 1. Compile libXpresent if needed
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/libXpresent.so" ]]; then
    bash "$BUILD_DIR/build-libxpresent.sh"
fi

# 2. Configure metacity if not configured
if [[ ! -f "$METACITY_BUILD/Makefile" ]]; then
    cd "$METACITY_BUILD"
    export CC="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
    export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"

    export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
    export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

    "$METACITY_SOURCE/configure" \
        --host=x86_64-linux-gnu \
        --prefix=/usr/local \
        --disable-nls \
        --disable-canberra \
        --disable-startup-notification \
        --disable-Werror \
        --disable-schemas-compile \
        --enable-shared \
        --disable-static \
        --with-x

    sed -i 's/-lXinerama/-lXinerama -lXpresent/g' "$METACITY_BUILD/src/Makefile"
fi

# 3. Build libmetacity and src
make -C "$METACITY_BUILD/libmetacity" -j2
make -C "$METACITY_BUILD/src" -j2

# 4. Relink metacity with native akaOS dynamic linker and user-dynamic-crt0
echo "[RELINK] metacity with akaOS dynamic runtime"
cd "$METACITY_BUILD/src"
clang --target=x86_64-unknown-none-elf \
    -nostdlib -fuse-ld=lld \
    -Wl,--allow-shlib-undefined -Wl,--as-needed \
    -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 \
    -Wl,--dynamic-linker,/lib/ld-akaos.so \
    -Wl,-T,"$ROOT_DIR/userspace/linker/user-dynamic.ld" \
    "$BUILD_DIR/user-dynamic-crt0.o" \
    compositor/metacity-*.o core/metacity-*.o ui/metacity-*.o \
    -L"$METACITY_BUILD/libmetacity/.libs" -lmetacity \
    -L"$GRAPHICS_SYSROOT/usr/local/lib" -L"$GRAPHICS_SYSROOT/usr/lib" -L"$BUILD_DIR" \
    -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lcairo-gobject -lgdk_pixbuf-2.0 -latk-1.0 -lpango-1.0 -lharfbuzz -lcairo \
    -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl \
    -lX11 -lXext -lXrandr -lXinerama -lXcursor -lXrender -lXcomposite -lXdamage -lXfixes -lXRes -lXpresent \
    -lSM -lICE \
    "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" \
    -o "$METACITY_BUILD/src/metacity.akaos"

# 5. Install to graphics sysroot
cp -f "$METACITY_BUILD/src/metacity.akaos" "$GRAPHICS_SYSROOT/usr/local/bin/metacity"
cp -a "$METACITY_BUILD/libmetacity/.libs/libmetacity.so"* "$GRAPHICS_SYSROOT/usr/local/lib/"

# 6. Install schemas
if [[ -f "$METACITY_SOURCE/data/schemas/org.gnome.metacity.gschema.xml" ]]; then
    cp -f "$METACITY_SOURCE/data/schemas/org.gnome.metacity.gschema.xml" "$GRAPHICS_SYSROOT/usr/local/share/glib-2.0/schemas/"
    glib-compile-schemas "$GRAPHICS_SYSROOT/usr/local/share/glib-2.0/schemas"
fi

echo "[DONE] metacity 3.50.0 built successfully:"
ls -lh "$GRAPHICS_SYSROOT/usr/local/bin/metacity" "$GRAPHICS_SYSROOT/usr/local/lib/libmetacity.so.3"
