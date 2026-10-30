#!/usr/bin/env bash
# Verify reboot, poweroff, and shutdown on every native non-x86 QEMU port.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for builder in build-aarch64.sh build-arm32.sh build-riscv64.sh build-riscv32.sh; do
    "$ROOT_DIR/tools/$builder" >/dev/null
done

echo "[TEST] Verifying non-x86 lifecycle commands in QEMU..."

python3 - "$ROOT_DIR" <<'PY'
import fcntl
import os
import subprocess
import sys
import time

root = sys.argv[1]
ports = {
    "aarch64": ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a57",
                "-kernel", f"{root}/build/XSCORE_AARCH64.ELF"],
    "arm32": ["qemu-system-arm", "-M", "virt,highmem=off", "-cpu", "cortex-a7",
              "-kernel", f"{root}/build/XSCORE_ARM32.ELF"],
    "riscv64": ["qemu-system-riscv64", "-M", "virt",
                "-kernel", f"{root}/build/XSCORE_RISCV64.ELF"],
    "riscv32": ["qemu-system-riscv32", "-M", "virt",
                "-kernel", f"{root}/build/XSCORE_RISCV32.ELF"],
}


class Guest:
    def __init__(self, architecture):
        self.architecture = architecture
        self.output = ""
        self.offset = 0
        self.process = subprocess.Popen(
            ports[architecture] + ["-m", "128M", "-serial", "stdio",
                                   "-monitor", "none", "-display", "none"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=0)
        fd = self.process.stdout.fileno()
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    def collect(self):
        try:
            data = self.process.stdout.read()
            if data:
                self.output += data
        except (BlockingIOError, TypeError):
            pass

    def wait_for(self, text, timeout=25):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.collect()
            index = self.output.find(text, self.offset)
            if index >= 0:
                self.offset = index + len(text)
                return
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"{self.architecture}: QEMU exited while waiting for {text!r}\n"
                    + self.output[-2000:])
            time.sleep(0.05)
        raise RuntimeError(
            f"{self.architecture}: timed out waiting for {text!r}\n"
            + self.output[-2000:])

    def send(self, line):
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def login(self):
        self.wait_for("login:")
        self.send("root")
        self.wait_for("Password:")
        self.send("root")
        self.wait_for("root@akaOS:/root#")

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def check_reboot(architecture):
    guest = Guest(architecture)
    try:
        guest.login()
        guest.send("reboot")
        guest.wait_for("[INFO] System reboot requested")
        guest.wait_for("login:", timeout=30)
        if guest.process.poll() is not None:
            raise RuntimeError(f"{architecture}: reboot closed QEMU")
    finally:
        guest.stop()
    print(f"[PASS] {architecture}: reboot reset the guest and kept QEMU open")


def check_power_command(architecture, command):
    guest = Guest(architecture)
    try:
        guest.login()
        guest.send(command)
        guest.wait_for("[INFO] System poweroff requested")
        try:
            guest.process.wait(timeout=10)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(
                f"{architecture}: {command} left QEMU running") from error
    finally:
        guest.stop()
    print(f"[PASS] {architecture}: {command} powered off and closed QEMU")


for arch in ports:
    check_reboot(arch)
    check_power_command(arch, "poweroff")
    check_power_command(arch, "shutdown")

print("[PASS] All non-x86 lifecycle commands work correctly")
PY
