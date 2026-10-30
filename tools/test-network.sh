#!/usr/bin/env bash
# Execute the full realistic Unix sequence in akaOS interactive shell and capture screenshot
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/interactive-test-screen.ppm"
SCREEN_PNG="$BUILD_DIR/interactive-test-screen.png"
RUN_LOG="$BUILD_DIR/interactive-test-run.log"

"$ROOT_DIR/build.sh" >/dev/null

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.interactive.fd"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG" <<'EOF'
import sys
import subprocess
import time
from PIL import Image

root_dir, ovmf_code, ovmf_vars, screen_ppm, screen_png, run_log = sys.argv[1:7]

def to_sendkeys(cmd):
    lines = []
    for ch in cmd:
        if 'a' <= ch <= 'z':
            lines.append(f"sendkey {ch}\n")
        elif 'A' <= ch <= 'Z':
            lines.append(f"sendkey shift-{ch.lower()}\n")
        elif '0' <= ch <= '9':
            lines.append(f"sendkey {ch}\n")
        elif ch == ' ':
            lines.append("sendkey spc\n")
        elif ch == '/':
            lines.append("sendkey slash\n")
        elif ch == '.':
            lines.append("sendkey dot\n")
        elif ch == '-':
            lines.append("sendkey minus\n")
        elif ch == '_':
            lines.append("sendkey shift-minus\n")
        elif ch == '>':
            lines.append("sendkey shift-dot\n")
        elif ch == '<':
            lines.append("sendkey shift-comma\n")
        elif ch == '"':
            lines.append("sendkey shift-apostrophe\n")
        elif ch == '$':
            lines.append("sendkey shift-4\n")
        elif ch == '?':
            lines.append("sendkey shift-slash\n")
        elif ch == '|':
            lines.append("sendkey shift-backslash\n")
        elif ch == ':':
            lines.append("sendkey shift-semicolon\n")
        elif ch == ',':
            lines.append("sendkey comma\n")
        elif ch == '=':
            lines.append("sendkey equal\n")
        elif ch == '+':
            lines.append("sendkey shift-equal\n")
        elif ch == '!':
            lines.append("sendkey shift-1\n")
        elif ch == '*':
            lines.append("sendkey shift-8\n")
    lines.append("sendkey ret\n")
    return "".join(lines)

qemu_cmd = [
    "qemu-system-x86_64",
    "-machine", "q35",
    "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={root_dir}/build/akaos.img",
    "-netdev", "user,id=net0",
    "-device", "virtio-net-pci,netdev=net0,disable-legacy=off",
    "-serial", f"file:{run_log}",
    "-monitor", "stdio",
    "-display", "none",
    "-no-reboot"
]

proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

# Wait for shell active in run_log
for _ in range(600):
    try:
        with open(run_log, "r", errors="ignore") as f:
            content = f.read()
            if "[INFO] akaOS shell active" in content:
                break
    except Exception:
        pass
    time.sleep(0.05)

time.sleep(0.5)

proc.stdin.write(to_sendkeys("root"))
proc.stdin.write(to_sendkeys("root"))
proc.stdin.flush()
time.sleep(1.0)

commands = [
    "ping -c 2 10.0.2.2",
    "netstat -i"
]

for cmd in commands:
    send_str = to_sendkeys(cmd)
    proc.stdin.write(send_str)
    proc.stdin.flush()
    time.sleep(2.5)

time.sleep(1.0)
proc.stdin.write(f"screendump {screen_ppm}\n")
proc.stdin.flush()
time.sleep(0.5)
proc.stdin.write("quit\n")
proc.stdin.flush()
proc.wait()

Image.open(screen_ppm).convert("RGB").save(screen_png)
print("Network test screenshot saved:", screen_png)
EOF

grep -Fq "[INFO] VirtIO-Net PCI device initialized" "$RUN_LOG"
grep -Fq "PING 10.0.2.2 (10.0.2.2)" "$RUN_LOG"
grep -Fq "64 bytes from 10.0.2.2" "$RUN_LOG"
grep -Fq "0% packet loss" "$RUN_LOG"
grep -Fq "RX packets:" "$RUN_LOG"
grep -Fq "TX packets:" "$RUN_LOG"
echo "[PASS] VirtIO-Net hardware stack & gateway ping verified with 0% packet loss!"
