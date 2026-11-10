#!/usr/bin/env bash
set -e
ROOT_DIR="$(pwd)"
BUILD_DIR="$ROOT_DIR/build"

XSCORE_C_SOURCES=(
    "$ROOT_DIR/xscore/kernel/main.c"
    "$ROOT_DIR/xscore/kernel/sched.c"
    "$ROOT_DIR/xscore/kernel/sched_linux.c"
    "$ROOT_DIR/xscore/kernel/thread.c"
    "$ROOT_DIR/xscore/kernel/file.c"
    "$ROOT_DIR/xscore/kernel/random.c"
    "$ROOT_DIR/xscore/kernel/tty.c"
    "$ROOT_DIR/xscore/kernel/input.c"
    "$ROOT_DIR/xscore/kernel/fb_console.c"
    "$ROOT_DIR/xscore/kernel/process.c"
    "$ROOT_DIR/xscore/kernel/vfs.c"
    "$ROOT_DIR/xscore/kernel/ext2_bridge.c"
    "$ROOT_DIR/xscore/kernel/time.c"
    "$ROOT_DIR/xscore/kernel/bufcache.c"
    "$ROOT_DIR/xscore/kernel/initramfs.c"
    "$ROOT_DIR/xscore/kernel/syscall.c"
    "$ROOT_DIR/xscore/kernel/usercopy.c"
    "$ROOT_DIR/xscore/kernel/user.c"
    "$ROOT_DIR/xscore/kernel/page_cache.c"
    "$ROOT_DIR/xscore/kernel/audio.c"
    "$ROOT_DIR/xscore/kernel/audio_linux.c"
    "$ROOT_DIR/xscore/kernel/net.c"
    "$ROOT_DIR/xscore/kernel/net_core.c"
    "$ROOT_DIR/xscore/kernel/net_virtio_bridge.c"
    "$ROOT_DIR/xscore/kernel/socket.c"
    "$ROOT_DIR/xscore/kernel/virtio_net.c"
    "$ROOT_DIR/xscore/kernel/pci.c"
    "$ROOT_DIR/xscore/kernel/cpu.c"
    "$ROOT_DIR/xscore/kernel/gpu.c"
    "$ROOT_DIR/xscore/kernel/nvme.c"
    "$ROOT_DIR/xscore/kernel/audio_drivers.c"
    "$ROOT_DIR/xscore/kernel/virtio_blk.c"
    "$ROOT_DIR/xscore/kernel/bios.c"
    "$ROOT_DIR/xscore/kernel/acpi.c"
    "$ROOT_DIR/xscore/kernel/usb.c"
    "$ROOT_DIR/xscore/kernel/ahci.c"
    "$ROOT_DIR/xscore/kernel/storage.c"
    "$ROOT_DIR/xscore/arch/x86_64/tables.c"
    "$ROOT_DIR/xscore/arch/x86_64/stack.c"
    "$ROOT_DIR/xscore/arch/x86_64/apic.c"
    "$ROOT_DIR/xscore/arch/x86_64/timer.c"
    "$ROOT_DIR/xscore/arch/x86_64/smp.c"
    "$ROOT_DIR/xscore/memory/physical.c"
    "$ROOT_DIR/xscore/memory/virtual.c"
    "$ROOT_DIR/xscore/memory/heap.c"
)
XSCORE_OBJECTS=()
for source in "${XSCORE_C_SOURCES[@]}"; do
    object="$BUILD_DIR/$(basename "${source%.c}").o"
    if [[ ! -f "$object" || "$source" -nt "$object" ]]; then
        clang --target=x86_64-unknown-none-elf             -std=c17 -O2 -Wall -Wextra -Werror             -ffreestanding -fno-stack-protector -fno-pic -fno-pie             -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387             -DXSCORE_BUILD_TIMESTAMP='"optimized"'             -Isdk/include -Ixscore/include -Ibuild/generated             -c "$source" -o "$object"
    fi
    XSCORE_OBJECTS+=("$object")
done

clang --target=x86_64-unknown-none-elf     -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel     -c "$ROOT_DIR/xscore/arch/x86_64/entry.S" -o "$BUILD_DIR/entry.o"
XSCORE_OBJECTS+=("$BUILD_DIR/entry.o")

clang --target=x86_64-unknown-none-elf     -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel     -c "$ROOT_DIR/xscore/arch/x86_64/smp_trampoline.S" -o "$BUILD_DIR/smp_trampoline.o"
XSCORE_OBJECTS+=("$BUILD_DIR/smp_trampoline.o")

XSCORE_OBJECTS+=("$BUILD_DIR/initramfs_archive.o")

ld.lld -nostdlib -static -z max-page-size=0x1000     -T "$ROOT_DIR/xscore/arch/x86_64/linker.ld"     "${XSCORE_OBJECTS[@]}" -o "$BUILD_DIR/XSCORE.ELF"

ld.lld -nostdlib -static -z max-page-size=0x1000     -e xscore_multiboot_entry     -T "$ROOT_DIR/xscore/arch/x86_64/linker.ld"     "${XSCORE_OBJECTS[@]}" -o "$BUILD_DIR/XSCORE-BIOS.ELF"

readelf -S build/XSCORE.ELF | grep -A 1 "\.bss"
nm --size-sort -r build/XSCORE.ELF | grep -E " [bBdD] " | head -n 12
