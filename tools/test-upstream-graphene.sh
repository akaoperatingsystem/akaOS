#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="$ROOT_DIR/build/akaos.iso"
INITRAMFS="$ROOT_DIR/build/initramfs.img"

if [[ ! -f "$IMAGE" || ! -f "$INITRAMFS" ||
      ! -f "$ROOT_DIR/build/GRAPHENE_UPSTREAM_TEST.ELF" ]] ||
   ! grep -a -q '/lib/libgraphene-1.0.so.0' "$INITRAMFS" ||
   ! grep -a -q '/bin/graphene-upstream-test' "$INITRAMFS"; then
    echo "Build the x86_64 ISO with upstream Graphene artifacts first." >&2
    exit 2
fi

python3 - "$ROOT_DIR" <<'PY'
import subprocess
import os
import signal
import sys
import time
from pathlib import Path

root = Path(sys.argv[1])
log = root / "build" / "test-upstream-graphene.log"
log.unlink(missing_ok=True)
storage = root / "build" / "upstream-graphene-test-storage.img"
with storage.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")
proc = subprocess.Popen(
    ["qemu-system-x86_64", "-machine", "q35", "-cpu", "max", "-m", "256M",
     "-cdrom", str(root / "build/akaos.iso"), "-boot", "d", "-vga", "std",
     "-drive", f"if=none,format=raw,file={storage},id=akaos-storage",
     "-device", "virtio-blk-pci,drive=akaos-storage",
     "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
     "-no-reboot"],
    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    text=True, start_new_session=True,
)

def content():
    return log.read_text(errors="ignore") if log.exists() else ""

def wait_for(marker, timeout=300):
    deadline = time.time() + timeout
    while time.time() < deadline:
        output = content()
        if marker in output:
            return output
        if "[FATAL]" in output:
            raise RuntimeError(f"guest faulted while waiting for {marker!r}:\n{output[-5000:]}")
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for {marker!r}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {marker!r}:\n{content()[-5000:]}")

def key_for(ch):
    if "A" <= ch <= "Z":
        return "shift-" + ch.lower()
    return {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
            "_": "shift-minus"}.get(ch, ch)

def send_line(text):
    for ch in text:
        proc.stdin.write(f"sendkey {key_for(ch)} 5\n")
        proc.stdin.flush()
        time.sleep(0.08)
    proc.stdin.write("sendkey ret 5\n")
    proc.stdin.flush()

try:
    wait_for("login:")
    send_line("root")
    wait_for("Password:")
    send_line("root")
    wait_for("root@akaOS:/root#")
    send_line("graphene-upstream-test")
    wait_for("Upstream Graphene 1.10.8 vector, matrix, geometry, and GObject APIs loaded")
    wait_for("upstream-graphene-smoke-ok")
    print("[PASS] Upstream Graphene vector, matrix, rectangle, and GObject APIs ran in x86_64 akaOS.")
finally:
    if proc.stdin:
        proc.stdin.close()
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait(timeout=10)
        except ProcessLookupError:
            proc.wait(timeout=10)
PY
