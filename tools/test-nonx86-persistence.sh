#!/usr/bin/env bash
# Verify shell redirection and two-boot akaFS persistence on ARM and RISC-V.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for builder in build-aarch64.sh build-arm32.sh build-riscv64.sh build-riscv32.sh; do
    "$ROOT_DIR/tools/$builder" >/dev/null
done

echo "[TEST] Verifying non-x86 echo redirection and disk persistence..."

python3 - "$ROOT_DIR" <<'PY'
import fcntl
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path(sys.argv[1])
ports = {
    "aarch64": ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a57",
                "-kernel", str(root / "build/XSCORE_AARCH64.ELF")],
    "arm32": ["qemu-system-arm", "-M", "virt,highmem=off", "-cpu", "cortex-a7",
              "-kernel", str(root / "build/XSCORE_ARM32.ELF")],
    "riscv64": ["qemu-system-riscv64", "-M", "virt",
                "-kernel", str(root / "build/XSCORE_RISCV64.ELF")],
    "riscv32": ["qemu-system-riscv32", "-M", "virt",
                "-kernel", str(root / "build/XSCORE_RISCV32.ELF")],
}


class Guest:
    def __init__(self, architecture, disk, log):
        self.architecture = architecture
        self.log = log
        self.output = ""
        self.offset = 0
        command = ports[architecture] + [
            "-m", "128M", "-drive",
            f"if=none,format=raw,file={disk},id=akaos-storage",
            "-device", "virtio-blk-device,drive=akaos-storage",
            "-serial", "stdio", "-monitor", "none", "-display", "none"]
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=0)
        descriptor = self.process.stdout.fileno()
        flags = fcntl.fcntl(descriptor, fcntl.F_GETFL)
        fcntl.fcntl(descriptor, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    def collect(self):
        try:
            data = self.process.stdout.read()
            if data:
                self.output += data
                with self.log.open("a") as handle:
                    handle.write(data)
        except (BlockingIOError, TypeError):
            pass

    def wait_for(self, text, timeout=35):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.collect()
            index = self.output.find(text, self.offset)
            if index >= 0:
                self.offset = index + len(text)
                return
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"{self.architecture}: QEMU exited waiting for {text!r}\n"
                    + self.output[-2500:])
            time.sleep(0.05)
        raise RuntimeError(
            f"{self.architecture}: timed out waiting for {text!r}\n"
            + self.output[-2500:])

    def send(self, line):
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def logout(self):
        self.send("exit")
        self.wait_for("login:")

    def login(self):
        self.wait_for("writable akaFS data disk mounted")
        self.wait_for("login:")
        self.send("root")
        self.wait_for("Password:")
        self.send("root")
        self.wait_for("root@akaOS:/root#")

    def poweroff(self):
        self.send("poweroff")
        try:
            self.process.wait(timeout=12)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(
                f"{self.architecture}: poweroff did not close QEMU") from error

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


for architecture in ports:
    disk = root / "build" / f"persistence-{architecture}.img"
    log = root / "build" / f"persistence-{architecture}.log"
    with disk.open("wb") as handle:
        handle.truncate(4 * 1024 * 1024)
    log.unlink(missing_ok=True)
    marker = f"persistent_{architecture}_verified"

    first = Guest(architecture, disk, log)
    try:
        first.login()
        first.send(f"echo {marker} > /saved.txt")
        first.wait_for("root@akaOS:/root#")
        first.send("cat /saved.txt")
        first.wait_for(marker)
        first.wait_for("root@akaOS:/root#")
        # The shell's exit builtin commits akaFS automatically; no explicit
        # sync command should be required for the logout/relaunch workflow.
        first.logout()
    finally:
        first.stop()

    second = Guest(architecture, disk, log)
    try:
        second.login()
        second.send("cat /saved.txt")
        second.wait_for(marker)
        second.wait_for("root@akaOS:/root#")
        second.poweroff()
    finally:
        second.stop()

    content = log.read_text(errors="ignore")
    if "[FATAL]" in content or "cannot create" in content:
        raise RuntimeError(f"{architecture}: fatal or echo redirection error\n"
                           + content[-2500:])
    if "akaFS persistent files loaded" not in content:
        raise RuntimeError(f"{architecture}: second boot did not load akaFS")
    print(f"[PASS] {architecture}: echo redirection survived a full power cycle")

print("[PASS] All non-x86 persistence tests passed")
PY
