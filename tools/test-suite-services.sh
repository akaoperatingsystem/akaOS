#!/usr/bin/env bash
# Test akaOS Remote Shell, GUI apps, VFS mount table, and Linux-compatible ip utility
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/suite-services-screen.ppm"
SCREEN_PNG="$BUILD_DIR/suite-services-verified.png"
RUN_LOG="$BUILD_DIR/suite-services-run.log"

"$ROOT_DIR/build.sh" >/dev/null

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.services.fd"
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
        elif ch == '&':
            lines.append("sendkey shift-7\n")
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
    "-netdev", "user,id=net0,hostfwd=tcp::18080-:80,hostfwd=tcp::12323-:23,hostfwd=udp::16969-:69",
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
    "ip addr",
    "ip link",
    "ip route",
    "ip neigh",
    "mount",
    "akaos-config --status",
    "akaos-config --set-telnet 1",
    "akaos-config --set-tftp 1",
    "akaos-config --status",
    "notepad --self-test",
    "taskmgr --self-test",
    "webviewer --self-test",
    "akaos-config --desktop-env kde",
    "akaos-config --desktop-env aurora",
    "akaos-config --enable-autostart-desktop",
    "akaos-config --disable-httpd --enable-httpd --status",
    "akaos-config --port 0",
    "akaos-config --autostart-desktop 1 --hostname akaOS-test",
    "akaos-config --status",
    "aurora --once",
    "desktop --check",
    "desktop --help"
]

for cmd in commands:
    send_str = to_sendkeys(cmd)
    proc.stdin.write(send_str)
    proc.stdin.flush()
    time.sleep(2.0)

time.sleep(1.0)
proc.stdin.write(f"screendump {screen_ppm}\n")
proc.stdin.flush()
time.sleep(0.5)
proc.stdin.write("quit\n")
proc.stdin.flush()
proc.wait()

Image.open(screen_ppm).convert("RGB").save(screen_png)
print("Suite services test screenshot saved:", screen_png)
EOF

chmod +x "$ROOT_DIR/tools/test-suite-services.sh"
echo "[TEST] Validating serial output log..."
grep -Fq "eth0: <BROADCAST,MULTICAST,UP>" "$RUN_LOG"
grep -Fq "inet 10.0.2.15/24" "$RUN_LOG"
grep -Fq "default via 10.0.2.2 dev eth0" "$RUN_LOG"
grep -Fq "rootfs on / type akafs (rw)" "$RUN_LOG"
grep -Fq "devfs on /dev type devfs (rw)" "$RUN_LOG"
grep -Fq "procfs on /proc type procfs (ro)" "$RUN_LOG"
grep -Fq "Telnet Server:" "$RUN_LOG"
grep -Fq "TFTP Server:" "$RUN_LOG"
grep -Fq "DISABLED [Inactive]" "$RUN_LOG"
grep -Fq "Telnet daemon ENABLED on port 23" "$RUN_LOG"
grep -Fq "TFTP daemon ENABLED on port 69" "$RUN_LOG"
grep -Fq "ENABLED [Active]" "$RUN_LOG"

grep -Fq "notepad: surface and compositor protocol verified" "$RUN_LOG"
grep -Fq "taskmgr: surface and compositor protocol verified" "$RUN_LOG"
grep -Fq "webviewer: surface and compositor protocol verified" "$RUN_LOG"
grep -Fq "akaos-config: error: desktop environment 'kde' is not installed" "$RUN_LOG"
grep -Fq "akaos-config: Desktop environment set to aurora" "$RUN_LOG"
grep -Fq "akaos-config: Desktop autostart ENABLED" "$RUN_LOG"
grep -Fq "akaos-config: error: invalid HTTP port '0' (use 1-65535)" "$RUN_LOG"
grep -Fq "akaos-config: Hostname set to akaOS-test" "$RUN_LOG"
grep -Fq "Desktop Autostart: ENABLED" "$RUN_LOG"
grep -Fq "Desktop Env:       aurora" "$RUN_LOG"
grep -Fq "Hostname:          akaOS-test" "$RUN_LOG"
grep -Fq "aurora: desktop shell initialized" "$RUN_LOG"
grep -Fq "akaOS Desktop Environment Launcher" "$RUN_LOG"
grep -Fq "akaOS desktop runtime readiness" "$RUN_LOG"
grep -Fq "Aurora (aurora): READY" "$RUN_LOG"
grep -Fq "LXDE (lxde): COMPATIBILITY ONLY (upstream session not installed)" "$RUN_LOG"
grep -Fq "LXQt (lxqt): COMPATIBILITY ONLY (upstream session not installed)" "$RUN_LOG"
grep -Fq "GNOME (gnome): COMPATIBILITY ONLY (upstream session not installed)" "$RUN_LOG"
grep -Fq "KDE Plasma (kde): COMPATIBILITY ONLY (upstream session not installed)" "$RUN_LOG"
echo "[PASS] All services, GUI, mount table, and ip tests passed!"
