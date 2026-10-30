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
VERSION=1.16.2
SOURCE_ROOT="$BUILD_DIR/upstream-source"
SOURCE_DIR="$SOURCE_ROOT/dbus-$VERSION"
ARCHIVE="$SOURCE_ROOT/dbus-$VERSION.tar.xz"
TARGET_BUILD="$BUILD_DIR/upstream-build/dbus-$VERSION/x86_64-akaos"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
JOBS="${AKAOS_DBUS_BUILD_JOBS:-4}"
MESON_PATH="${AKAOS_MESON_PATH:-/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin}"
DBUS_VERBOSE_OPTION=false
if [[ "${AKAOS_DBUS_VERBOSE:-0}" == "1" ]]; then
    DBUS_VERBOSE_OPTION=true
fi

for command_name in clang llvm-ar llvm-strip meson ninja pkg-config curl sha256sum tar llvm-readelf; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing D-Bus build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld meson ninja-build pkg-config curl coreutils tar llvm" >&2
        exit 2
    fi
done
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_DBUS_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

mkdir -p "$SOURCE_ROOT" "$(dirname "$TARGET_BUILD")"
if [[ ! -f "$ARCHIVE" ]]; then
    curl -fL --retry 2 \
        "https://dbus.freedesktop.org/releases/dbus/dbus-$VERSION.tar.xz" \
        -o "$ARCHIVE"
fi
printf '%s  %s\n' \
    0ba2a1a4b16afe7bceb2c07e9ce99a8c2c3508e5dec290dbb643384bd6beb7e2 \
    "$ARCHIVE" | sha256sum -c -
if [[ ! -f "$SOURCE_DIR/meson.build" ]]; then
    tar -xf "$ARCHIVE" -C "$SOURCE_ROOT"
fi
python3 "$ROOT_DIR/tools/patch-upstream-dbus-update-activation-environment.py" \
    "$SOURCE_DIR"

for required_file in \
    "$ROOT_DIR/tools/meson-akaos-x86_64.ini" \
    "$BUILD_DIR/libc.so.6" "$BUILD_DIR/libm.so.6" \
    "$BUILD_DIR/user-dynamic-crt0.o" \
    "$ROOT_DIR/userspace/linker/user-dynamic.ld"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS D-Bus prerequisite: $required_file" >&2
        exit 2
    fi
done

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export CFLAGS="-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include -ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -I$ROOT_DIR/sdk/include -I$ROOT_DIR/userspace/libc/include"
export LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$GRAPHICS_SYSROOT/usr/local/lib -L$GRAPHICS_SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6"

MESON_ARGS=(
    --cross-file "$ROOT_DIR/tools/meson-akaos-x86_64.ini"
    --prefix=/usr/local --libdir=lib --includedir=include
    --default-library=shared --buildtype=release --wrap-mode=nofallback
    -Dmessage_bus=false -Dtools=false -Dmodular_tests=disabled
    -Dverbose_mode="$DBUS_VERBOSE_OPTION"
    -Dinstalled_tests=false -Dintrusive_tests=false
    -Ddoxygen_docs=disabled -Dducktype_docs=disabled -Dxml_docs=disabled
    -Dqt_help=disabled -Dselinux=disabled -Dapparmor=disabled
    -Dsystemd=disabled -Dlibaudit=disabled -Dx11_autolaunch=disabled
    -Dlaunchd=disabled -Dkqueue=disabled -Depoll=disabled -Dinotify=disabled
    -Dvalgrind=disabled
)
if [[ -f "$TARGET_BUILD/meson-private/coredata.dat" &&
      -f "$TARGET_BUILD/build.ninja" ]]; then
    PATH="$MESON_PATH" meson setup --reconfigure "${MESON_ARGS[@]}" "$TARGET_BUILD" "$SOURCE_DIR"
else
    PATH="$MESON_PATH" meson setup "${MESON_ARGS[@]}" "$TARGET_BUILD" "$SOURCE_DIR"
fi
PATH="$MESON_PATH" ninja -C "$TARGET_BUILD" -j "$JOBS" dbus/libdbus-1.so.3.38.3
PATH="$MESON_PATH" DESTDIR="$GRAPHICS_SYSROOT" meson install --no-rebuild -C "$TARGET_BUILD"

LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/libdbus-1.so.3.38.3"
if [[ ! -f "$LIBRARY" ]]; then
    echo "D-Bus libdbus install did not produce $LIBRARY" >&2
    exit 1
fi
ELF_INFO="$(llvm-readelf -h -d "$LIBRARY")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Library soname: [libdbus-1.so.3]' <<<"$ELF_INFO"; then
    echo "libdbus output has an invalid architecture or SONAME" >&2
    exit 1
fi

echo "[DONE] Upstream D-Bus $VERSION libdbus x86_64: $LIBRARY"

TOOL_OUTPUT="$BUILD_DIR/DBUS_UPDATE_ACTIVATION_ENVIRONMENT.ELF"
TOOL_OBJECT="$BUILD_DIR/dbus-update-activation-environment.o"
TOOL_COMMON_OBJECT="$BUILD_DIR/dbus-tool-common.o"
TOOL_CFLAGS=(
    --target=x86_64-unknown-none-elf -std=gnu17 -O2 -Wall -Wextra -Werror
    -ffreestanding -fno-builtin -fno-stack-protector -fpie
    -mno-red-zone -mcmodel=small
    -nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include
    -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/userspace/libc/include"
    -I"$TARGET_BUILD" -I"$SOURCE_DIR" -I"$SOURCE_DIR/tools"
    -I"$GRAPHICS_SYSROOT/usr/local/include/dbus-1.0"
    -I"$GRAPHICS_SYSROOT/usr/local/lib/dbus-1.0/include"
)
clang "${TOOL_CFLAGS[@]}" -c \
    "$SOURCE_DIR/tools/dbus-update-activation-environment.c" \
    -o "$TOOL_OBJECT"
clang "${TOOL_CFLAGS[@]}" -c "$SOURCE_DIR/tools/tool-common.c" \
    -o "$TOOL_COMMON_OBJECT"
ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
    --dynamic-linker /lib/ld-akaos.so \
    -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
    "$BUILD_DIR/user-dynamic-crt0.o" "$TOOL_OBJECT" \
    "$TOOL_COMMON_OBJECT" --no-as-needed "$LIBRARY" \
    "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$TOOL_OUTPUT"

TOOL_INFO="$(llvm-readelf -h -l -d "$TOOL_OUTPUT")"
if ! grep -Fq 'Class:                             ELF64' <<<"$TOOL_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$TOOL_INFO" || \
   ! grep -Fq 'Requesting program interpreter: /lib/ld-akaos.so' <<<"$TOOL_INFO" || \
   ! grep -Fq 'Shared library: [libdbus-1.so.3]' <<<"$TOOL_INFO"; then
    echo "dbus-update-activation-environment has an invalid ABI or dependency" >&2
    exit 1
fi
echo "[DONE] Upstream dbus-update-activation-environment: $TOOL_OUTPUT"
