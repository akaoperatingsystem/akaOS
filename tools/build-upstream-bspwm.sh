#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
BSPWM_SOURCE="$BUILD_DIR/upstream-source/bspwm-0.9.10"
BSPWM_BUILD="$BUILD_DIR/upstream-build/bspwm-0.9.10/x86_64-akaos"

mkdir -p "$BSPWM_BUILD"
cd "$BSPWM_SOURCE"

CFLAGS=(
    --target=x86_64-unknown-none-elf
    -std=c99 -Os -Wall -Wextra -DJSMN_STRICT
    -ffreestanding -fno-builtin -fno-stack-protector -fpie
    -mno-red-zone -mcmodel=small
    -D_POSIX_C_SOURCE=200809L -DVERSION=\"0.9.10\"
    -I"$ROOT_DIR/sdk/include"
    -I"$ROOT_DIR/userspace/libc/include"
    -I"$GRAPHICS_SYSROOT/usr/local/include"
    -I"$GRAPHICS_SYSROOT/usr/include"
)

WM_SRCS=(
    bspwm helpers geometry jsmn settings monitor desktop tree stack history
    events pointer window messages parse query restore rule ewmh subscribe
)

wm_objs=()
for src in "${WM_SRCS[@]}"; do
    echo "[COMPILE] src/$src.c"
    obj="$BSPWM_BUILD/$src.o"
    clang "${CFLAGS[@]}" -c "src/$src.c" -o "$obj"
    wm_objs+=("$obj")
done

echo "[COMPILE] src/bspc.c"
clang "${CFLAGS[@]}" -c "src/bspc.c" -o "$BSPWM_BUILD/bspc.o"

echo "[LINK] bspwm"
ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000     --allow-shlib-undefined     --dynamic-linker /lib/ld-akaos.so     -T "$ROOT_DIR/userspace/linker/user-dynamic.ld"     "$BUILD_DIR/user-dynamic-crt0.o"     "${wm_objs[@]}"     --no-as-needed     -L"$GRAPHICS_SYSROOT/usr/local/lib"     -L"$BUILD_DIR"     -lxcb -lxcb-util -lxcb-keysyms -lxcb-icccm -lxcb-ewmh -lxcb-randr -lxcb-xinerama -lxcb-shape     "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6"     -o "$BSPWM_BUILD/bspwm"

echo "[LINK] bspc"
ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000     --allow-shlib-undefined     --dynamic-linker /lib/ld-akaos.so     -T "$ROOT_DIR/userspace/linker/user-dynamic.ld"     "$BUILD_DIR/user-dynamic-crt0.o"     "$BSPWM_BUILD/bspc.o" "$BSPWM_BUILD/helpers.o"     --no-as-needed     -L"$GRAPHICS_SYSROOT/usr/local/lib"     -L"$BUILD_DIR"     -lxcb     "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6"     -o "$BSPWM_BUILD/bspc"

mkdir -p "$GRAPHICS_SYSROOT/usr/local/bin"
cp -f "$BSPWM_BUILD/bspwm" "$GRAPHICS_SYSROOT/usr/local/bin/bspwm"
cp -f "$BSPWM_BUILD/bspc" "$GRAPHICS_SYSROOT/usr/local/bin/bspc"

mkdir -p "$GRAPHICS_SYSROOT/etc/xdg/bspwm"
if [[ -f examples/bspwmrc ]]; then
    cp -f examples/bspwmrc "$GRAPHICS_SYSROOT/etc/xdg/bspwm/bspwmrc"
    chmod 0755 "$GRAPHICS_SYSROOT/etc/xdg/bspwm/bspwmrc"
fi
if [[ -f examples/sxhkdrc ]]; then
    mkdir -p "$GRAPHICS_SYSROOT/etc/xdg/sxhkd"
    cp -f examples/sxhkdrc "$GRAPHICS_SYSROOT/etc/xdg/sxhkd/sxhkdrc"
fi

echo "[DONE] bspwm and bspc built successfully:"
ls -lh "$GRAPHICS_SYSROOT/usr/local/bin/bspwm" "$GRAPHICS_SYSROOT/usr/local/bin/bspc"
