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

# Build the unmodified upstream Openbox window manager, which is the window
# manager of the LXDE desktop environment.  The package is an autotools
# project, so it is configured with the same cross compiler wrapper the X.Org
# client stack uses.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="3.6.1"
SHA256="8b4ac0760018c77c0044fab06a4f0c510ba87eae934d9983b10878483bde7ef7"
URL="https://deb.debian.org/debian/pool/main/o/openbox/openbox_3.6.1.orig.tar.gz"
WORKROOT="${AKAOS_OPENBOX_WORKROOT:-$HOME/.cache/akaos/openbox}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream Openbox build tool: $command_name" >&2
        echo "On Ubuntu: sudo install curl coreutils tar clang lld pkg-config make" >&2
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
for required_library in libX11.so.6 libXext.so.6 libXrender.so.1 libpng16.so.16; do
    if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/$required_library" ]]; then
        echo "Missing upstream X11 client library: $required_library" >&2
        echo "Build the upstream X11 client stack first." >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

ARCHIVE="$SRC_DIR/openbox-$VERSION.tar.gz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[openbox] fetching openbox $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[openbox] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[openbox] checksum mismatch" >&2
    echo "  expected $SHA256" >&2
    echo "  actual   $actual_sha" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/openbox-$VERSION"
if [[ ! -f "$SOURCE_DIR/.akaos-extracted" ]]; then
    echo "[openbox] extracting openbox $VERSION"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
    tar -xf "$ARCHIVE" -C "$SOURCE_DIR" --strip-components=1
    touch "$SOURCE_DIR/.akaos-extracted"
fi

BUILD_DIR_PATH="$BUILD_ROOT/openbox-$VERSION"
if [[ -f "$STAGE/usr/local/bin/openbox" ]] &&
   llvm-readelf -l "$STAGE/usr/local/bin/openbox" 2>/dev/null |
       grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[openbox] already staged"
else
    echo "[openbox] configuring openbox $VERSION"
    rm -rf "$BUILD_DIR_PATH"
    mkdir -p "$BUILD_DIR_PATH"
    (
        cd "$BUILD_DIR_PATH" || exit 1
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
                --prefix="$STAGE_PREFIX" \
                --disable-static \
                --enable-shared \
                --disable-imlib2 \
                --disable-librsvg \
                --disable-startup-notification
    ) >"$LOG_DIR/openbox-configure.log" 2>&1 || {
        echo "[openbox] configure failed" >&2
        tail -40 "$LOG_DIR/openbox-configure.log" >&2
        exit 1
    }

    echo "[openbox] building openbox $VERSION"
    # The startup object and the linker script belong to the program alone.
    # A shared library that carries the startup object keeps a reference to the
    # program entry point, which no loader can satisfy for a library.
    make -C "$BUILD_DIR_PATH" -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
        openbox_openbox_LDFLAGS="-export-dynamic -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o" \
        >"$LOG_DIR/openbox-build.log" 2>&1 || {
        echo "[openbox] build failed" >&2
        tail -40 "$LOG_DIR/openbox-build.log" >&2
        exit 1
    }
    make -C "$BUILD_DIR_PATH" install >"$LOG_DIR/openbox-install.log" 2>&1 || {
        echo "[openbox] install failed" >&2
        tail -30 "$LOG_DIR/openbox-install.log" >&2
        exit 1
    }
fi

if [[ ! -x "$STAGE/usr/local/bin/openbox" ]]; then
    echo "[openbox] did not install its window manager" >&2
    exit 1
fi
# The program has to ask for the akaOS loader, or the kernel cannot start it.
# The count keeps the reader of the pipe from closing it early, which would
# leave the tool with a broken pipe and the check with a failure that is not
# one.
if ! llvm-readelf -l "$STAGE/usr/local/bin/openbox" 2>/dev/null |
        grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[openbox] the window manager does not use the akaOS loader" >&2
    exit 1
fi

# Publish into the graphics sysroot.
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share bin etc; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done

if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/etc/xdg/openbox/rc.xml" ]]; then
    echo "[openbox] the window manager configuration file is missing from the graphics sysroot" >&2
    exit 1
fi

echo "[DONE] Upstream Openbox $VERSION built for the LXDE desktop environment"
