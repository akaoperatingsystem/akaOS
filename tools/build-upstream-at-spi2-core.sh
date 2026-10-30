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

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
VERSION=2.56.8
SOURCE_ROOT="$BUILD_DIR/upstream-source"
SOURCE_DIR="$SOURCE_ROOT/at-spi2-core-$VERSION"
ARCHIVE="$SOURCE_ROOT/at-spi2-core-$VERSION.tar.xz"
PROJECT_SOURCE_TREE="$SOURCE_DIR/extracted"
PROJECT_BUILD_TREE="$BUILD_DIR/upstream-build/at-spi2-core-$VERSION/x86_64-akaos"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
DESTDIR_ROOT="${AKAOS_ATSPI2_DESTDIR:-$GRAPHICS_SYSROOT}"
HOST_GLIB_TOOLS="$BUILD_DIR/host-tools/glib-2.88"
HOST_GLIB_PKGCONFIG="$BUILD_DIR/host-tools/at-spi2-glib-pkgconfig"
ATSPI2_BUILD_JOBS="${AKAOS_ATSPI2_BUILD_JOBS:-4}"

# The WSL DrvFS mount is very slow for repeated compiler runs, so the build
# accepts an ext4 work root that holds the source and build trees.
if [[ -n "${AKAOS_ATSPI2_WORKROOT:-}" ]]; then
    SOURCE_TREE="$AKAOS_ATSPI2_WORKROOT/source"
    BUILD_TREE="$AKAOS_ATSPI2_WORKROOT/build"
    mkdir -p "$AKAOS_ATSPI2_WORKROOT"
else
    SOURCE_TREE="$PROJECT_SOURCE_TREE"
    BUILD_TREE="$PROJECT_BUILD_TREE"
fi

for command_name in curl sha256sum tar clang meson ninja pkg-config python3 llvm-readelf; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream AT-SPI2 core build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld meson ninja-build pkg-config python3 llvm" >&2
        exit 2
    fi
