#!/usr/bin/env bash
# Verify that the interactive QEMU session remains open across `reboot`.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
RUN_LOG="$BUILD_DIR/reboot-test-run.log"
STORAGE_IMAGE="$BUILD_DIR/reboot-test-data.img"

"$ROOT_DIR/build.sh" --arch x86_64 --firmware uefi --disk-type fat32 >/dev/null

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.reboot-test.fd"
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"
truncate -s 4M "$STORAGE_IMAGE"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$STORAGE_IMAGE" "$RUN_LOG" <<'PY'
import subprocess
import sys
import time
from pathlib import Path

root, ovmf_code, ovmf_vars, storage_image, log_name = sys.argv[1:]
log_path = Path(log_name)

qemu = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "q35", "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file={root}/build/akaos.img",
    "-drive", f"if=none,format=raw,file={storage_image},id=akaos-storage",
    "-device", "virtio-blk-pci,drive=akaos-storage",
    "-serial", f"file:{log_path}", "-monitor", "stdio", "-display", "none",
], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
   stderr=subprocess.DEVNULL, text=True)

def log_text():
    return log_path.read_text(errors="ignore")

def send_line(line):
    for character in line:
        qemu.stdin.write(f"sendkey {character}\n")
    qemu.stdin.write("sendkey ret\n")
    qemu.stdin.flush()
    time.sleep(.6)

try:
    for _ in range(800):
        if log_text().count("[INFO] akaOS shell active") >= 1:
            break
        if qemu.poll() is not None:
            raise RuntimeError("QEMU closed before the first shell started")
        time.sleep(0.05)
    else:
        raise RuntimeError("first akaOS shell did not become active")

    send_line("root")
    send_line("root")
    send_line("reboot")

    for _ in range(1200):
        content = log_text()
        if (content.count("[INFO] System reboot requested") >= 1 and
                content.count("[INFO] XSCore entry reached") >= 2 and
                content.count("[INFO] akaOS shell active") >= 2):
            break
        if qemu.poll() is not None:
            raise RuntimeError("QEMU closed instead of resetting")
        time.sleep(0.05)
    else:
        raise RuntimeError("akaOS did not return to the shell after reboot")
finally:
    if qemu.poll() is None:
        qemu.stdin.write("quit\n")
        qemu.stdin.flush()
    qemu.wait(timeout=10)

print("[PASS] reboot reset akaOS and kept the QEMU session open")
PY
