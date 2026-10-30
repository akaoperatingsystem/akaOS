#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

# QEMU boot window. Slower hosts (WSL on DrvFS, -d int debug logging) may
# need more than the default; override with SMP_QEMU_TIMEOUT.
SMP_QEMU_TIMEOUT="${SMP_QEMU_TIMEOUT:-90s}"

echo "[TEST] Building akaOS for SMP verification..."
"$ROOT_DIR/build.sh" --arch x86_64 --firmware uefi --disk-type fat32 --test

STORAGE_IMAGE="$ROOT_DIR/build/akaos-virtio-test.img"
TEST_MARKER="$ROOT_DIR/build/AKAOS.TEST"
: > "$TEST_MARKER"

python3 - "$STORAGE_IMAGE" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
with path.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")
PY

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"

# -------------------------------------------------------------
# Test 1: 2 Cores (-smp 2)
# -------------------------------------------------------------
echo "[TEST] Testing SMP with 2 Cores (-smp 2)..."
SMP2_IMAGE="$ROOT_DIR/build/akaos-smp2.img"
SMP2_LOG="$ROOT_DIR/build/boot-smp2.log"
SMP2_VARS="$ROOT_DIR/build/OVMF_VARS.smp2.fd"

cp "$ROOT_DIR/build/akaos.img" "$SMP2_IMAGE"
mcopy -o -i "$SMP2_IMAGE" "$TEST_MARKER" ::/AKAOS.TEST
cp "$OVMF_VARS_SOURCE" "$SMP2_VARS"

set +e
timeout "$SMP_QEMU_TIMEOUT" qemu-system-x86_64 \
    -machine q35 \
    -smp 2 \
    -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$SMP2_VARS" \
    -drive format=raw,file.locking=off,file="$SMP2_IMAGE" \
    -drive if=none,format=raw,file="$STORAGE_IMAGE",id=akaos-storage \
    -device virtio-blk-pci,drive=akaos-storage \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0,disable-legacy=off \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -display none \
    -serial file:"$SMP2_LOG" \
    -monitor none \
    -d int,cpu_reset,guest_errors -D "$ROOT_DIR/build/qemu_debug.log" \
    -no-reboot
QEMU_STATUS_2=$?
set -e

if [[ "$QEMU_STATUS_2" -ne 33 ]]; then
    echo "[FAIL] QEMU -smp 2 exited with status $QEMU_STATUS_2 (expected 33)." >&2
    tail -n 50 "$SMP2_LOG"
    echo "=== QEMU DEBUG LOG ==="
    tail -n 50 "$ROOT_DIR/build/qemu_debug.log" || true
    exit 1
fi

grep -Fq "[INFO] SMP: 2 cores detected in ACPI MADT" "$SMP2_LOG"
grep -Fq "[INFO] SMP: Core 1 (APIC ID 1) online" "$SMP2_LOG"
grep -Fq "[INFO] SMP: All 2 cores active" "$SMP2_LOG"
grep -Fq "[PASS] SMP concurrent work and shared-counter stress verified" "$SMP2_LOG"
grep -Fq "[PASS] SMP per-CPU scheduler handoff verified" "$SMP2_LOG"
grep -Fq "[PASS] SMP shared PMM and heap allocator stress verified" "$SMP2_LOG"
grep -Fq "[PASS] SMP concurrent VFS/device/socket stress verified" "$SMP2_LOG"
grep -Fq "[PASS] SMP persistent VFS transactions verified" "$SMP2_LOG"
grep -Fq "[PASS] SMP external network transmit stress verified" "$SMP2_LOG"
grep -Fq "[PASS] SMP AP userspace scheduling stress verified" "$SMP2_LOG"
grep -Fq "[PASS] SMP AP userspace process reclamation verified" "$SMP2_LOG"
grep -Fq "CPU(s):                          2" "$SMP2_LOG"
grep -Fq "On-line CPU(s) list:             0-1" "$SMP2_LOG"
grep -Fq "Core(s) per socket:              2" "$SMP2_LOG"
echo "[PASS] 2-core SMP verification successful!"

