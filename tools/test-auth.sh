#!/usr/bin/env bash
# Verify login, process credentials, filesystem permissions, and root-only reboot.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"
BUILD_DIR="$ROOT_DIR/build"
RUN_LOG="$BUILD_DIR/auth-test-run.log"
STORAGE_IMAGE="$BUILD_DIR/auth-test-data.img"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/build.sh" --arch x86_64 --firmware uefi --disk-type fat32 >/dev/null
fi
OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.auth-test.fd"
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"
: > "$STORAGE_IMAGE"
truncate -s 4M "$STORAGE_IMAGE"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$STORAGE_IMAGE" "$RUN_LOG" <<'PY'
import subprocess, sys, time
from pathlib import Path

root, code, variables, storage, log_name = sys.argv[1:]
log = Path(log_name)
qemu = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "q35", "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
    "-drive", f"if=pflash,format=raw,file={variables}",
    "-drive", f"format=raw,file={root}/build/akaos.img",
    "-drive", f"if=none,format=raw,file={storage},id=akaos-storage",
    "-device", "virtio-blk-pci,drive=akaos-storage",
    "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
   stderr=subprocess.DEVNULL, text=True)

def content():
    return log.read_text(errors="ignore")

def wait_for(text, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if text in content(): return
        if qemu.poll() is not None: raise RuntimeError("QEMU exited early")
        time.sleep(.05)
    raise RuntimeError(f"timed out waiting for {text!r}")

def wait_count(text, count, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if content().count(text) >= count: return
        if qemu.poll() is not None: raise RuntimeError("QEMU exited early")
        time.sleep(.05)
    raise RuntimeError(f"timed out waiting for {count} occurrences of {text!r}")

def send_line(line):
    keys = {" ": "spc", "/": "slash", "-": "minus", ".": "dot"}
    for character in line:
        qemu.stdin.write(f"sendkey {keys.get(character, character)}\n")
    qemu.stdin.write("sendkey ret\n")
    qemu.stdin.flush()
    time.sleep(.7)

try:
    wait_for("akaOS tty1", 180)
    send_line("root")
    send_line("root")
    send_line("id")
    wait_for("uid=0(root) gid=0 euid=0")
    send_line("cat /etc/shadow")
    wait_for("483862a3791663d0")
    send_line("exit")
    wait_for("akaOS tty1\nlogin: ")
    send_line("user")
    send_line("akaos")
    send_line("id")
    wait_for("uid=1000(user) gid=1000 euid=1000")
    send_line("cat /etc/shadow")
    send_line("touch /root/denied")
    send_line("reboot")
    wait_for("Permission denied")
    if "System reboot requested" in content():
        raise RuntimeError("unprivileged reboot was accepted")
    send_line("sudo -h")
    wait_for("Usage: sudo")
    send_line("sudo whoami")
    wait_for("[sudo] password for user: ")
    send_line("akaos")
    wait_for("root\n")
    send_line("sudo cat /etc/shadow")
    wait_for("[sudo] password for user: ")
    send_line("akaos")
    wait_for("483862a3791663d0")
    send_line("su")
    send_line("root")
    send_line("id")
    wait_count("uid=0(root) gid=0 euid=0", 2)
    send_line("exit")
    send_line("passwd")
    send_line("akaos")
    send_line("akaos2")
    send_line("akaos2")
    wait_for("passwd: password updated successfully")
    send_line("exit")
    wait_count("akaOS tty1", 3)
    send_line("user")
    send_line("akaos2")
    send_line("id")
    wait_count("uid=1000(user) gid=1000 euid=1000", 2)
finally:
    if qemu.poll() is None:
        qemu.stdin.write("quit\n"); qemu.stdin.flush()
    qemu.wait(timeout=10)

print("[PASS] login identities and root security boundary verified")
PY
