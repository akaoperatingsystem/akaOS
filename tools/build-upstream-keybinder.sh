#!/usr/bin/env bash
set -eo pipefail
# Build the unmodified upstream keybinder-3.0 library for akaOS x86-64.
#
# libkeybinder-3.0 is the global keyboard shortcut library that lxpanel links
# for its hotkey buttons.  The Debian source archive does not ship a generated
# configure script, so the build generates one with the host autotools and a
# staged host copy of gtk-docize (the source's automake files include the
# gtk-doc rules).
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
WORKROOT="$AKAOS_KEYBINDER_WORKROOT"
[[ -n "$WORKROOT" ]] || WORKROOT="$HOME/.cache/akaos/keybinder"
JOBS="$AKAOS_KEYBINDER_BUILD_JOBS"
[[ -n "$JOBS" ]] || JOBS=4
SRC_DIR="$WORKROOT/src"
BUILD_TREE="$WORKROOT/build/libkeybinder-3.0"
LOG_DIR="$WORKROOT/logs"
HOST_GTK_DOC="$HOME/.cache/akaos/host-tools/gtk-doc"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
VERSION="0.3.2"
SHA256="2eec50be6dfafa672d719e719f639df10403f270b2473e60deb2fb8455d13c51"
URL="https://deb.debian.org/debian/pool/main/k/keybinder-3.0/keybinder-3.0_0.3.2.orig.tar.gz"
ARCHIVE="keybinder-3.0_0.3.2.orig.tar.gz"

for command_name in curl sha256sum tar autoconf automake libtoolize pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream keybinder build tool: $command_name" >&2
        exit 2
    fi
done
if [[ ! -x "$CC_WRAPPER" ]]; then
    echo "Missing cross compiler wrapper: $CC_WRAPPER" >&2
    exit 2
fi
if [[ ! -f "$BUILD_DIR/libc.so.6" || ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/gtk+-3.0.pc" ]]; then
    echo "Missing akaOS libc or upstream GTK 3; build those first." >&2
    exit 2
fi

mkdir -p "$SRC_DIR" "$WORKROOT/build" "$LOG_DIR"
if [[ ! -s "$SRC_DIR/$ARCHIVE" ]]; then
    echo "[keybinder] fetching keybinder-3.0 $VERSION"
    curl -fL --retry 3 --max-time 300 -o "$SRC_DIR/$ARCHIVE.part" "$URL" || { echo "[keybinder] download failed" >&2; exit 1; }
    mv "$SRC_DIR/$ARCHIVE.part" "$SRC_DIR/$ARCHIVE"
fi
actual_sha="$(sha256sum "$SRC_DIR/$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[keybinder] checksum mismatch" >&2
    exit 1
fi
SOURCE_TREE="$SRC_DIR/keybinder-3.0"
# The source tree is regenerated on every run, because the autotools run
# writes generated files into it and a stale tree hides a generation error.
rm -rf "$SOURCE_TREE"
mkdir -p "$SOURCE_TREE"
tar -xf "$SRC_DIR/$ARCHIVE" -C "$SOURCE_TREE" --strip-components=1

# Automake includes the gtk-doc rules of the project.  The host package of
# gtk-doc provides the macro and the rules; they are staged from the signed
# Ubuntu archive and copied into the source tree instead of installing the
# package on the build host.
mkdir -p "$HOST_GTK_DOC"
if [[ ! -f "$HOST_GTK_DOC/root/usr/share/aclocal/gtk-doc.m4" ]]; then
    (cd "$HOST_GTK_DOC" && apt-get download gtk-doc-tools)
    dpkg-deb -x "$HOST_GTK_DOC"/gtk-doc-tools_*.deb "$HOST_GTK_DOC/root"
fi
mkdir -p "$SOURCE_TREE/m4"
cp -f "$HOST_GTK_DOC/root/usr/share/aclocal/gtk-doc.m4" "$SOURCE_TREE/m4/gtk-doc.m4"
cp -f "$HOST_GTK_DOC/root/usr/share/gtk-doc/data/gtk-doc.make" "$SOURCE_TREE/gtk-doc.make"
(
    cd "$SOURCE_TREE" || exit 1
    touch ChangeLog
    aclocal -I m4
    libtoolize --copy --force
    autoheader
    automake --add-missing --copy --foreign
    autoconf
) >"$LOG_DIR/keybinder-autoreconf.log" 2>&1 || {
    echo "[keybinder] autotools generation failed" >&2
    tail -30 "$LOG_DIR/keybinder-autoreconf.log" >&2
    exit 1
}
if [[ ! -f "$SOURCE_TREE/configure" ]]; then
    echo "[keybinder] configure was not generated" >&2
    exit 1
fi

rm -rf "$BUILD_TREE"
mkdir -p "$BUILD_TREE"
(
    cd "$BUILD_TREE" || exit 1
    env CC="$CC_WRAPPER" \
        CFLAGS="-O2 -g0 -fno-strict-aliasing" \
        CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include" \
        LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
        LIBS="" \
        PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig" \
        PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT" \
        "$SOURCE_TREE/configure" --host=x86_64-pc-linux-gnu --prefix=/usr/local --disable-static --enable-shared --disable-gtk-doc --disable-gtk-doc-html --disable-dependency-tracking
) >"$LOG_DIR/keybinder-configure.log" 2>&1 || {
    echo "[keybinder] configure failed; see $LOG_DIR/keybinder-configure.log" >&2
    tail -40 "$LOG_DIR/keybinder-configure.log" >&2
    exit 1
}
make -C "$BUILD_TREE" -j"$JOBS" >"$LOG_DIR/keybinder-build.log" 2>&1 || {
    echo "[keybinder] build failed; see $LOG_DIR/keybinder-build.log" >&2
    tail -40 "$LOG_DIR/keybinder-build.log" >&2
    exit 1
}
make -C "$BUILD_TREE" install DESTDIR="$GRAPHICS_SYSROOT" >"$LOG_DIR/keybinder-install.log" 2>&1 || {
    echo "[keybinder] install failed; see $LOG_DIR/keybinder-install.log" >&2
    tail -30 "$LOG_DIR/keybinder-install.log" >&2
    exit 1
}
if [[ -z "$(find "$GRAPHICS_SYSROOT/usr/local" -name 'keybinder-3.0.pc' -print -quit)" ]]; then
    echo "[keybinder] did not install keybinder-3.0.pc" >&2
    exit 1
fi
rm -f "$GRAPHICS_SYSROOT/usr/local/lib/libkeybinder-3.0.la"
echo "[DONE] Upstream keybinder-3.0 $VERSION built for akaOS"
