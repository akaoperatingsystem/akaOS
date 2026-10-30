#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/host-common.sh"
BUILD_DIR="$ROOT_DIR/build"
ARCH_DIR="$ROOT_DIR/xscore/arch/x86_32"
GENERATED_DIR="$BUILD_DIR/generated"

mkdir -p "$BUILD_DIR"

build_iso=0
build_fat32=0
test_mode=0
verbose_mode=0
firmware=both

while [[ $# -gt 0 ]]; do
    case "$1" in
        --disk-type)
            case "${2:-}" in
                iso) build_iso=1 ;;
                fat32) build_fat32=1 ;;
            esac
            shift 2
            ;;
        --firmware)
            firmware="${2:-}"
            shift 2
            ;;
        --iso)
            build_iso=1
            shift
            ;;
        --test)
            test_mode=1
            shift
            ;;
        --verbose)
            verbose_mode=1
            shift
            ;;
        *)
            shift
            ;;
    esac
done

if [[ "$firmware" != "uefi" && "$firmware" != "bios" && "$firmware" != "both" ]]; then
    echo "Unsupported x86_32 firmware '$firmware' (expected uefi, bios, or both)" >&2
    exit 2
fi

# BIOS cannot execute a FAT filesystem directly, so a FAT32 request paired
# with BIOS also emits the bootable ISO fallback. This keeps every documented
# selector combination useful without silently dropping the requested FAT32
# artifact.
if [[ "$build_fat32" -eq 1 && ( "$firmware" == "bios" || "$firmware" == "both" ) ]]; then
    build_iso=1
fi

# The native x86_32 kernel uses the same splash and VGA console font headers
# as the other graphical kernel targets. Generate them for a clean build so
# this target does not depend on an earlier x86_64 or AArch64 build.
mkdir -p "$GENERATED_DIR"
FONT_FILE="$(akaos_find_dejavu_font)"
python3 "$ROOT_DIR/tools/generate_font.py" "$FONT_FILE" "$GENERATED_DIR/font_data.h"
python3 "$ROOT_DIR/tools/generate_vga_font.py" "$GENERATED_DIR/font8x16_data.h"

BUILD_TIMESTAMP="${BUILD_TIMESTAMP:-$(date +%m%d%Y%H%M%S)}"
export BUILD_TIMESTAMP

# 1. Build 32-bit userspace binaries and initramfs archive
bash "$ROOT_DIR/tools/build-user-32.sh"

echo "[BUILD] akaOS x86_32 XSCore Kernel"

CFLAGS=(
    --target=i686-unknown-none-elf
    -std=c17 -O2 -Wall -Wextra
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
    -m32 -march=i386 -mno-sse -mno-mmx -mno-80387
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\""
    -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/xscore/include" -I"$BUILD_DIR/generated"
)

# Compile architecture assembly and C sources
clang -target i386-unknown-none-elf \
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -m32 -march=i386 \
    -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/xscore/include" \
    -c "$ARCH_DIR/entry.S" -o "$BUILD_DIR/xscore32_entry.o"

clang "${CFLAGS[@]}" -c "$ARCH_DIR/tables.c" -o "$BUILD_DIR/xscore32_tables.o"
clang "${CFLAGS[@]}" -c "$ARCH_DIR/paging.c" -o "$BUILD_DIR/xscore32_paging.o"
clang "${CFLAGS[@]}" -c "$ARCH_DIR/timer.c" -o "$BUILD_DIR/xscore32_timer.o"
clang "${CFLAGS[@]}" -c "$ARCH_DIR/stack.c" -o "$BUILD_DIR/xscore32_stack.o"
clang "${CFLAGS[@]}" -c "$ROOT_DIR/xscore/kernel/main.c" -o "$BUILD_DIR/xscore32_main.o"
clang "${CFLAGS[@]}" -c "$ROOT_DIR/xscore/kernel/bios.c" -o "$BUILD_DIR/xscore32_bios.o"