done
if [[ ! "$ATSPI2_BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_ATSPI2_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

mkdir -p "$SOURCE_ROOT" "$(dirname "$PROJECT_BUILD_TREE")"
if [[ ! -f "$ARCHIVE" ]]; then
    curl -fL --retry 2 \
        "https://download.gnome.org/sources/at-spi2-core/2.56/at-spi2-core-$VERSION.tar.xz" \
        -o "$ARCHIVE"
fi
printf '%s  %s\n' \
    b0e0037c3386811f06730a3c5e3e86532ebe3ecb7a7a94eb5002ed4f573b2f69 \
    "$ARCHIVE" | sha256sum -c -
if [[ ! -f "$PROJECT_SOURCE_TREE/meson.build" ]]; then
    mkdir -p "$PROJECT_SOURCE_TREE"
    tar -xf "$ARCHIVE" -C "$PROJECT_SOURCE_TREE" --strip-components=1
fi
if [[ "$SOURCE_TREE" != "$PROJECT_SOURCE_TREE" && ! -f "$SOURCE_TREE/meson.build" ]]; then
    mkdir -p "$SOURCE_TREE"
    cp -a "$PROJECT_SOURCE_TREE/." "$SOURCE_TREE/"
fi

# Upstream at-spi2-core requires libxml-2.0 unconditionally, but the only
# consumer of that dependency is the at-spi2-atk test helper, which uses
# libxml2 to load XML accessibility trees.  The akaOS graphics sysroot has no
# libxml2, so the dependency is made optional and the test programs are put
# behind a "tests" option that akaOS disables.
python3 - "$SOURCE_TREE" <<'PATCH'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
options = root / "meson_options.txt"
build = root / "meson.build"

text = options.read_text()
if "option('tests'" not in text:
    options.write_text(text + """
option('tests',
       description: 'Build the test programs',
       type: 'boolean',
       value: true)
""")

text = build.read_text()
text = text.replace(
    "libxml_dep = dependency('libxml-2.0', version: libxml_req_version)",
    "libxml_dep = dependency('libxml-2.0', version: libxml_req_version, required: false)")
text = text.replace(
    "  subdir('tests')\n",
    "  if get_option('tests')\n    subdir('tests')\n  endif\n")
# The upstream dlopen probe compiles a test program that declares its own
# "char dlopen (void)", which clashes with the akaOS libc prototype, so the
# probe always fails.  The akaOS libc exports dlopen itself and is linked
# into every target, so no separate library is needed for it.
text = text.replace(
    """  if cc.has_function('dlopen')
    dl_dep = []
  elif cc.has_function('dlopen', args: '-ldl')
    dl_dep = cc.find_library('dl')
  else
    error('Could not find a library with the dlopen function')
  endif
""",
    """  dl_dep = []
""")
if "required: false" not in text or "if get_option('tests')" not in text:
    raise SystemExit("the at-spi2-core meson.build patch did not apply")
if "cc.has_function('dlopen')" in text:
    raise SystemExit("the at-spi2-core dlopen patch did not apply")
build.write_text(text)
PATCH

# Meson runs glib-genmarshal, glib-mkenums and gobject-query on the build
# host, so the upstream GLib command line tools are staged next to the other
# akaOS host tools.
if ! command -v glib-genmarshal >/dev/null 2>&1 || \
   ! command -v glib-mkenums >/dev/null 2>&1 || \
   ! command -v gobject-query >/dev/null 2>&1; then
    if [[ ! -x "$HOST_GLIB_TOOLS/root/usr/bin/glib-genmarshal" ]]; then
        mkdir -p "$HOST_GLIB_TOOLS"
        (cd "$HOST_GLIB_TOOLS" && apt-get download libgio-2.0-dev-bin)
        dpkg-deb -x "$HOST_GLIB_TOOLS"/libgio-2.0-dev-bin_*.deb \
            "$HOST_GLIB_TOOLS/root"
    fi
    export PATH="$HOST_GLIB_TOOLS/root/usr/bin:$PATH"
fi

mkdir -p "$HOST_GLIB_PKGCONFIG"
cp "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" \
    "$HOST_GLIB_PKGCONFIG/glib-2.0.pc"
cp "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gio-2.0.pc" \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_genmarshal=.*#glib_genmarshal=glib-genmarshal#' \
    "$HOST_GLIB_PKGCONFIG/glib-2.0.pc"
sed -i 's#^gobject_query=.*#gobject_query=gobject-query#' \
    "$HOST_GLIB_PKGCONFIG/glib-2.0.pc"
sed -i 's#^glib_mkenums=.*#glib_mkenums=glib-mkenums#' \
    "$HOST_GLIB_PKGCONFIG/glib-2.0.pc"
sed -i 's#^glib_compile_resources=.*#glib_compile_resources=glib-compile-resources#' \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"
sed -i 's#^glib_compile_schemas=.*#glib_compile_schemas=glib-compile-schemas#' \
    "$HOST_GLIB_PKGCONFIG/gio-2.0.pc"

for required_file in \
    "$ROOT_DIR/tools/meson-akaos-x86_64.ini" \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gio-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gobject-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gmodule-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/dbus-1.pc"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing upstream AT-SPI2 core prerequisite: $required_file" >&2
        exit 2
    fi
done

export PKG_CONFIG_LIBDIR="$HOST_GLIB_PKGCONFIG:$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

meson_args=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    -Ddefault_library=shared -Dbuildtype=release -Doptimization=2
    --wrap-mode=nofallback
    -Datk_only=false -Dtests=false -Ddocs=false -Dintrospection=disabled
    -Dgtk2_atk_adaptor=false -Dx11=disabled
    -Ddbus_daemon=/usr/local/bin/dbus-daemon
)
if [[ -f "$BUILD_TREE/meson-private/coredata.dat" && -f "$BUILD_TREE/build.ninja" ]]; then
    meson setup --reconfigure "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
else
    meson setup "${meson_args[@]}" "$BUILD_TREE" "$SOURCE_TREE"
fi

meson compile -C "$BUILD_TREE" --jobs "$ATSPI2_BUILD_JOBS"
DESTDIR="$DESTDIR_ROOT" meson install -C "$BUILD_TREE"

BRIDGE_LIBRARY="$DESTDIR_ROOT/usr/local/lib/libatk-bridge-2.0.so.0"
BRIDGE_PKGCONFIG="$DESTDIR_ROOT/usr/local/lib/pkgconfig/atk-bridge-2.0.pc"
if [[ ! -f "$BRIDGE_PKGCONFIG" ]]; then
    echo "Upstream AT-SPI2 core did not install atk-bridge-2.0.pc" >&2
    exit 1
fi
if [[ ! -e "$BRIDGE_LIBRARY" ]]; then
    echo "Upstream AT-SPI2 core did not install libatk-bridge-2.0.so.0" >&2
    exit 1
fi
BRIDGE_INFO="$(llvm-readelf -h -d "$BRIDGE_LIBRARY")"
if ! grep -Fq 'Class:                             ELF64' <<<"$BRIDGE_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$BRIDGE_INFO" || \
   ! grep -Fq 'Library soname: [libatk-bridge-2.0.so.0]' <<<"$BRIDGE_INFO"; then
    echo "libatk-bridge-2.0.so.0 has an invalid architecture or SONAME" >&2
    exit 1
fi
BRIDGE_SYMBOLS="$(llvm-readelf --dyn-syms "$BRIDGE_LIBRARY")"
for required_symbol in \
    atk_bridge_adaptor_init \
    atk_bridge_adaptor_cleanup \
    atk_bridge_set_event_context; do
    if ! grep -Fq "$required_symbol" <<<"$BRIDGE_SYMBOLS"; then
        echo "libatk-bridge-2.0.so.0 does not export $required_symbol" >&2
        exit 1
    fi
done

# The bridge needs ATK symbols that the ATK release in the graphics sysroot
# does not provide, so the ATK library that upstream installs alongside the
# bridge has to stay in place.
ATK_LIBRARY="$DESTDIR_ROOT/usr/local/lib/libatk-1.0.so.0"
if [[ ! -e "$ATK_LIBRARY" ]]; then
    echo "Upstream AT-SPI2 core did not install its ATK library" >&2
    exit 1
fi
ATK_SYMBOLS="$(llvm-readelf --dyn-syms "$ATK_LIBRARY" | awk '$7 != "UND" {print $8}')"
mapfile -t BRIDGE_ATK_SYMBOLS < <(
    llvm-readelf --dyn-syms "$BRIDGE_LIBRARY" | awk '$7 == "UND" {print $8}' |
        grep '^atk_' | sort -u
)
for required_symbol in "${BRIDGE_ATK_SYMBOLS[@]}"; do
    if ! grep -Fxq "$required_symbol" <<<"$ATK_SYMBOLS"; then
        echo "libatk-bridge-2.0.so.0 needs $required_symbol, which the installed ATK does not export" >&2
        exit 1
    fi
done

echo "[DONE] Upstream AT-SPI2 core $VERSION x86_64 build: $BUILD_TREE"
echo "[DONE] Upstream AT-SPI2 core $VERSION atk-bridge install: $BRIDGE_LIBRARY"
