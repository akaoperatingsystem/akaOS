#!/usr/bin/env bash
# Test akaOS Advanced Network Stack, DNS, curl, httpd, and akaos-config
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/advanced-network-screen.ppm"
SCREEN_PNG="$BUILD_DIR/advanced-network-verified.png"
RUN_LOG="$BUILD_DIR/advanced-network-run.log"

"$ROOT_DIR/build.sh" >/dev/null

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.advnet.fd"
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
            lines.append(f"sendkey {ch} 10\n")
        elif 'A' <= ch <= 'Z':
            lines.append(f"sendkey shift-{ch.lower()} 10\n")
        elif '0' <= ch <= '9':
            lines.append(f"sendkey {ch} 10\n")
        elif ch == ' ':
            lines.append("sendkey spc 10\n")
        elif ch == '/':
            lines.append("sendkey slash 10\n")
        elif ch == '.':
            lines.append("sendkey dot 10\n")
        elif ch == '-':
            lines.append("sendkey minus 10\n")
        elif ch == '_':
            lines.append("sendkey shift-minus 10\n")
        elif ch == '>':
            lines.append("sendkey shift-dot 10\n")
        elif ch == '<':
            lines.append("sendkey shift-comma 10\n")
        elif ch == '"':
            lines.append("sendkey shift-apostrophe 10\n")
        elif ch == '$':
            lines.append("sendkey shift-4 10\n")
        elif ch == '&':
            lines.append("sendkey shift-7 10\n")
        elif ch == '?':
            lines.append("sendkey shift-slash 10\n")
        elif ch == '|':
            lines.append("sendkey shift-backslash 10\n")
        elif ch == ':':
            lines.append("sendkey shift-semicolon 10\n")
        elif ch == ',':
            lines.append("sendkey comma 10\n")
        elif ch == '=':
            lines.append("sendkey equal 10\n")
        elif ch == '+':
            lines.append("sendkey shift-equal 10\n")
        elif ch == '!':
            lines.append("sendkey shift-1 10\n")
        elif ch == '*':
            lines.append("sendkey shift-8 10\n")
    lines.append("sendkey ret 10\n")
    return lines

qemu_cmd = [
    "qemu-system-x86_64",
    "-machine", "q35",
    "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={root_dir}/build/akaos.img",
    "-netdev", "user,id=net0,hostfwd=tcp::8080-:80",
    "-device", "virtio-net-pci,netdev=net0,disable-legacy=off",
    "-serial", f"file:{run_log}",
    "-monitor", "stdio",
    "-display", "none",
    "-no-reboot"
]

proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

def send_cmd(cmd):
    for line in to_sendkeys(cmd):
        proc.stdin.write(line)
        proc.stdin.flush()
        time.sleep(0.05)

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

send_cmd("root")
time.sleep(0.5)
send_cmd("root")
time.sleep(1.0)

commands = [
    "akaos-config --status",
    "sysmon --once",
    "nslookup google.com",
    "ping -c 2 10.0.2.2",
    "akaos-config --enable-httpd",
    "akaos-config --status",
    "httpd &",
    "curl -i http://127.0.0.1:80/"
]

for cmd in commands:
    send_cmd(cmd)
    time.sleep(3.0)

time.sleep(1.0)
proc.stdin.write(f"screendump {screen_ppm}\n")
proc.stdin.flush()
time.sleep(0.5)
proc.stdin.write("quit\n")
proc.stdin.flush()
proc.wait()

Image.open(screen_ppm).convert("RGB").save(screen_png)
print("Advanced network test screenshot saved:", screen_png)
EOF

chmod +x "$ROOT_DIR/tools/test-advanced-network.sh"
echo "[TEST] Validating serial output log..."
grep -Fq "akaOS System Configuration" "$RUN_LOG"
grep -Fq "Web Server:        DISABLED" "$RUN_LOG"
grep -Fq "akaOS System Telemetry Monitor" "$RUN_LOG"
grep -Fq "Server:" "$RUN_LOG"
grep -Fq "64 bytes from 10.0.2.2" "$RUN_LOG"
grep -Fq "HTTP web server ENABLED on port 80" "$RUN_LOG"
grep -Fq "Web Server:        ENABLED" "$RUN_LOG"
grep -Fq "HTTP/1.0 200 OK" "$RUN_LOG"
grep -Fq "Welcome to akaOS" "$RUN_LOG"
grep -Fq "[INFO] VirtIO-Net MSI-X interrupt received" "$RUN_LOG"
grep -Fq "[INFO] TTY hardware interrupt received" "$RUN_LOG"
echo "[PASS] All advanced network verifications passed!"
