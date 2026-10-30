#!/usr/bin/env bash
# Verify x86_32 shell redirection survives a complete QEMU power cycle.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
ISO="$BUILD_DIR/akaos32.iso"
DATA_IMAGE="$BUILD_DIR/persistence-x86_32.img"

"$ROOT_DIR/build.sh" --arch x86_32 --firmware bios --disk-type iso >/dev/null
truncate -s 4M "$DATA_IMAGE"

python3 - "$ROOT_DIR" "$ISO" "$DATA_IMAGE" <<'PY'
import os
import select
import subprocess
import sys
import time
from pathlib import Path

root, iso_name, data_name = map(Path, sys.argv[1:])
marker = "persistent_x86_32_verified"


class Guest:
    def __init__(self):
        self.output = ""
        self.process = subprocess.Popen(
            [
                "qemu-system-i386", "-M", "pc", "-m", "128M",
                "-cdrom", str(iso_name), "-boot", "d",
                "-drive", f"format=raw,file={data_name},if=ide,index=0,media=disk",
                "-display", "none", "-monitor", "none", "-serial", "stdio",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        os.set_blocking(self.process.stdout.fileno(), False)

    def collect(self):
        ready, _, _ = select.select([self.process.stdout], [], [], 0.05)
        if ready:
            data = self.process.stdout.read()
            if data:
                self.output += data.decode(errors="ignore")

    def wait_for(self, text, timeout=35, start=0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.collect()
            if text in self.output[start:]:
                return
            if self.process.poll() is not None:
                raise RuntimeError(f"QEMU exited before {text!r}\n{self.output[-2000:]}")
        raise RuntimeError(f"Timed out waiting for {text!r}\n{self.output[-2000:]}")

    def send(self, line):
        self.process.stdin.write((line + "\n").encode())
        self.process.stdin.flush()

    def command(self, line):
        start = len(self.output)
        self.send(line)
        self.wait_for("root@akaOS:/root#", start=start)

    def logout(self):
        start = len(self.output)
        self.send("exit")
        self.wait_for("login:", start=start)

    def login(self):
        self.wait_for("Writable akaFS data disk mounted")
        self.wait_for("login:")
        self.send("root")
        self.wait_for("Password:")
        self.send("root")
        self.wait_for("root@akaOS:/root#")

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


first = Guest()
try:
    first.login()
    first.command(f"echo {marker} > /saved.txt")
    # Exiting the shell must commit dirty akaFS blocks automatically.
    first.logout()
finally:
    # QEMU termination is used as the host-side power cycle after logout.
    first.close()

second = Guest()
try:
    second.login()
    second.send("cat /saved.txt")
    second.wait_for(marker)
finally:
    second.close()

if "[FATAL]" in second.output or "cannot create" in second.output:
    raise RuntimeError("x86_32 persistence boot emitted a fatal or write error")
print("[PASS] x86_32 echo redirection survived a full QEMU power cycle")
PY
