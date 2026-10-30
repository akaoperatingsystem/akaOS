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

# Build the unmodified upstream LXDE panel, the panel of the LXDE desktop
# environment.  The package is an autotools project, so it is configured with
# the same cross compiler wrapper the X.Org client stack uses.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="0.11.1"
SHA256="24334dde6ed37d32bb021f902dbd54a1e6cdd01f35a778d80fcd3acd23a0e93b"
URL="https://deb.debian.org/debian/pool/main/l/lxpanel/lxpanel_0.11.1.orig.tar.gz"
WORKROOT="${AKAOS_LXPANEL_WORKROOT:-$HOME/.cache/akaos/lxpanel}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make autoreconf autoconf automake libtoolize; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream LXDE panel build tool: $command_name" >&2
        echo "On Ubuntu: sudo install curl coreutils tar clang lld pkg-config make autoconf automake libtool" >&2
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
for required_package in gtk+-3.0 glib-2.0 libxml-2.0 x11; do
    if ! PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig" \
        PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT" \
        pkg-config --exists "$required_package"; then
        echo "The upstream LXDE panel needs the $required_package package" >&2
        exit 2
    fi
done

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

ARCHIVE="$SRC_DIR/lxpanel-$VERSION.tar.gz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[lxpanel] fetching lxpanel $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[lxpanel] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[lxpanel] checksum mismatch" >&2
    echo "  expected $SHA256" >&2
    echo "  actual   $actual_sha" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/lxpanel-$VERSION"
if [[ ! -f "$SOURCE_DIR/.akaos-extracted" ]]; then
    echo "[lxpanel] extracting lxpanel $VERSION"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
    tar -xf "$ARCHIVE" -C "$SOURCE_DIR" --strip-components=1
    touch "$SOURCE_DIR/.akaos-extracted"
fi

# The release of the panel carries no configure script, only the sources of
# the autotools that build it, so the script is generated first.  libtoolize
# and intltoolize run before aclocal so that aclocal.m4 includes their macros
# and is newer than m4/*.m4; otherwise make tries to re-run aclocal during the
# build.  ACLOCAL_PATH points aclocal at the staged GLib gettext macro.
if [[ ! -f "$SOURCE_DIR/configure" ]] || grep -q "AM_GLIB_GNU_GETTEXT" "$SOURCE_DIR/configure"; then
    echo "[lxpanel] generating the configure script"
    (
        cd "$SOURCE_DIR" || exit 1
        mkdir -p m4
        export ACLOCAL_PATH="$GRAPHICS_SYSROOT/usr/share/aclocal:$GRAPHICS_SYSROOT/usr/local/share/aclocal"
        libtoolize -c --automake --force
        intltoolize -c --automake --force
        aclocal -I m4
        autoheader --force
        automake --add-missing --copy --include-deps
        autoconf
    ) >"$LOG_DIR/lxpanel-autogen.log" 2>&1 || {
        echo "[lxpanel] generating the configure script failed" >&2
        tail -40 "$LOG_DIR/lxpanel-autogen.log" >&2
        exit 1
    }
fi
if [[ ! -f "$SOURCE_DIR/configure" ]]; then
    echo "[lxpanel] the configure script was not generated" >&2
    exit 1
fi

