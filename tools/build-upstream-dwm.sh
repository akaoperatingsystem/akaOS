#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
DWM_SOURCE="$BUILD_DIR/upstream-source/dwm-6.5"
DWM_BUILD="$BUILD_DIR/upstream-build/dwm-6.5/x86_64-akaos"

mkdir -p "$DWM_BUILD"
cd "$DWM_SOURCE"

if [[ ! -f "config.h" ]]; then
    cp config.def.h config.h
fi

CFLAGS=(
    --target=x86_64-unknown-none-elf
    -std=c99 -Os -Wall -Wextra -Wno-deprecated-declarations
    -ffreestanding -fno-builtin -fno-stack-protector -fpie
    -mno-red-zone -mcmodel=small
    -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700L
    -DVERSION=\"6.5\" -DXINERAMA
    -I"$ROOT_DIR/sdk/include"
    -I"$ROOT_DIR/userspace/libc/include"
    -I"$GRAPHICS_SYSROOT/usr/local/include"
    -I"$GRAPHICS_SYSROOT/usr/local/include/freetype2"
    -I"$GRAPHICS_SYSROOT/usr/local/include/X11"
)

echo "[COMPILE] drw.c"
clang "${CFLAGS[@]}" -c drw.c -o "$DWM_BUILD/drw.o"

echo "[COMPILE] util.c"
clang "${CFLAGS[@]}" -c util.c -o "$DWM_BUILD/util.o"

echo "[COMPILE] dwm.c"
clang "${CFLAGS[@]}" -c dwm.c -o "$DWM_BUILD/dwm.o"

echo "[LINK] dwm"
ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 --allow-shlib-undefined     --dynamic-linker /lib/ld-akaos.so     -T "$ROOT_DIR/userspace/linker/user-dynamic.ld"     "$BUILD_DIR/user-dynamic-crt0.o"     "$DWM_BUILD/drw.o" "$DWM_BUILD/util.o" "$DWM_BUILD/dwm.o"     --no-as-needed     -L"$GRAPHICS_SYSROOT/usr/local/lib"     -L"$BUILD_DIR"     -lX11 -lXinerama -lXft -lfontconfig -lfreetype -lXrender -lXext     "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6"     -o "$DWM_BUILD/dwm"

mkdir -p "$GRAPHICS_SYSROOT/usr/local/bin"
cp -f "$DWM_BUILD/dwm" "$GRAPHICS_SYSROOT/usr/local/bin/dwm"

echo "[DONE] dwm 6.5 built successfully:"
ls -lh "$GRAPHICS_SYSROOT/usr/local/bin/dwm"
