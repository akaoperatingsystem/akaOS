#!/usr/bin/env bash
# Verify Ctrl+C interrupts a foreground stdin consumer and restores akash.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/sigint-test-screen.ppm"
SCREEN_PNG="$BUILD_DIR/sigint-test-screen.png"
RUN_LOG="$BUILD_DIR/sigint-test-run.log"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/build.sh" >/dev/null
fi

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.sigint-test.fd"
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG" <<'EOF'
import subprocess
import sys
import time
from pathlib import Path
from PIL import Image

root_dir, ovmf_code, ovmf_vars, screen_ppm, screen_png, run_log = sys.argv[1:7]
run_log = Path(run_log)
run_log.write_text("")

qemu = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "q35", "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file={root_dir}/build/akaos.img",
    "-serial", f"file:{run_log}", "-monitor", "stdio",
    "-display", "none", "-no-reboot",
], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
   stderr=subprocess.DEVNULL, text=True)

try:
    for _ in range(600):
        if "[INFO] akaOS shell active" in run_log.read_text(errors="ignore"):
            break
        time.sleep(0.05)
    else:
        raise RuntimeError("akaOS shell did not become active")

    time.sleep(0.4)

    def send_text(text):
        for char in text:
            key = {" ": "spc", "/": "slash"}.get(char, char)
            qemu.stdin.write(f"sendkey {key}\n")
        qemu.stdin.write("sendkey ret\n")
        qemu.stdin.flush()

    send_text("root")
    time.sleep(0.5)
    send_text("root")
    time.sleep(0.8)
    qemu.stdin.write("sendkey ctrl-c\n")
    qemu.stdin.flush()
    time.sleep(0.5)
    send_text("echo idleshellsurvived")
    time.sleep(0.8)
    send_text("cat")
    time.sleep(0.8)
    qemu.stdin.write("sendkey ctrl-c\n")
    qemu.stdin.flush()
    time.sleep(1.0)
    send_text("echo interrupted")
    time.sleep(1.0)
    qemu.stdin.write(f"screendump {screen_ppm}\n")
    qemu.stdin.flush()
    time.sleep(0.7)
finally:
    if qemu.poll() is None:
        qemu.stdin.write("quit\n")
        qemu.stdin.flush()
    qemu.wait(timeout=10)

content = run_log.read_text(errors="ignore")
if ("^C" not in content or "idleshellsurvived" not in content or
        "interrupted" not in content or content.count("akaOS tty1") != 1):
    raise RuntimeError("Ctrl+C did not restore the shell for a follow-up echo")
if not Path(screen_ppm).is_file() or Path(screen_ppm).stat().st_size == 0:
    raise RuntimeError("QEMU did not produce the SIGINT screenshot")
Image.open(screen_ppm).convert("RGB").save(screen_png)
print("[PASS] SIGINT foreground-job test completed.")
print(f"[PASS] Screenshot saved: {screen_png}")
EOF
