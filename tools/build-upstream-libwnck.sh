#!/usr/bin/env bash
set -euo pipefail
# Build the unmodified upstream libwnck 43.0 for the akaOS x86-64 userland.
#
# libwnck is the window-navigator library of the GTK desktop environments:
# the LXDE panel's taskbar and the LXDE session use its window list, and the
# XFCE, MATE and Cinnamon stacks use the same protocol.  libwnck 43 uses Meson
# and GTK 3, and it needs the XRes extension library in addition to GTK.
#
# The build runs in the Ubuntu ext4 cache, with the same cross compiler
# environment as the upstream GTK 3/4 builds, and publishes the installed tree
# into the graphics sysroot that build.sh consumes.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
WORKROOT="${AKAOS_LIBWNCK_WORKROOT:-$HOME/.cache/akaos/libwnck}"
SOURCE_DIR="$WORKROOT/src"
BUILD_TREE="$WORKROOT/build/libwnck-43.0"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
VERSION="43.0"
SHA256="905bcdb85847d6b8f8861e56b30cd6dc61eae67ecef4cd994a9f925a26a2c1fe"
URL="https://download.gnome.org/sources/libwnck/43/libwnck-43.0.tar.xz"
HOST_GLIB_TOOLS="$BUILD_DIR/host-tools/glib-2.88"
JOBS="${AKAOS_LIBWNCK_BUILD_JOBS:-4}"

for command_name in curl sha256sum tar meson ninja pkg-config make python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream libwnck build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar meson ninja-build pkg-config python3" >&2
        exit 2
    fi
done
# The Meson build compiles the libwnck GResource with the build machine's
# glib-compile-resources.  The target copy in the graphics sysroot cannot run
# on Ubuntu, so a host copy is staged locally and the package descriptions of
# the build machine are made to name it.
if ! command -v glib-compile-resources >/dev/null 2>&1; then
    for command_name in apt-get dpkg-deb; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            echo "Missing host tool $command_name; install libgio-2.0-dev-bin on Ubuntu." >&2
            exit 2
        fi
    done
    mkdir -p "$HOST_GLIB_TOOLS"
    if [[ ! -x "$HOST_GLIB_TOOLS/root/usr/bin/glib-compile-resources" ]]; then
        (cd "$HOST_GLIB_TOOLS" && apt-get download libgio-2.0-dev-bin)
        dpkg-deb -x "$HOST_GLIB_TOOLS"/libgio-2.0-dev-bin_*.deb "$HOST_GLIB_TOOLS/root"
    fi
    export PATH="$HOST_GLIB_TOOLS/root/usr/bin:$PATH"
fi
HOST_GLIB_PKGCONFIG="$WORKROOT/host-glib-pkgconfig"
mkdir -p "$HOST_GLIB_PKGCONFIG"
cp "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" "$HOST_GLIB_PKGCONFIG/glib-2.0.pc"
cp "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gio-2.0.pc" "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_compile_resources=.*#glib_compile_resources=glib-compile-resources#' "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_compile_schemas=.*#glib_compile_schemas=glib-compile-schemas#' "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
if [[ ! -f "$BUILD_DIR/libc.so.6" || ! -f "$BUILD_DIR/libm.so.6" ]]; then
    echo "Missing shared akaOS libc in $BUILD_DIR; run build.sh first" >&2
    exit 2
fi
for required_file in \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/gtk+-3.0.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/xres.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libXRes.so.1.0.0"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing upstream libwnck prerequisite: $required_file" >&2
        echo "Build the upstream X11 stack with libXres and GTK 3 first." >&2
        exit 2
    fi
done

mkdir -p "$SOURCE_DIR" "$WORKROOT/build" "$STAGE" "$LOG_DIR"
ARCHIVE="$SOURCE_DIR/libwnck-$VERSION.tar.xz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[libwnck] fetching libwnck $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[libwnck] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[libwnck] checksum mismatch" >&2
    echo "  expected $SHA256" >&2
    echo "  actual   $actual_sha" >&2
    exit 1
fi
SOURCE_TREE="$SOURCE_DIR/libwnck-$VERSION"
if [[ ! -f "$SOURCE_TREE/meson.build" ]]; then
    echo "[libwnck] extracting libwnck $VERSION"
    rm -rf "$SOURCE_TREE"
    mkdir -p "$SOURCE_TREE"
    tar -xf "$ARCHIVE" -C "$SOURCE_TREE" --strip-components=1
fi
if ! grep -q 'foreach prog: \[\]' "$SOURCE_TREE/libwnck/meson.build"; then
    echo "[libwnck] applying the akaOS build patch"
    patch -d "$SOURCE_TREE/libwnck" -p0 < "$ROOT_DIR/tools/libwnck-akaos-build.patch"
fi

export PKG_CONFIG_LIBDIR="$HOST_GLIB_PKGCONFIG:$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include -I$GRAPHICS_SYSROOT/usr/local/include/freetype2"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/lib -L$GRAPHICS_SYSROOT/usr/local/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-pango-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    -Dintrospection=disabled
    -Dstartup_notification=disabled
    -Dinstall_tools=false
    -Dgtk_doc=false
    -Ddeprecation_flags=false
)
# A changed pkg-config search path is not always picked up by a Meson
# reconfigure, so the build tree is created from scratch on every run.
rm -rf "$BUILD_TREE"
meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE" >"$LOG_DIR/libwnck-configure.log" 2>&1 || {
    echo "[libwnck] configure failed; see $LOG_DIR/libwnck-configure.log" >&2
    tail -50 "$LOG_DIR/libwnck-configure.log" >&2
    exit 1
}
meson compile -C "$BUILD_TREE" --jobs "$JOBS" >"$LOG_DIR/libwnck-build.log" 2>&1 || {
    echo "[libwnck] build failed; see $LOG_DIR/libwnck-build.log" >&2
    tail -60 "$LOG_DIR/libwnck-build.log" >&2
    exit 1
}
rm -rf "$STAGE/usr/local"
DESTDIR="$STAGE" meson install -C "$BUILD_TREE" >"$LOG_DIR/libwnck-install.log" 2>&1 || {
    echo "[libwnck] install failed; see $LOG_DIR/libwnck-install.log" >&2
    tail -40 "$LOG_DIR/libwnck-install.log" >&2
    exit 1
}
if [[ -z "$(find "$STAGE" -name 'libwnck-3.so.0' -print -quit)" ]]; then
    echo "[libwnck] did not install libwnck-3.so.0" >&2
    exit 1
fi

echo "[libwnck] publishing staged tree to $GRAPHICS_SYSROOT"
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
while IFS= read -r descriptor; do
    sed -i "s|^prefix=$STAGE/usr/local$|prefix=/usr/local|" "$descriptor"
done < <(find "$GRAPHICS_SYSROOT/usr/local" -name "*.pc" -type f)
echo "[DONE] Upstream libwnck $VERSION built for the akaOS desktop stack"
