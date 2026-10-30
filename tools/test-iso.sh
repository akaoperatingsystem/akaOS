#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

"$ROOT_DIR/build.sh" --arch x86_64 --firmware both --disk-type iso --test

ISO_IMAGE="$ROOT_DIR/build/akaos.iso"
STORAGE_IMAGE="$ROOT_DIR/build/akaos-iso-virtio-test.img"
UEFI_LOG="$ROOT_DIR/build/iso-uefi-test.log"
BIOS_LOG="$ROOT_DIR/build/iso-bios-test.log"
OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$ROOT_DIR/build/OVMF_VARS.iso-regression.fd"

python3 - "$STORAGE_IMAGE" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
with path.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")
PY
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"

set +e
# Full userspace/process verification can exceed 30 seconds on WSL/DrvFS even
# when both firmware paths are healthy. Keep a bounded but realistic timeout.
timeout 90s qemu-system-x86_64 \
    -machine q35 -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$OVMF_VARS" \
    -cdrom "$ISO_IMAGE" -boot d \
    -drive if=none,format=raw,file="$STORAGE_IMAGE",id=akaos-storage \
    -device virtio-blk-pci,drive=akaos-storage \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -display none -serial file:"$UEFI_LOG" -monitor none -no-reboot
uefi_status=$?

timeout 90s qemu-system-x86_64 \
    -machine q35 -m 256M \
    -cdrom "$ISO_IMAGE" -boot d \
    -drive if=none,format=raw,file="$STORAGE_IMAGE",id=akaos-storage \
    -device virtio-blk-pci,drive=akaos-storage \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -display none -serial file:"$BIOS_LOG" -monitor none -no-reboot
bios_status=$?
set -e

if [[ "$uefi_status" -ne 33 || "$bios_status" -ne 33 ]]; then
    echo "[FAIL] ISO firmware exits: UEFI=$uefi_status BIOS=$bios_status (expected 33/33)" >&2
    exit 1
fi
grep -Fq "[INFO] akaOS UEFI loader starting" "$UEFI_LOG"
grep -Fq "[INFO] UEFI framebuffer initialized" "$UEFI_LOG"
grep -Fq "[PASS] akaOS user-mode milestone complete" "$UEFI_LOG"
grep -Fq "[INFO] BIOS Multiboot2 loader handoff received" "$BIOS_LOG"
grep -Fq "[INFO] BIOS framebuffer tag accepted" "$BIOS_LOG"
grep -Fq "[PASS] akaOS user-mode milestone complete" "$BIOS_LOG"

echo "[PASS] One akaos.iso booted successfully through UEFI and legacy BIOS."
