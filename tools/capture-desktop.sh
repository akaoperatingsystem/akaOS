#!/usr/bin/env bash
# Capture the live LXDE desktop as build/akaos-desktop.png.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/akaos-desktop.ppm"
SCREEN_PNG="$BUILD_DIR/akaos-desktop.png"
CACHE_DIR="${AKAOS_EXT4_CACHE:-/home/lessplum3393/.cache/akaos}"
mkdir -p "$CACHE_DIR"
RUN_LOG="$CACHE_DIR/desktop-run.log"
OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$CACHE_DIR/OVMF_VARS.desktop.fd"
rm -f "$OVMF_VARS"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"
DISK_IMAGE="$CACHE_DIR/akaos.img"
if [[ ! -f "$DISK_IMAGE" || "$BUILD_DIR/akaos.img" -nt "$DISK_IMAGE" ]]; then
    cp -f "$BUILD_DIR/akaos.img" "$DISK_IMAGE"
fi

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG" "$DISK_IMAGE" <<'EOF'
import subprocess
import sys
import time
from PIL import Image

root, ovmf_code, ovmf_vars, ppm, png, log, disk_image = sys.argv[1:8]

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
    "qemu-system-x86_64", "-machine", "q35", "-m", "1024M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={disk_image}",
    "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
    "-no-reboot"
]
process = subprocess.Popen(command, stdin=subprocess.PIPE,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           text=True)

for _ in range(3000):
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
process.stdin.write(keys("root") + keys("root"))
process.stdin.flush()
# Wait for desktop session to fully spawn components and map windows
# Wait for desktop session components to finish mapping
for _ in range(800):
    try:
        content = open(log, errors="ignore").read()
        if "c5 request 8" in content or "c6 request 8" in content or "c7 request 8" in content or "c8 request 8" in content:
            print("Desktop window mapped!")
            break
    except OSError:
        pass
    time.sleep(0.1)
time.sleep(12.0)
# Trigger root menu on the desktop via right-click (mouse_button 4 in QEMU HMP)
process.stdin.write("mouse_button 4\nmouse_button 0\n")
process.stdin.flush()
time.sleep(1.5)
# Capture live desktop frame
process.stdin.write(f"screendump {ppm}\n")
process.stdin.flush()
time.sleep(0.8)
process.stdin.write("quit\n")
process.stdin.flush()
try:
    process.wait(timeout=5)
except subprocess.TimeoutExpired:
    process.kill()

Image.open(ppm).convert("RGB").save(png)
print("saved", png)
EOF

echo "[PASS] LXDE desktop screenshot captured"
