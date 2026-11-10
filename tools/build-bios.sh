#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BIOS_ROOT="$BUILD_DIR/bios-root"
KERNEL="${AKAOS_BIOS_KERNEL:-$BUILD_DIR/XSCORE-BIOS.ELF}"
UEFI_LOADER="$BUILD_DIR/esp/EFI/BOOT/BOOTX64.EFI"
UEFI_KERNEL="${AKAOS_UEFI_KERNEL:-$BUILD_DIR/XSCORE.ELF}"
EFI_IMAGE="$BUILD_DIR/akaos-efi-boot.img"
OUTPUT="${AKAOS_ISO_OUTPUT:-$BUILD_DIR/akaos.iso}"

usage() {
    cat <<'EOF'
Usage: ./tools/build-bios.sh [--test] [--verbose] [--no-build]

Creates one dual-firmware ISO: GRUB Multiboot2 for legacy BIOS plus an El
Torito FAT image containing the native akaOS UEFI loader. The normal output
boots the interactive shell; --test and --verbose apply to both boot paths.
The internal --no-build option is used by build.sh after the common build.

Optional environment overrides: AKAOS_BIOS_KERNEL, AKAOS_UEFI_KERNEL, and
AKAOS_ISO_OUTPUT select alternate kernel and output paths.
EOF
}

for tool in grub-mkrescue grub-file xorriso; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing BIOS image dependency: $tool" >&2
        echo "On Ubuntu: sudo apt install grub-pc-bin grub-common grub2-common xorriso" >&2
        exit 1
    fi
done

test_mode=0
verbose_mode=0
build_kernel=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)
            test_mode=1
            shift
            ;;
        --verbose)
            verbose_mode=1
            shift
            ;;
        --no-build)
            build_kernel=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option '$1'" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ! -f "$KERNEL" || ! -f "$UEFI_LOADER" || ! -f "$UEFI_KERNEL" ]]; then
    if [[ "$build_kernel" -eq 0 ]]; then
        echo "Missing $KERNEL; run ./build.sh first" >&2
        exit 1
    fi
    "$ROOT_DIR/build.sh" --arch x86_64 --firmware both --disk-type iso
fi

ISO_STAGING_DIR="$(mktemp -d /tmp/akaos-iso.XXXXXX)"
STAGING_OUTPUT="$ISO_STAGING_DIR/akaos.iso"
STAGING_BIOS_ROOT="$ISO_STAGING_DIR/bios-root"
cleanup_staged_iso() {
    if [[ "$(realpath -- "$ISO_STAGING_DIR")" == /tmp/akaos-iso.* ]]; then
        rm -rf -- "$ISO_STAGING_DIR"
    fi
}
trap cleanup_staged_iso EXIT

grub-file --is-x86-multiboot2 "$KERNEL"
mkdir -p "$BIOS_ROOT/boot/grub"
cp "$KERNEL" "$BIOS_ROOT/boot/XSCORE.ELF"

# UEFI optical boot uses an El Torito no-emulation FAT image. The kernel
# embeds the initramfs and can grow well past the old fixed 32 MiB image size.
# Leave 8 MiB for FAT metadata and future growth, round up to a whole MiB, and
# select FAT32 once the image no longer fits a conventional FAT16 ESP. UEFI
# treats an El Torito sector count of 0/1 as extending to the end of the CD,
# which is how xorriso represents an EFI image larger than 65535 512-byte
# sectors.
kernel_bytes=$(stat -c '%s' "$UEFI_KERNEL")
efi_image_bytes=$(((kernel_bytes + 8 * 1024 * 1024 + 1024 * 1024 - 1) /
    (1024 * 1024) * 1024 * 1024))
if (( efi_image_bytes <= 32 * 1024 * 1024 )); then
    efi_fat_type=16
else
    efi_fat_type=32
