#!/usr/bin/env bash
# Capture the live helloDesktop session as build/akaos-desktop.png.
#
# The console of the guest is on the serial port, so the script talks to it
# through a socket.  The keys of the keyboard belong to the desktop and never
# reach the shell of the console.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/akaos-desktop.ppm"
SCREEN_PNG="$BUILD_DIR/akaos-desktop.png"
CACHE_DIR="${AKAOS_EXT4_CACHE:-/home/lessplum3393/.cache/akaos}"
mkdir -p "$CACHE_DIR"
RUN_LOG="$CACHE_DIR/desktop-run.log"
SERIAL_SOCKET="$CACHE_DIR/desktop-console.sock"
OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$CACHE_DIR/OVMF_VARS.desktop.fd"
rm -f "$OVMF_VARS"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"
DISK_IMAGE="$CACHE_DIR/akaos.img"
if [[ ! -f "$DISK_IMAGE" || "$BUILD_DIR/akaos.img" -nt "$DISK_IMAGE" ]]; then
    cp -f "$BUILD_DIR/akaos.img" "$DISK_IMAGE"
fi

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG" "$SERIAL_SOCKET"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$SCREEN_PPM" "$SCREEN_PNG" \
    "$RUN_LOG" "$DISK_IMAGE" "$SERIAL_SOCKET" <<'EOF'
import os
import socket
import subprocess
import sys
import threading
import time
from PIL import Image

root, ovmf_code, ovmf_vars, ppm, png, log, disk_image, serial_socket = sys.argv[1:9]
build = os.path.dirname(ppm)

command = [
    "qemu-system-x86_64", "-machine", "q35", "-m", "1024M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={disk_image}",
    "-serial", f"unix:{serial_socket},server=on,wait=off",
    "-monitor", "stdio", "-display", "none",
    "-no-reboot"
]
process = subprocess.Popen(command, stdin=subprocess.PIPE,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           text=True)

console = None
for _ in range(200):
    try:
        console = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        console.connect(serial_socket)
        break
    except OSError:
        console.close()
        console = None
        time.sleep(0.1)
if console is None:
    process.kill()
    raise RuntimeError("the console of the guest did not answer")

handle = open(log, "w", encoding="utf-8", errors="ignore")


def pump():
    while True:
        try:
            data = console.recv(4096)
        except OSError:
            break
        if not data:
            break
        handle.write(data.decode("utf-8", "ignore"))
        handle.flush()


reader = threading.Thread(target=pump, daemon=True)
reader.start()


def console_write(text):
    console.sendall(text.encode("utf-8"))
    time.sleep(0.2)


def wait_for(marker, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            if marker in open(log, errors="ignore").read():
                return True
        except OSError:
            pass
        time.sleep(0.05)
    return False


if not wait_for("[INFO] akaOS shell active", 120):
    process.kill()
    raise RuntimeError("akaOS login did not become ready")

time.sleep(0.5)
console_write("root\n")
console_write("root\n")

# Wait for the session of the desktop: the compositor, the display server and
# the window manager have to answer before the screen shows anything.
if wait_for("xserver: ready on display", 60):
    print("the display server is ready!")
time.sleep(4.0)

# 1. The desktop itself.
process.stdin.write(f"screendump {ppm}\n")
process.stdin.flush()
time.sleep(1.5)

# 2. A window that the window manager frames, so that the frame with the
#    traffic light buttons is on the screen.
window_ppm = os.path.join(build, "akaos-window.ppm")
window_png = os.path.join(build, "akaos-window.png")
console_write("x11-wm-client\n")
time.sleep(4.0)
process.stdin.write(f"screendump {window_ppm}\n")
process.stdin.flush()
time.sleep(1.5)

process.stdin.write("quit\n")
process.stdin.flush()
try:
    process.wait(timeout=8)
except subprocess.TimeoutExpired:
    process.kill()

if os.path.exists(ppm):
    Image.open(ppm).convert("RGB").save(png)
    print("saved", png)
if os.path.exists(window_ppm):
    Image.open(window_ppm).convert("RGB").save(window_png)
    print("saved", window_png)
EOF

echo "[PASS] helloDesktop session screenshot captured"
