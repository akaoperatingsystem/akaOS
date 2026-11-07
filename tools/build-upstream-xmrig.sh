#!/usr/bin/env bash
set -euo pipefail

# Build standalone static PIE xmrig compute engine binary for akaOS
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
STAGE_DIR="$BUILD_DIR/upstream-sysroot/graphics-x86_64/usr/local/bin"
SOURCE_DIR="$ROOT_DIR/userspace/bin/xmrig"
MAIN_OBJECT="$BUILD_DIR/xmrig-main.o"

mkdir -p "$BUILD_DIR" "$STAGE_DIR"

clang --target=x86_64-unknown-none-elf \
    -std=c17 -Os -Wall -Wextra \
    -ffreestanding -fno-builtin -fno-stack-protector -fpie \
    -ffunction-sections -fdata-sections \
    -mno-red-zone -mcmodel=small \
    -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/userspace/libc/include" \
    -I"$ROOT_DIR/xscore/include" \
    -c "$SOURCE_DIR/main.c" -o "$MAIN_OBJECT"

ld.lld -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "$MAIN_OBJECT" \
    --whole-archive "$BUILD_DIR/user-libc-crt_init.o" --no-whole-archive \
    "$BUILD_DIR/libc.a" \
    -o "$BUILD_DIR/XMRIG.ELF"

cp -f "$BUILD_DIR/XMRIG.ELF" "$STAGE_DIR/xmrig"
chmod +x "$STAGE_DIR/xmrig"
echo "[DONE] Built standalone static PIE xmrig: $BUILD_DIR/XMRIG.ELF -> $STAGE_DIR/xmrig"