fi
truncate -s "$efi_image_bytes" "$EFI_IMAGE"
mkfs.fat -F "$efi_fat_type" -n AKAOS_EFI "$EFI_IMAGE" >/dev/null
mmd -i "$EFI_IMAGE" ::/EFI ::/EFI/BOOT
mcopy -o -i "$EFI_IMAGE" "$UEFI_LOADER" ::/EFI/BOOT/BOOTX64.EFI
mcopy -o -i "$EFI_IMAGE" "$UEFI_KERNEL" ::/XSCORE.ELF
if [[ "$test_mode" -eq 1 ]]; then
    : > "$BUILD_DIR/AKAOS.TEST"
    mcopy -o -i "$EFI_IMAGE" "$BUILD_DIR/AKAOS.TEST" ::/AKAOS.TEST
fi
if [[ "$verbose_mode" -eq 1 ]]; then
    : > "$BUILD_DIR/VERBOSE"
    mcopy -o -i "$EFI_IMAGE" "$BUILD_DIR/VERBOSE" ::/VERBOSE
fi
cp "$EFI_IMAGE" "$BIOS_ROOT/boot/akaos-efi.img"
mkdir -p "$BIOS_ROOT/EFI/BOOT"
cp "$UEFI_LOADER" "$BIOS_ROOT/EFI/BOOT/BOOTX64.EFI"

kernel_args=""
if [[ "$test_mode" -eq 1 ]]; then kernel_args+=" AKAOS.TEST"; fi
if [[ "$verbose_mode" -eq 1 ]]; then kernel_args+=" VERBOSE"; fi

local_default_entry=0
if [[ "$verbose_mode" -eq 1 ]]; then
    local_default_entry=1
fi

{
    printf 'set timeout=3\n'
    printf 'set default=%s\n' "$local_default_entry"
    printf 'set gfxmode=1024x768x32\n'
    printf 'set gfxpayload=keep\n'
    # `vbe.mod` exists only in the i386-pc GRUB module set.  The same config
    # is read by the x86_64-efi El Torito path, so use the portable selector.
    printf 'insmod all_video\n'
    printf 'insmod gfxterm\n'
    printf 'terminal_output gfxterm\n'
    printf "menuentry 'akaOS (Graphical Splash)' {\n"
    printf '    multiboot2 /boot/XSCORE.ELF splash%s\n' "$kernel_args"
    printf '    boot\n'
    printf '}\n'
    printf "menuentry 'akaOS (Verbose Console / Text-Only)' {\n"
    printf '    multiboot2 /boot/XSCORE.ELF verbose%s\n' "$kernel_args"
    printf '    boot\n'
    printf '}\n'
} > "$BIOS_ROOT/boot/grub/grub.cfg"

mkdir -p "$STAGING_BIOS_ROOT"
cp -a "$BIOS_ROOT/." "$STAGING_BIOS_ROOT/"

grub_directory_args=()
if [[ -n "${AKAOS_GRUB_DIRECTORY:-}" ]]; then
    grub_directory_args=(-d "$AKAOS_GRUB_DIRECTORY")
elif [[ -d "${AKAOS_GRUB_BIOS_DIRECTORY:-/usr/lib/grub/i386-pc}" ]]; then
    # Build only the BIOS GRUB platform here.  grub-mkrescue otherwise adds
    # its own x86_64-efi El Torito image before akaOS's native UEFI image,
    # causing OVMF to enter GRUB instead of the XSCore UEFI loader.
    grub_directory_args=(-d "${AKAOS_GRUB_BIOS_DIRECTORY:-/usr/lib/grub/i386-pc}")
fi
grub-mkrescue "${grub_directory_args[@]}" -o "$STAGING_OUTPUT" "$STAGING_BIOS_ROOT" \
    -eltorito-alt-boot -eltorito-platform efi -e boot/akaos-efi.img \
    -no-emul-boot -efi-boot-part --efi-boot-image >/dev/null
cp -- "$STAGING_OUTPUT" "$OUTPUT"
echo "[DONE] $OUTPUT"
