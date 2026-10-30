#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${AKAOS_TEST_IMAGE:-$ROOT_DIR/build/akaos.iso}"
INITRAMFS="$ROOT_DIR/build/initramfs.img"

if [[ ! -f "$IMAGE" || ! -f "$INITRAMFS" ||
      ! -f "$ROOT_DIR/build/FONTCONFIG_UPSTREAM_TEST.ELF" ]] ||
   ! grep -a -q '/lib/libfontconfig.so.1' "$INITRAMFS" ||
   ! grep -a -q '/lib/libexpat.so.1' "$INITRAMFS" ||
   ! grep -a -q '/etc/fonts/fonts.conf' "$INITRAMFS" ||
   ! grep -a -q '/etc/fonts/conf.d/49-sansserif.conf' "$INITRAMFS" ||
   ! grep -a -q '/usr/share/fonts/truetype/akaos/default.ttf' "$INITRAMFS"; then
    echo "Build the x86_64 ISO with upstream Fontconfig/Expat artifacts first." >&2
    exit 2
fi

python3 - "$ROOT_DIR" <<'PY'
import os
import subprocess
import sys
import time
from pathlib import Path

root = Path(sys.argv[1])
image = Path(os.environ.get("AKAOS_TEST_IMAGE", root / "build/akaos.iso"))
log = root / "build" / "test-upstream-fontconfig.log"
log.unlink(missing_ok=True)
storage = root / "build" / "upstream-fontconfig-test-storage.img"
with storage.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")
qemu_memory = os.environ.get("AKAOS_TEST_MEMORY", "256M")
proc = subprocess.Popen(
    ["qemu-system-x86_64", "-machine", "q35", "-cpu", "max", "-m", qemu_memory,
     "-cdrom", str(image), "-boot", "d", "-vga", "std",
     "-drive", f"if=none,format=raw,file={storage},id=akaos-storage",
     "-device", "virtio-blk-pci,drive=akaos-storage",
     "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
     "-no-reboot"],
    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    text=True,
)

def content():
    return log.read_text(errors="ignore") if log.exists() else ""

def wait_for(marker, timeout=90):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if marker in content():
            return
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for {marker!r}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {marker!r}; guest output:\n{content()[-5000:]}")

def key_for(ch):
    if "A" <= ch <= "Z":
        return "shift-" + ch.lower()
    return {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
            "_": "shift-minus", "&": "shift-7", "=": "equal",
            ":": "shift-semicolon"}.get(ch, ch)

def send_line(text):
    for ch in text:
        proc.stdin.write(f"sendkey {key_for(ch)} 5\n")
        proc.stdin.flush()
        time.sleep(0.08)
    proc.stdin.write("sendkey ret 5\n")
    proc.stdin.flush()
    time.sleep(0.3)

def wait_for_prompt_count(count, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if content().count("root@akaOS:/root#") >= count:
            return
        if proc.poll() is not None:
            raise RuntimeError("QEMU exited while waiting for the root prompt")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for root prompt; guest output:\n{content()[-5000:]}")

def wait_for_command_marker(marker, expected_prompt_count, timeout=90):
    deadline = time.time() + timeout
    while time.time() < deadline:
        output = content()
        if marker in output:
            return
        if output.count("root@akaOS:/root#") >= expected_prompt_count:
            raise RuntimeError(f"command returned without {marker!r}; guest output:\n{output[-5000:]}")
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for {marker!r}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {marker!r}; guest output:\n{content()[-5000:]}")

try:
    wait_for("login:", 300)
    send_line("root")
    wait_for("Password:", 90)
    send_line("root")
    wait_for_prompt_count(1, 90)
    expected_prompt_count = content().count("root@akaOS:/root#") + 1
    send_line("fontconfig-upstream-test")
    wait_for_command_marker("Upstream Fontconfig 21803 matched", expected_prompt_count)
    wait_for_command_marker("Fontconfig configuration, Expat XML parsing, and font discovery verified",
                            expected_prompt_count)
    wait_for_command_marker("upstream-fontconfig-smoke-ok", expected_prompt_count)
    wait_for_prompt_count(expected_prompt_count)
    print("[PASS] Upstream Fontconfig/Expat found packaged DejaVu Sans in x86_64 akaOS.")
finally:
    if proc.poll() is None:
        proc.kill()
        proc.wait(timeout=10)
PY
