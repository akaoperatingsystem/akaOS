#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

TEST_IMAGE="$ROOT_DIR/build/akaos-test.img"
STORAGE_IMAGE="$ROOT_DIR/build/akaos-virtio-test.img"
TEST_MARKER="$ROOT_DIR/build/AKAOS.TEST"
TEST_LOG="$ROOT_DIR/build/boot-test.log"
cp "$ROOT_DIR/build/akaos.img" "$TEST_IMAGE"
: > "$TEST_MARKER"
mcopy -o -i "$TEST_IMAGE@@1M" "$TEST_MARKER" ::/AKAOS.TEST

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$ROOT_DIR/build/OVMF_VARS.test.fd"
if [[ -d "$OVMF_VARS_SOURCE" ]]; then
    OVMF_VARS_SOURCE="$OVMF_VARS_SOURCE/OVMF_VARS_4M.fd"
fi
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"

set +e
timeout 240s qemu-system-x86_64 \
    -machine q35 \
    -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$OVMF_VARS" \
    -drive format=raw,file.locking=off,file="$TEST_IMAGE" \
    -drive if=none,format=raw,file="$STORAGE_IMAGE",id=akaos-storage \
    -device virtio-blk-pci,drive=akaos-storage \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -display none \
    -serial file:"$TEST_LOG" \
    -monitor none \
    -no-reboot
QEMU_STATUS=$?
set -e

echo "QEMU status: $QEMU_STATUS"
tail -n 40 "$TEST_LOG"