BUILD_DIR_PATH="$BUILD_ROOT/lxpanel-$VERSION"
if [[ "${AKAOS_LXPANEL_FORCE_REBUILD:-0}" == "1" ||
      ! -x "$STAGE/usr/local/bin/lxpanel" ||
      ! -f "$STAGE/etc/xdg/lxpanel/default/panels/panel" ]]; then
    echo "[lxpanel] configuring lxpanel $VERSION"
    rm -rf "$BUILD_DIR_PATH"
    rm -rf "$STAGE" "$GRAPHICS_SYSROOT/usr/local/lib/lxpanel/plugins"
    rm -f "$GRAPHICS_SYSROOT/usr/local/lib/lxpanel/plugins"/*.la
    mkdir -p "$BUILD_DIR_PATH/data/ui" "$BUILD_DIR_PATH/data/default/panels" "$BUILD_DIR_PATH/data/two_panels/panels"
    (
        cd "$BUILD_DIR_PATH" || exit 1
        env \
            CC="$CC_WRAPPER" \
            CFLAGS="-O2 -g0 -fno-strict-aliasing" \
            CPPFLAGS="-I$BUILD_DIR_PATH/src -I$GRAPHICS_SYSROOT/usr/local/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2" \
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
                --enable-gtk3 \
                --disable-indicator_support \
                --disable-alsa \
                --with-plugins=deskno
    ) >"$LOG_DIR/lxpanel-configure.log" 2>&1 || {
        echo "[lxpanel] configure failed" >&2
        tail -40 "$LOG_DIR/lxpanel-configure.log" >&2
        exit 1
    }

    echo "[lxpanel] building lxpanel $VERSION"
    # The startup object and the linker script belong to the programs alone.  A
    # shared library that carries the startup object keeps a reference to the
    # program entry point, which no loader can satisfy for a library.
    make -C "$BUILD_DIR_PATH/src" -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
        lxpanel_LDFLAGS="$(sed -n 's/^lxpanel_LDFLAGS = //p' "$BUILD_DIR_PATH/src/Makefile" | head -1) -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o" \
        lxpanelctl_LDFLAGS="$(sed -n 's/^lxpanelctl_LDFLAGS = //p' "$BUILD_DIR_PATH/src/Makefile" | head -1) -pie -Wl,--eh-frame-hdr -Wl,-z,max-page-size=0x1000 -Wl,--dynamic-linker,/lib/ld-akaos.so -Wl,-T,$ROOT_DIR/userspace/linker/user-dynamic.ld $BUILD_DIR/user-dynamic-crt0.o" \
        >"$LOG_DIR/lxpanel-build.log" 2>&1 || {
        echo "[lxpanel] build failed" >&2
        tail -40 "$LOG_DIR/lxpanel-build.log" >&2
        exit 1
    }
    make -C "$BUILD_DIR_PATH" -j1 \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
        >"$LOG_DIR/lxpanel-build.log" 2>&1 || {
        echo "[lxpanel] build failed" >&2
        tail -40 "$LOG_DIR/lxpanel-build.log" >&2
        exit 1
    }
    rm -rf "$STAGE"
    mkdir -p "$STAGE"
    make -C "$BUILD_DIR_PATH" install DESTDIR="$STAGE" >"$LOG_DIR/lxpanel-install.log" 2>&1 || {
        echo "[lxpanel] install failed" >&2
        tail -30 "$LOG_DIR/lxpanel-install.log" >&2
        exit 1
    }
fi

if [[ ! -x "$STAGE/usr/local/bin/lxpanel" ]]; then
    echo "[lxpanel] did not install its panel" >&2
    exit 1
fi
# The program has to ask for the akaOS loader, or the kernel cannot start it.
# The count keeps the reader of the pipe from closing it early, which would
# leave the tool with a broken pipe and the check with a failure that is not
# one.
if ! llvm-readelf -l "$STAGE/usr/local/bin/lxpanel" 2>/dev/null |
        grep -c "/lib/ld-akaos.so" >/dev/null; then
    echo "[lxpanel] the panel does not use the akaOS loader" >&2
    exit 1
fi

# Publish into the graphics sysroot.
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
rm -f "$STAGE/usr/local/lib/lxpanel/liblxpanel.la"
for subtree in include lib share bin; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
if [[ -d "$STAGE/etc" ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc"
    cp -a "$STAGE/etc/." "$GRAPHICS_SYSROOT/etc/"
fi

echo "[DONE] Upstream LXDE panel $VERSION built for the LXDE desktop environment"
