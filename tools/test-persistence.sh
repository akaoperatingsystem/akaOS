#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

"$ROOT_DIR/build.sh" --arch x86_64 --firmware uefi --disk-type fat32 >/dev/null

DATA_IMAGE="$ROOT_DIR/build/persistence-test.img"
OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$ROOT_DIR/build/OVMF_VARS.persistence-test.fd"
truncate -s 4M "$DATA_IMAGE"
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$DATA_IMAGE" <<'PY'
import struct
import subprocess
import sys
import time
from pathlib import Path

root, ovmf_code, ovmf_vars, data_name = sys.argv[1:]
data_image = Path(data_name)

def monitor_keys(command):
    result = []
    for character in command:
        if "a" <= character <= "z":
            result.append(f"sendkey {character}\n")
        elif "0" <= character <= "9":
            result.append(f"sendkey {character}\n")
        elif character == " ":
            result.append("sendkey spc\n")
        elif character == "/":
            result.append("sendkey slash\n")
        elif character == ".":
            result.append("sendkey dot\n")
        elif character == "-":
            result.append("sendkey minus\n")
        elif character == ">":
            result.append("sendkey shift-dot\n")
        else:
            raise RuntimeError(f"unsupported test character: {character!r}")
    result.append("sendkey ret\n")
    return "".join(result)

def boot(log_name, command=None):
    log_path = Path(root) / "build" / log_name
    log_path.write_bytes(b"")
    command_line = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
        "-drive", f"format=raw,file.locking=off,file={root}/build/akaos.img",
        "-drive", f"if=none,format=raw,file={data_image},id=akaos-storage",
        "-device", "virtio-blk-pci,drive=akaos-storage",
        "-display", "none", "-serial", f"file:{log_path}",
        "-monitor", "stdio", "-no-reboot",
    ]
    process = subprocess.Popen(
        command_line, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True,
    )
    for _ in range(600):
        if log_path.exists() and b"[INFO] akaOS shell active" in log_path.read_bytes():
            break
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited early: {process.stderr.read()}")
        time.sleep(0.05)
    else:
        process.kill()
        raise RuntimeError("akaOS shell did not become active")
    process.stdin.write(monitor_keys("root"))
    process.stdin.write(monitor_keys("root"))
    process.stdin.flush()
    time.sleep(1)
    if command:
        process.stdin.write(monitor_keys(command))
        process.stdin.flush()
        time.sleep(2)
    else:
        # Desktop autostart initializes Lumen before Aurora can register its
        # surfaces.  Wait for the launcher milestone instead of quitting on a
        # fixed two-second timer, which made a healthy cold boot look broken.
        for _ in range(160):
            current_log = log_path.read_text(errors="ignore")
            if "[desktop] Starting aurora desktop environment..." in current_log:
                break
            if "[desktop] Error:" in current_log:
                break
            time.sleep(0.05)
    process.stdin.write("quit\n")
    process.stdin.flush()
    process.wait(timeout=10)
    return log_path.read_text(errors="ignore")

boot("persistence-first.log", "echo survives-reboot > /saved.txt")
boot("persistence-config.log", "akaos-config --enable-autostart-desktop")
disk = data_image.read_bytes()
header_a = disk[8 * 512:9 * 512]
header_b = disk[9 * 512:10 * 512]
if header_a[:8] != b"AKAFS002" and header_b[:8] != b"AKAFS002" and header_a[:8] != b"AKAFS001":
    raise RuntimeError("akaFS header was not written")

headers = [header for header in (header_a, header_b)
           if header[:8] in (b"AKAFS001", b"AKAFS002")]
if not headers:
    raise RuntimeError("akaFS has no valid superblock")
# The journal alternates superblocks.  Select the newest committed generation,
# rather than assuming the second copy is active; a config write can leave the
# newest record in either slot.
active_header = max(headers, key=lambda header: struct.unpack_from("<I", header, 12)[0])
payload_size = struct.unpack_from("<I", active_header, 20)[0]
payload_sector = 10 if active_header[:8] == b"AKAFS002" else 9
payload = disk[payload_sector * 512:payload_sector * 512 + payload_size]
if b"/saved.txt" not in payload or b"survives-reboot" not in payload:
    raise RuntimeError("saved file is absent from the akaFS payload")

second_log = boot("persistence-second.log")
if "[INFO] akaFS persistent files loaded" not in second_log:
    raise RuntimeError("akaFS did not load the saved state on reboot")
if "[desktop] Starting aurora desktop environment..." not in second_log:
    raise RuntimeError("AUTOSTART_DESKTOP=1 did not launch the desktop on reboot")
if "[desktop] akaOS Desktop Environment (aurora) is running." not in second_log:
    raise RuntimeError("desktop launcher did not stay active after autostart")

print("[PASS] akaFS files and desktop autostart were written to disk and loaded on the next boot")
PY
