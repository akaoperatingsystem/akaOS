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

# Build the upstream XML parser that the LXDE window manager needs to read its
# menu file.  The parser is an autotools package, so it is configured with the
# same cross compiler wrapper the X.Org client stack uses.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
VERSION="2.13.8"
SHA256="277294cb33119ab71b2bc81f2f445e9bc9435b893ad15bb2cd2b0e859a0ee84a"
URL="https://download.gnome.org/sources/libxml2/2.13/libxml2-2.13.8.tar.xz"
WORKROOT="${AKAOS_LIBXML2_WORKROOT:-$HOME/.cache/akaos/libxml2}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
LOG_DIR="$WORKROOT/logs"
STAGE_PREFIX="$STAGE/usr/local"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"

for command_name in curl sha256sum tar clang pkg-config make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream libxml2 build tool: $command_name" >&2
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

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

ARCHIVE="$SRC_DIR/libxml2-$VERSION.tar.xz"
if [[ ! -s "$ARCHIVE" ]]; then
    echo "[libxml2] fetching libxml2 $VERSION"
    curl -fL --retry 3 --max-time 600 -o "$ARCHIVE.part" "$URL" ||
        { echo "[libxml2] download failed: $URL" >&2; exit 1; }
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
actual_sha="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
if [[ "$actual_sha" != "$SHA256" ]]; then
    echo "[libxml2] checksum mismatch" >&2
    echo "  expected $SHA256" >&2
    echo "  actual   $actual_sha" >&2
    exit 1
fi

SOURCE_DIR="$SRC_DIR/libxml2-$VERSION"
if [[ ! -f "$SOURCE_DIR/.akaos-extracted" ]]; then
    echo "[libxml2] extracting libxml2 $VERSION"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
    tar -xf "$ARCHIVE" -C "$SOURCE_DIR" --strip-components=1
    touch "$SOURCE_DIR/.akaos-extracted"
fi

BUILD_DIR_PATH="$BUILD_ROOT/libxml2-$VERSION"
if [[ -f "$STAGE/usr/local/lib/libxml2.so.2" &&
      -f "$STAGE/usr/local/lib/pkgconfig/libxml-2.0.pc" ]]; then
    echo "[libxml2] already staged"
else
    echo "[libxml2] configuring libxml2 $VERSION"
    rm -rf "$BUILD_DIR_PATH"
    mkdir -p "$BUILD_DIR_PATH"
    (
        cd "$BUILD_DIR_PATH" || exit 1
        env \
            CC="$CC_WRAPPER" \
            CFLAGS="-O2 -g0 -fno-strict-aliasing" \
            CPPFLAGS="-I$GRAPHICS_SYSROOT/usr/local/include" \
            LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR -Wl,-l:libm.so.6 -Wl,-l:libc.so.6" \
            LIBS="" \
            PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig" \
            PKG_CONFIG_SYSROOT_DIR="" \
            "$SOURCE_DIR/configure" \
                --host=x86_64-pc-linux-gnu \
                --prefix="$STAGE_PREFIX" \
                --disable-static \
                --enable-shared \
                --without-python \
                --without-lzma \
                --without-http \
                --without-ftp \
                --without-iconv
    ) >"$LOG_DIR/libxml2-configure.log" 2>&1 || {
        echo "[libxml2] configure failed" >&2
        tail -40 "$LOG_DIR/libxml2-configure.log" >&2
        exit 1
    }

    echo "[libxml2] building libxml2 $VERSION"
    # Only the shared library is needed.  The command line tools would have to
    # be linked against the akaOS startup files, which this builder does not
    # set up, and nothing in a desktop environment needs them.
    make -C "$BUILD_DIR_PATH" -j"$(nproc)" libxml2.la >"$LOG_DIR/libxml2-build.log" 2>&1 || {
        echo "[libxml2] build failed" >&2
        tail -40 "$LOG_DIR/libxml2-build.log" >&2
        exit 1
    }
    make -C "$BUILD_DIR_PATH" install-libLTLIBRARIES install-pkgconfigDATA \
        >"$LOG_DIR/libxml2-install.log" 2>&1 || {
        echo "[libxml2] install failed" >&2
        tail -30 "$LOG_DIR/libxml2-install.log" >&2
        exit 1
    }
fi

# The headers live in a subdirectory whose install rule is part of the
# recursive install, and the recursive install also builds the command line
# tools.  The headers are copied on their own instead, and the version header
# that the build generates is copied with them.
mkdir -p "$STAGE/usr/local/include/libxml"
for header in "$SOURCE_DIR/include/libxml/"*.h; do
    [[ -e "$header" ]] || continue
    cp -f "$header" "$STAGE/usr/local/include/libxml/"
done
for header in "$BUILD_DIR_PATH/include/libxml/"*.h; do
    [[ -e "$header" ]] || continue
    cp -f "$header" "$STAGE/usr/local/include/libxml/"
done

if [[ ! -f "$STAGE/usr/local/lib/libxml2.so.2" ]]; then
    echo "[libxml2] did not install its shared library" >&2
    exit 1
fi

# Publish into the graphics sysroot, then move the package description beside
# the others so that the one directory every package search path contains holds
# the whole set.
mkdir -p "$GRAPHICS_SYSROOT/usr/local"
for subtree in include lib share; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$GRAPHICS_SYSROOT/usr/local/$subtree/"
    fi
done
while IFS= read -r descriptor; do
    sed -i "s|^prefix=$STAGE_PREFIX$|prefix=/usr/local|" "$descriptor"
done < <(find "$GRAPHICS_SYSROOT/usr/local" -name "*.pc" -type f)
mkdir -p "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig"
for descriptor in "$GRAPHICS_SYSROOT/usr/local/share/pkgconfig"/*.pc; do
    [[ -e "$descriptor" ]] || continue
    cp -f "$descriptor" "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/$(basename "$descriptor")"
done

if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/libxml-2.0.pc" ]]; then
    echo "[libxml2] the package description is missing from the graphics sysroot" >&2
    exit 1
fi

echo "[DONE] Upstream libxml2 $VERSION built for the LXDE window manager"
