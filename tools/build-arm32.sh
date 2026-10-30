#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/host-common.sh"
BUILD_DIR="$ROOT_DIR/build"
ARCH_DIR="$ROOT_DIR/xscore/arch/arm32"

mkdir -p "$BUILD_DIR"

GENERATED_DIR="$BUILD_DIR/generated"
mkdir -p "$GENERATED_DIR"
FONT_FILE="$(akaos_find_dejavu_font)"
python3 "$ROOT_DIR/tools/generate_font.py" "$FONT_FILE" "$GENERATED_DIR/font_data.h"
python3 "$ROOT_DIR/tools/generate_vga_font.py" "$GENERATED_DIR/font8x16_data.h"

BUILD_TIMESTAMP="${BUILD_TIMESTAMP:-$(date +%m%d%Y%H%M%S)}"
export BUILD_TIMESTAMP

# Build the real ARM EABI userspace and embed its initramfs before linking the
# kernel. This target is a direct QEMU virt image, not an x86 firmware disk.
bash "$ROOT_DIR/tools/build-user-arm32.sh"

echo "[BUILD] akaOS ARM32 XSCore kernel"

CFLAGS=(
    --target=armv7-unknown-none-eabi
    -mcpu=cortex-a7 -marm -mfloat-abi=soft
    -std=c17 -O2 -Wall -Wextra
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\""
    -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/xscore/include" -I"$GENERATED_DIR"
)

clang --target=armv7-unknown-none-eabi -mcpu=cortex-a7 -marm -mfloat-abi=soft \
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
    -c "$ARCH_DIR/entry.S" -o "$BUILD_DIR/xscore_arm32_entry.o"

clang "${CFLAGS[@]}" -c "$ARCH_DIR/paging.c" -o "$BUILD_DIR/xscore_arm32_paging.o"
clang "${CFLAGS[@]}" -c "$ARCH_DIR/main_arm32.c" -o "$BUILD_DIR/xscore_arm32_main.o"

XSCORE_C_SOURCES=(
    "$ROOT_DIR/xscore/arch/virt/device_irq.c"
    "$ROOT_DIR/xscore/arch/virt/secondary.c"
    "$ARCH_DIR/stack.c"
    "$ROOT_DIR/xscore/kernel/vfs.c"
    "$ROOT_DIR/xscore/kernel/time.c"
    "$ROOT_DIR/xscore/kernel/pci.c"
    "$ROOT_DIR/xscore/kernel/cpu.c"
    "$ROOT_DIR/xscore/kernel/gpu.c"
    "$ROOT_DIR/xscore/kernel/audio_drivers.c"
    "$ROOT_DIR/xscore/kernel/acpi.c"
    "$ROOT_DIR/xscore/kernel/audio.c"
    "$ROOT_DIR/xscore/kernel/net.c"
    "$ROOT_DIR/xscore/kernel/socket.c"
    "$ROOT_DIR/xscore/kernel/virtio_net.c"
    "$ROOT_DIR/xscore/kernel/file.c"
    "$ROOT_DIR/xscore/kernel/random.c"
    "$ROOT_DIR/xscore/kernel/tty.c"
    "$ROOT_DIR/xscore/kernel/input.c"
    "$ROOT_DIR/xscore/kernel/fb_console.c"
    "$ROOT_DIR/xscore/kernel/virtio_input.c"
    "$ROOT_DIR/xscore/kernel/virtio_blk_mmio.c"
    "$ROOT_DIR/xscore/kernel/initramfs.c"
    "$ROOT_DIR/xscore/kernel/bufcache.c"
    "$ROOT_DIR/xscore/kernel/process.c"
    "$ROOT_DIR/xscore/kernel/native_context.c"
    "$ROOT_DIR/xscore/kernel/page_cache.c"
    "$ROOT_DIR/xscore/kernel/syscall.c"
    "$ROOT_DIR/xscore/kernel/usercopy.c"
    "$ROOT_DIR/xscore/kernel/thread.c"
    "$ROOT_DIR/xscore/kernel/sched.c"
    "$ROOT_DIR/xscore/memory/physical.c"
    "$ROOT_DIR/xscore/memory/heap.c"
)

KERNEL_OBJECTS=()
for source in "${XSCORE_C_SOURCES[@]}"; do
    base="$(basename "$source" .c)"
    object="$BUILD_DIR/xscore_arm32_$base.o"
    clang "${CFLAGS[@]}" -c "$source" -o "$object"
    KERNEL_OBJECTS+=("$object")
done

ld.lld -m armelf -T "$ARCH_DIR/linker.ld" \
    -o "$BUILD_DIR/XSCORE_ARM32.ELF" \
    "$BUILD_DIR/xscore_arm32_entry.o" \
    "$BUILD_DIR/xscore_arm32_paging.o" \
    "$BUILD_DIR/xscore_arm32_main.o" \
    "${KERNEL_OBJECTS[@]}" \
    "$BUILD_DIR/initramfs_arm32_archive.o"

echo "[DONE] Built $BUILD_DIR/XSCORE_ARM32.ELF"
