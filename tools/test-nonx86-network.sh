#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/tools/build-aarch64.sh"
    "$ROOT_DIR/tools/build-arm32.sh"
    "$ROOT_DIR/tools/build-riscv64.sh"
    "$ROOT_DIR/tools/build-riscv32.sh"
fi

for image in \
    "$ROOT_DIR/build/XSCORE_AARCH64.ELF" \
    "$ROOT_DIR/build/XSCORE_ARM32.ELF" \
    "$ROOT_DIR/build/XSCORE_RISCV64.ELF" \
    "$ROOT_DIR/build/XSCORE_RISCV32.ELF"; do
    if [[ ! -f "$image" ]]; then
        echo "[FAIL] Missing native kernel image: $image" >&2
        exit 1
    fi
done

echo "[TEST] Running native MMIO VirtIO-Net ping/curl verification..."

python3 - "$ROOT_DIR" <<'PY'
import fcntl
import http.server
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

root_dir = Path(sys.argv[1])

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"akaOS native VirtIO-Net curl verified\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass

server = http.server.ThreadingHTTPServer(("0.0.0.0", 0), Handler)
server_port = server.server_address[1]
server_thread = threading.Thread(target=server.serve_forever, daemon=True)
server_thread.start()

targets = {
    "aarch64": (
        "qemu-system-aarch64",
        ["-M", "virt", "-cpu", "cortex-a57", "-kernel", str(root_dir / "build/XSCORE_AARCH64.ELF")],
    ),
    "arm32": (
        "qemu-system-arm",
        ["-M", "virt,highmem=off", "-cpu", "cortex-a7", "-kernel", str(root_dir / "build/XSCORE_ARM32.ELF")],
    ),
    "riscv64": (
        "qemu-system-riscv64",
        ["-M", "virt", "-kernel", str(root_dir / "build/XSCORE_RISCV64.ELF")],
    ),
    "riscv32": (
        "qemu-system-riscv32",
        ["-M", "virt", "-kernel", str(root_dir / "build/XSCORE_RISCV32.ELF")],
    ),
}

def run_target(name, qemu_bin, kernel_args):
    log_path = root_dir / "build" / f"test-nonx86-network-{name}.log"
    log_path.unlink(missing_ok=True)
    command = [
        qemu_bin,
        *kernel_args,
        "-m", "128M",
        "-device", "ramfb",
        "-device", "virtio-keyboard-device",
        "-device", "virtio-mouse-device",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-device,netdev=net0",
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot",
    ]
    qemu = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=0,
    )
    fd = qemu.stdout.fileno()
    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
    collected = []
    offset = 0

    def content():
        try:
            data = qemu.stdout.read()
            if data:
                collected.append(data)
                with log_path.open("a", encoding="utf-8") as stream:
                    stream.write(data)
        except (BlockingIOError, OSError):
            pass
        return "".join(collected)

    def wait_for(needle, timeout=30):
        nonlocal offset
        deadline = time.time() + timeout
        while time.time() < deadline:
            output = content()
            index = output.find(needle, offset)
            if index != -1:
                offset = index + len(needle)
                return
            if qemu.poll() is not None:
                raise RuntimeError(f"{name}: QEMU exited while waiting for {needle!r}")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for {needle!r}")

    def send_line(line):
        if qemu.stdin is None:
            raise RuntimeError(f"{name}: QEMU stdin closed")
        qemu.stdin.write(line + "\n")
        qemu.stdin.flush()
        time.sleep(0.35)

    prompt = "root@akaOS:/root#"
    try:
        wait_for("[INFO] VirtIO-Net MMIO device initialized")
        wait_for("login:")
        send_line("root")
        wait_for("Password:")
        send_line("root")
        wait_for(prompt)

        send_line("ping -c 2 10.0.2.2")
        wait_for("2 packets transmitted, 2 received, 0% packet loss", 40)
        wait_for(prompt)

        send_line("netstat -i")
        wait_for("eth0")
        wait_for("RX packets:")
        wait_for(prompt)

        send_line(f"curl -s http://10.0.2.2:{server_port}/")
        wait_for("akaOS native VirtIO-Net curl verified", 40)
        if "\x00" in "".join(collected):
            raise RuntimeError(f"{name}: curl response contained padded NUL bytes")
        wait_for(prompt)

        send_line(f"echo {name}_mmio_network_verified")
        wait_for(f"{name}_mmio_network_verified")
        wait_for(prompt)
        content()
    finally:
        if qemu.poll() is None:
            qemu.kill()
        qemu.wait()
        content()

for name, (qemu_bin, kernel_args) in targets.items():
    print(f"[TEST] {name}: VirtIO-Net MMIO + ping + curl", flush=True)
    run_target(name, qemu_bin, kernel_args)
    print(f"[PASS] {name}: external VirtIO-Net traffic verified", flush=True)

server.shutdown()
server.server_close()
print("[PASS] Native MMIO VirtIO-Net verification passed on AArch64, ARM32, RISC-V64, and RISC-V32.")
PY

for arch in aarch64 arm32 riscv64 riscv32; do
    log="$ROOT_DIR/build/test-nonx86-network-$arch.log"
    echo "[OUTPUT] $arch network transcript: $log"
    cat "$log"
    grep -Fq "VirtIO-Net MMIO device initialized" "$log"
    grep -Fq "2 packets transmitted, 2 received, 0% packet loss" "$log"
    grep -Fq "akaOS native VirtIO-Net curl verified" "$log"
    grep -Fq "${arch}_mmio_network_verified" "$log"
    if grep -Fq "[FATAL]" "$log"; then
        echo "[FAIL] $arch emitted a fatal trap." >&2
        exit 1
    fi
done

echo "[PASS] Native MMIO VirtIO-Net ping/curl verification passed."