# Compile core kernel modules for 32-bit
XSCORE_C_SOURCES=(
    "$ROOT_DIR/xscore/kernel/pci.c"
    "$ROOT_DIR/xscore/kernel/cpu.c"
    "$ROOT_DIR/xscore/kernel/gpu.c"
    "$ROOT_DIR/xscore/kernel/audio_drivers.c"
    "$ROOT_DIR/xscore/kernel/acpi.c"
    "$ROOT_DIR/xscore/kernel/virtio_blk.c"
    "$ROOT_DIR/xscore/kernel/audio.c"
    "$ROOT_DIR/xscore/kernel/net.c"
    "$ROOT_DIR/xscore/kernel/socket.c"
    "$ROOT_DIR/xscore/kernel/virtio_net.c"
    "$ROOT_DIR/xscore/kernel/vfs.c"
    "$ROOT_DIR/xscore/kernel/time.c"
    "$ROOT_DIR/xscore/kernel/file.c"
    "$ROOT_DIR/xscore/kernel/random.c"
    "$ROOT_DIR/xscore/kernel/tty.c"
    "$ROOT_DIR/xscore/kernel/input.c"
    "$ROOT_DIR/xscore/kernel/fb_console.c"
    "$ROOT_DIR/xscore/kernel/initramfs.c"
    "$ROOT_DIR/xscore/kernel/bufcache.c"
    "$ROOT_DIR/xscore/kernel/process.c"
    "$ROOT_DIR/xscore/kernel/page_cache.c"
    "$ROOT_DIR/xscore/kernel/user.c"
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
    obj="$BUILD_DIR/xscore32_$base.o"
    clang "${CFLAGS[@]}" -c "$src" -o "$obj"
    KERNEL_OBJECTS+=("$obj")
done

ld.lld -m elf_i386 \
    -T "$ARCH_DIR/linker.ld" \
    -o "$BUILD_DIR/XSCORE32.ELF" \
    "$BUILD_DIR/xscore32_entry.o" \
    "$BUILD_DIR/xscore32_tables.o" \
    "$BUILD_DIR/xscore32_paging.o" \
    "$BUILD_DIR/xscore32_timer.o" \
    "$BUILD_DIR/xscore32_stack.o" \
    "$BUILD_DIR/xscore32_main.o" \
    "$BUILD_DIR/xscore32_bios.o" \
    "${KERNEL_OBJECTS[@]}" \
    "$BUILD_DIR/initramfs32_archive.o"

echo "[DONE] Built $BUILD_DIR/XSCORE32.ELF"

kernel_args=""
if [[ "$test_mode" -eq 1 ]]; then kernel_args+=" AKAOS.TEST"; fi
if [[ "$verbose_mode" -eq 1 ]]; then kernel_args+=" VERBOSE"; fi

# Build standalone GRUB EFI binaries once. The same binaries are used by the
# FAT32 disk image and by the ISO's El Torito EFI partition.
build_uefi=0
if [[ "$build_fat32" -eq 1 || "$firmware" == "uefi" || "$firmware" == "both" ]]; then
    build_uefi=1
fi
if [[ "$build_uefi" -eq 1 ]]; then
    for tool in grub-mkstandalone mkfs.fat mmd mcopy truncate; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "Missing UEFI image dependency: $tool" >&2
            echo "On Ubuntu: sudo apt install grub-efi-amd64-bin grub-efi-ia32-bin mtools dosfstools" >&2
            exit 1
        fi
    done
    if ! GRUB_EFI64_DIR="$(akaos_find_grub_platform_dir \
        x86_64-efi "${AKAOS_GRUB_EFI64_DIR:-}")" ||
       ! GRUB_EFI32_DIR="$(akaos_find_grub_platform_dir \
        i386-efi "${AKAOS_GRUB_EFI32_DIR:-}")"; then
        echo "Missing GRUB EFI module directories." >&2
        echo "Run ./build.sh --check-deps with this target for package guidance." >&2
        exit 1
    fi
    if [[ ! -f "$GRUB_EFI64_DIR/modinfo.sh" || ! -f "$GRUB_EFI32_DIR/modinfo.sh" ]]; then
        echo "Missing GRUB EFI modules." >&2
        echo "On Ubuntu: sudo apt install grub-efi-amd64-bin grub-efi-ia32-bin" >&2
        exit 1
    fi
    EFI_MODULES="normal configfile search search_label search_fs_file fat iso9660 part_gpt part_msdos multiboot multiboot2 all_video gfxterm font"
    sed "s/@AKAOS_KERNEL_ARGS@/$kernel_args/" \
        "$ROOT_DIR/boot/grub/efi-bootstrap.cfg" > "$BUILD_DIR/grub32-uefi.cfg"
    grub-mkstandalone -d "$GRUB_EFI64_DIR" -O x86_64-efi \
        --install-modules="$EFI_MODULES" --modules="$EFI_MODULES" \
        -o "$BUILD_DIR/BOOTX64.EFI" \
        "boot/grub/grub.cfg=$BUILD_DIR/grub32-uefi.cfg"
    grub-mkstandalone -d "$GRUB_EFI32_DIR" -O i386-efi \
        --install-modules="$EFI_MODULES" --modules="$EFI_MODULES" \
        -o "$BUILD_DIR/BOOTIA32.EFI" \
        "boot/grub/grub.cfg=$BUILD_DIR/grub32-uefi.cfg"
fi

if [[ "$build_fat32" -eq 1 ]]; then
    echo "[BUILD] akaOS x86_32 FAT32 image"
    FAT_IMAGE="$BUILD_DIR/akaos32.img"
    truncate -s 64M "$FAT_IMAGE"
    mkfs.fat -F 32 -n AKAOS32 "$FAT_IMAGE" >/dev/null
    mmd -i "$FAT_IMAGE" ::/EFI ::/EFI/BOOT ::/boot ::/boot/grub
    if [[ "$build_uefi" -eq 1 ]]; then
        mcopy -o -i "$FAT_IMAGE" "$BUILD_DIR/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
        mcopy -o -i "$FAT_IMAGE" "$BUILD_DIR/BOOTIA32.EFI" ::/EFI/BOOT/BOOTIA32.EFI
    fi
    mcopy -o -i "$FAT_IMAGE" "$BUILD_DIR/XSCORE32.ELF" ::/boot/XSCORE32.ELF
    if [[ "$test_mode" -eq 1 ]]; then
        : > "$BUILD_DIR/AKAOS.TEST"
        mcopy -o -i "$FAT_IMAGE" "$BUILD_DIR/AKAOS.TEST" ::/AKAOS.TEST
    fi
    if [[ "$verbose_mode" -eq 1 ]]; then
        : > "$BUILD_DIR/VERBOSE"
        mcopy -o -i "$FAT_IMAGE" "$BUILD_DIR/VERBOSE" ::/VERBOSE
    fi
    echo "[DONE] Built $FAT_IMAGE"
fi

if [[ "$build_iso" -eq 1 ]]; then
    echo "[BUILD] akaOS x86_32 $firmware ISO image"
    ISO_ROOT="$BUILD_DIR/iso32-root"
    mkdir -p "$ISO_ROOT/boot/grub"
    cp "$BUILD_DIR/XSCORE32.ELF" "$ISO_ROOT/boot/XSCORE32.ELF"

    cat <<EOF > "$ISO_ROOT/boot/grub/grub.cfg"
set timeout=0
set default=0
set gfxmode=1024x768x32
set gfxpayload=keep
menuentry "akaOS x86_32 XSCore" {
    multiboot2 /boot/XSCORE32.ELF$kernel_args AKAOS.BIOS
    boot
}
menuentry "akaOS x86_32 XSCore (Multiboot 1)" {
    multiboot /boot/XSCORE32.ELF$kernel_args AKAOS.BIOS
    boot
}
EOF

    ISO_ARGS=(-as mkisofs -R -J -V akaOS32 -o "$BUILD_DIR/akaos32.iso")

    if [[ "$firmware" == "bios" || "$firmware" == "both" ]]; then
        for tool in grub-mkimage xorriso; do
            if ! command -v "$tool" >/dev/null 2>&1; then
                echo "Missing BIOS ISO dependency: $tool" >&2
                exit 1
            fi
        done
        if ! BIOS_GRUB_DIR="$(akaos_find_grub_platform_dir \
            i386-pc "${AKAOS_GRUB_BIOS_DIR:-}")"; then
            echo "Missing GRUB BIOS module directory." >&2
            echo "Run ./build.sh --check-deps with this target for package guidance." >&2
            exit 1
        fi
        AKAOS_GRUB_BIOS_DIR="$BIOS_GRUB_DIR"
        if [[ ! -f "$BIOS_GRUB_DIR/cdboot.img" || ! -f "$BIOS_GRUB_DIR/boot_hybrid.img" ]]; then
            echo "Missing GRUB BIOS modules; on Ubuntu: sudo apt install grub-pc-bin" >&2
            exit 1
        fi
        mkdir -p "$ISO_ROOT/boot/grub/i386-pc"
        grub-mkimage -d "$BIOS_GRUB_DIR" -O i386-pc \
            -o "$BUILD_DIR/grub32-bios-core.img" -p /boot/grub \
            biosdisk iso9660 normal configfile multiboot multiboot2 all_video gfxterm
        cp "$BIOS_GRUB_DIR/cdboot.img" "$ISO_ROOT/boot/grub/i386-pc/eltorito.img"
        dd if="$BUILD_DIR/grub32-bios-core.img" \
            of="$ISO_ROOT/boot/grub/i386-pc/eltorito.img" \
            oflag=append conv=notrunc status=none
        ISO_ARGS+=(
            -b boot/grub/i386-pc/eltorito.img
            -no-emul-boot -boot-load-size 4 -boot-info-table
        )
    fi

    if [[ "$firmware" == "uefi" || "$firmware" == "both" ]]; then
        for tool in mkfs.fat mmd mcopy truncate xorriso; do
            if ! command -v "$tool" >/dev/null 2>&1; then
                echo "Missing UEFI ISO dependency: $tool" >&2
                exit 1
            fi
        done
        mkdir -p "$ISO_ROOT/EFI/BOOT"
        EFI_IMAGE="$ISO_ROOT/EFI/BOOT/efiboot.img"
        # The dual-firmware image carries both the x86_64 and IA32 loaders.
        # They are intentionally self-contained and can each be >10 MiB, so
        # a 16 MiB FAT image can overflow when both files are copied.  Keep
        # enough headroom for either loader plus FAT metadata.
        truncate -s 64M "$EFI_IMAGE"
        mkfs.fat -F 16 -n AKAOS32EFI "$EFI_IMAGE" >/dev/null
        mmd -i "$EFI_IMAGE" ::/EFI ::/EFI/BOOT
        mcopy -o -i "$EFI_IMAGE" "$BUILD_DIR/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
        mcopy -o -i "$EFI_IMAGE" "$BUILD_DIR/BOOTIA32.EFI" ::/EFI/BOOT/BOOTIA32.EFI
        if [[ "$firmware" == "both" ]]; then
            ISO_ARGS+=(-eltorito-alt-boot)
        fi
        ISO_ARGS+=(-e EFI/BOOT/efiboot.img -no-emul-boot)
    fi

    if [[ "$firmware" == "both" ]]; then
        ISO_ARGS+=(
            -isohybrid-mbr "${AKAOS_GRUB_BIOS_DIR:-/usr/lib/grub/i386-pc}/boot_hybrid.img"
            -isohybrid-gpt-basdat
        )
    fi
    ISO_ARGS+=("$ISO_ROOT")
    xorriso "${ISO_ARGS[@]}" >/dev/null 2>&1
    echo "[DONE] Built $BUILD_DIR/akaos32.iso"
fi
