#!/usr/bin/env bash
# Capture the live Aurora desktop as build/aurora-desktop.png.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/aurora-desktop.ppm"
SCREEN_PNG="$BUILD_DIR/aurora-desktop.png"
RUN_LOG="$BUILD_DIR/aurora-desktop-run.log"
OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.desktop.fd"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG" <<'EOF'
import subprocess
import sys
import time
from PIL import Image

root, ovmf_code, ovmf_vars, ppm, png, log = sys.argv[1:7]

def keys(value):
    result = []
    for char in value:
        if 'a' <= char <= 'z': result.append(f"sendkey {char}\n")
        elif '0' <= char <= '9': result.append(f"sendkey {char}\n")
        elif char == ' ': result.append("sendkey spc\n")
        elif char == '-': result.append("sendkey minus\n")
        elif char == '&': result.append("sendkey shift-7\n")
        else: raise ValueError(f"unsupported capture key: {char!r}")
    result.append("sendkey ret\n")
    return ''.join(result)

command = [
    "qemu-system-x86_64", "-machine", "q35", "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={root}/build/akaos.img",
    "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
    "-no-reboot"
]
process = subprocess.Popen(command, stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)

for _ in range(800):
    try:
        if "[INFO] akaOS shell active" in open(log, errors="ignore").read():
            break
    except OSError:
        pass
    time.sleep(0.05)
else:
    process.kill()
    raise RuntimeError("akaOS login did not become ready")

time.sleep(0.4)
process.stdin.write(keys("root") + keys("root") + keys("desktop --apps"))
process.stdin.flush()
time.sleep(3.0)
# Lumen now owns keyboard input exclusively while the graphical session is
# active. Launch applications through Aurora, not through the hidden TTY.
process.stdin.write("sendkey f1\n")
process.stdin.flush()
time.sleep(0.4)
process.stdin.write(keys("task manager"))
process.stdin.flush()
time.sleep(1.0)
process.stdin.write("sendkey f1\n")
process.stdin.flush()
time.sleep(0.4)
process.stdin.write(keys("text editor"))
process.stdin.flush()
time.sleep(1.2)
# Prove the production x86 PS/2 pointer path by moving from the initial screen
# center onto the taskbar launcher in small, pixel-precise steps and clicking.
for _ in range(10):
    process.stdin.write("mouse_move -5 0\n")
    process.stdin.flush()
    time.sleep(0.01)
for _ in range(72):
    process.stdin.write("mouse_move 0 5\n")
    process.stdin.flush()
    time.sleep(0.01)
process.stdin.write("mouse_button 1\nmouse_button 0\n")
process.stdin.flush()
time.sleep(0.8)
process.stdin.write(f"screendump {ppm}\n")
process.stdin.flush()
time.sleep(0.7)
process.stdin.write("quit\n")
process.stdin.flush()
process.wait(timeout=10)

Image.open(ppm).convert("RGB").save(png)
print("saved", png)
EOF

echo "[PASS] Aurora desktop screenshot captured"
