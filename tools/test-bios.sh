#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_LOG="$ROOT_DIR/build/bios-boot-test.log"
STORAGE_IMAGE="$ROOT_DIR/build/akaos-bios-virtio-test.img"

"$ROOT_DIR/build.sh" --arch x86_64 --firmware bios --disk-type iso --test

python3 - "$STORAGE_IMAGE" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
with path.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")
PY

set +e
timeout 30s qemu-system-x86_64 \
    -machine q35 \
    -m 256M \
    -cdrom "$ROOT_DIR/build/akaos.iso" \
    -boot d \
    -drive if=none,format=raw,file="$STORAGE_IMAGE",id=akaos-storage \
    -device virtio-blk-pci,drive=akaos-storage \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -display none \
    -serial file:"$TEST_LOG" \
    -monitor none \
    -no-reboot
QEMU_STATUS=$?
set -e

cat "$TEST_LOG"
if [[ "$QEMU_STATUS" -ne 33 ]]; then
    echo "[FAIL] BIOS QEMU exited with status $QEMU_STATUS (expected 33)." >&2
    exit 1
fi
grep -Fq "[INFO] BIOS Multiboot2 loader handoff received" "$TEST_LOG"
grep -Fq "[INFO] BIOS framebuffer tag accepted" "$TEST_LOG"
grep -Fq "[INFO] GDT initialized" "$TEST_LOG"
grep -Fq "[INFO] XSCore virtual memory active" "$TEST_LOG"
grep -Fq "[INFO] virtio-blk sector read verified" "$TEST_LOG"
grep -Fq "[INFO] Created isolated user process 1" "$TEST_LOG"
grep -Fq "[PASS] akaOS user-mode milestone complete" "$TEST_LOG"
echo "[PASS] Automated BIOS boot test completed."
