#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/host-common.sh"
BUILD_DIR="$ROOT_DIR/build"
ARCH_DIR="$ROOT_DIR/xscore/arch/aarch64"
GENERATED_DIR="$BUILD_DIR/generated"

mkdir -p "$BUILD_DIR"

# Keep the native target self-contained when it is built directly (without
# first running the x86-64 image builder that normally generates these
# headers).
mkdir -p "$GENERATED_DIR"
FONT_FILE="$(akaos_find_dejavu_font)"
python3 "$ROOT_DIR/tools/generate_font.py" "$FONT_FILE" "$GENERATED_DIR/font_data.h"
python3 "$ROOT_DIR/tools/generate_vga_font.py" "$GENERATED_DIR/font8x16_data.h"

BUILD_TIMESTAMP="${BUILD_TIMESTAMP:-$(date +%m%d%Y%H%M%S)}"
export BUILD_TIMESTAMP

# 1. Build AArch64 userspace binaries and initramfs archive
bash "$ROOT_DIR/tools/build-user-aarch64.sh"

echo "[BUILD] akaOS AArch64 XSCore Kernel"

CFLAGS=(
    --target=aarch64-unknown-none-elf
    -std=c17 -O2 -Wall -Wextra
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
    -mgeneral-regs-only
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\""
    -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/xscore/include" -I"$BUILD_DIR/generated"
)

# Compile architecture assembly and C sources
clang --target=aarch64-unknown-none-elf \
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
    -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/xscore/include" \
    -c "$ARCH_DIR/entry.S" -o "$BUILD_DIR/xscore_aarch64_entry.o"

clang "${CFLAGS[@]}" -c "$ARCH_DIR/paging.c" -o "$BUILD_DIR/xscore_aarch64_paging.o"
clang "${CFLAGS[@]}" -c "$ARCH_DIR/main_aarch64.c" -o "$BUILD_DIR/xscore_aarch64_main.o"

# Compile core kernel modules for AArch64
XSCORE_C_SOURCES=(
    "$ROOT_DIR/xscore/arch/virt/device_irq.c"
    "$ROOT_DIR/xscore/arch/virt/secondary.c"
    "$ROOT_DIR/xscore/arch/aarch64/stack.c"
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
for src in "${XSCORE_C_SOURCES[@]}"; do
    base="$(basename "$src" .c)"
    obj="$BUILD_DIR/xscore_aarch64_$base.o"
    clang "${CFLAGS[@]}" -c "$src" -o "$obj"
    KERNEL_OBJECTS+=("$obj")
done

ld.lld -m aarch64elf \
    -T "$ARCH_DIR/linker.ld" \
    -o "$BUILD_DIR/XSCORE_AARCH64.ELF" \
    "$BUILD_DIR/xscore_aarch64_entry.o" \
    "$BUILD_DIR/xscore_aarch64_paging.o" \
    "$BUILD_DIR/xscore_aarch64_main.o" \
    "${KERNEL_OBJECTS[@]}" \
    "$BUILD_DIR/initramfs_aarch64_archive.o"

echo "[DONE] Built $BUILD_DIR/XSCORE_AARCH64.ELF"