# -------------------------------------------------------------
# Test 2: 4 Cores (-smp 4)
# -------------------------------------------------------------
echo "[TEST] Testing SMP with 4 Cores (-smp 4)..."
SMP4_IMAGE="$ROOT_DIR/build/akaos-smp4.img"
SMP4_LOG="$ROOT_DIR/build/boot-smp4.log"
SMP4_VARS="$ROOT_DIR/build/OVMF_VARS.smp4.fd"

cp "$ROOT_DIR/build/akaos.img" "$SMP4_IMAGE"
mcopy -o -i "$SMP4_IMAGE" "$TEST_MARKER" ::/AKAOS.TEST
cp "$OVMF_VARS_SOURCE" "$SMP4_VARS"

set +e
timeout "$SMP_QEMU_TIMEOUT" qemu-system-x86_64 \
    -machine q35 \
    -smp 4 \
    -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$SMP4_VARS" \
    -drive format=raw,file.locking=off,file="$SMP4_IMAGE" \
    -drive if=none,format=raw,file="$STORAGE_IMAGE",id=akaos-storage \
    -device virtio-blk-pci,drive=akaos-storage \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0,disable-legacy=off \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -display none \
    -serial file:"$SMP4_LOG" \
    -monitor none \
    -no-reboot
QEMU_STATUS_4=$?
set -e

if [[ "$QEMU_STATUS_4" -ne 33 ]]; then
    echo "[FAIL] QEMU -smp 4 exited with status $QEMU_STATUS_4 (expected 33)." >&2
    tail -n 50 "$SMP4_LOG"
    exit 1
fi

grep -Fq "[INFO] SMP: 4 cores detected in ACPI MADT" "$SMP4_LOG"
grep -Fq "[INFO] SMP: Core 1 (APIC ID 1) online" "$SMP4_LOG"
grep -Fq "[INFO] SMP: Core 2 (APIC ID 2) online" "$SMP4_LOG"
grep -Fq "[INFO] SMP: Core 3 (APIC ID 3) online" "$SMP4_LOG"
grep -Fq "[INFO] SMP: All 4 cores active" "$SMP4_LOG"
grep -Fq "[PASS] SMP concurrent work and shared-counter stress verified" "$SMP4_LOG"
grep -Fq "[PASS] SMP per-CPU scheduler handoff verified" "$SMP4_LOG"
grep -Fq "[PASS] SMP shared PMM and heap allocator stress verified" "$SMP4_LOG"
grep -Fq "[PASS] SMP concurrent VFS/device/socket stress verified" "$SMP4_LOG"
grep -Fq "[PASS] SMP persistent VFS transactions verified" "$SMP4_LOG"
grep -Fq "[PASS] SMP external network transmit stress verified" "$SMP4_LOG"
grep -Fq "[PASS] SMP AP userspace scheduling stress verified" "$SMP4_LOG"
grep -Fq "[PASS] SMP AP userspace process reclamation verified" "$SMP4_LOG"
grep -Fq "CPU(s):                          4" "$SMP4_LOG"
grep -Fq "On-line CPU(s) list:             0-3" "$SMP4_LOG"
grep -Fq "Core(s) per socket:              4" "$SMP4_LOG"
echo "[PASS] 4-core SMP verification successful!"

python3 - "$STORAGE_IMAGE" <<'PY'
import sys
from pathlib import Path

image = Path(sys.argv[1]).read_bytes()
required = [b"/root/xsmp1/persist", b"/root/xsmp2/persist",
            b"/root/xsmp3/persist"]
missing = [item.decode() for item in required if item not in image]
if missing:
    raise SystemExit("[FAIL] persisted SMP records missing: " + ", ".join(missing))
print("[PASS] Persistent SMP records found in the writable akaFS image")
PY

echo "[PASS] All SMP multi-core tests passed!"
