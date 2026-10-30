#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/build.sh" >/dev/null
    "$ROOT_DIR/tools/build-bios.sh" --no-build >/dev/null
    "$ROOT_DIR/tools/build-x86_32.sh" --firmware bios --disk-type iso >/dev/null
    "$ROOT_DIR/tools/build-aarch64.sh" >/dev/null
    "$ROOT_DIR/tools/build-arm32.sh" >/dev/null
    "$ROOT_DIR/tools/build-riscv64.sh" >/dev/null
    "$ROOT_DIR/tools/build-riscv32.sh" >/dev/null
fi

python3 - "$ROOT_DIR" <<'PY'
import os
import subprocess
import sys
import time
from pathlib import Path

root_dir = Path(sys.argv[1])
targets = {
    "x86_64": ("qemu-system-x86_64", ["-machine", "q35", "-cdrom", str(root_dir / "build/akaos.iso"), "-boot", "d"]),
    "x86_32": ("qemu-system-i386", ["-cdrom", str(root_dir / "build/akaos32.iso"), "-boot", "d"]),
    "aarch64": ("qemu-system-aarch64", ["-M", "virt", "-cpu", "cortex-a57", "-kernel", str(root_dir / "build/XSCORE_AARCH64.ELF")]),
    "arm32": ("qemu-system-arm", ["-M", "virt,highmem=off", "-cpu", "cortex-a7", "-kernel", str(root_dir / "build/XSCORE_ARM32.ELF")]),
    "riscv64": ("qemu-system-riscv64", ["-M", "virt", "-kernel", str(root_dir / "build/XSCORE_RISCV64.ELF")]),
    "riscv32": ("qemu-system-riscv32", ["-M", "virt", "-kernel", str(root_dir / "build/XSCORE_RISCV32.ELF")]),
}
requested = os.environ.get("AKAOS_TEST_ARCHES", "").split()
if requested:
    unknown = [name for name in requested if name not in targets]
    if unknown:
        raise RuntimeError("unknown AKAOS_TEST_ARCHES value(s): " + ", ".join(unknown))
    targets = {name: targets[name] for name in requested}

def to_sendkeys(text):
    keys = {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
            "_": "shift-minus", ":": "shift-semicolon", "#": "shift-3",
            "&": "shift-7"}
    result = []
    for ch in text:
        if "a" <= ch <= "z" or "0" <= ch <= "9":
            result.append(f"sendkey {ch} 5\n")
        elif "A" <= ch <= "Z":
            result.append(f"sendkey shift-{ch.lower()} 5\n")
        elif ch in keys:
            result.append(f"sendkey {keys[ch]} 5\n")
    result.append("sendkey ret 5\n")
    return result

def run_target(name, qemu_bin, kernel_args):
    log_path = root_dir / "build" / f"test-dbus-owner-signals-{name}.log"
    log_path.unlink(missing_ok=True)
    input_devices = [] if name.startswith("x86") else [
        "-device", "virtio-keyboard-device", "-device", "virtio-mouse-device"]
    display_device = ["-vga", "std"] if name == "x86_32" else ["-device", "ramfb"]
    command = [qemu_bin, *kernel_args, "-m", "128M", *display_device,
               *input_devices, "-serial", f"file:{log_path}", "-monitor", "stdio",
               "-display", "none", "-no-reboot"]
    qemu = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, text=True)
    offset = 0

    def content():
        return log_path.read_text(errors="ignore") if log_path.exists() else ""

    def wait_for(needle, timeout=60):
        nonlocal offset
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = content()
            index = data.find(needle, offset)
            if index != -1:
                offset = index + len(needle)
                return
            if qemu.poll() is not None:
                raise RuntimeError(f"{name}: QEMU exited while waiting for {needle!r}")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for {needle!r}")

    def send_line(line):
        if qemu.stdin is None:
            raise RuntimeError(f"{name}: QEMU monitor closed")
        for key in to_sendkeys(line):
            qemu.stdin.write(key)
            qemu.stdin.flush()
            time.sleep(0.02)
        time.sleep(0.25)

    prompt = "root@akaOS:/root#"
    try:
        wait_for("login:", 90)
        send_line("root")
        wait_for("Password:")
        send_line("root")
        wait_for(prompt, 30)
        send_line("dbus-daemon --session &")
        wait_for(prompt, 20)
        send_line("hello --dbus-owner-signal-test")
        wait_for("D-Bus owner-change signals verified", 30)
        wait_for(prompt, 20)
    finally:
        if qemu.poll() is None:
            qemu.kill()
        qemu.wait()
    data = content()
    if "[FATAL]" in data:
        raise RuntimeError(f"{name}: owner signal probe emitted a fatal trap")

for name, (qemu_bin, kernel_args) in targets.items():
    print(f"[TEST] {name}: D-Bus NameOwnerChanged signal delivery", flush=True)
    run_target(name, qemu_bin, kernel_args)
    print(f"[PASS] {name}: D-Bus owner-change signals verified", flush=True)

print("[PASS] D-Bus owner-change signal verification passed on " + ", ".join(targets) + ".")
PY

test_arches="${AKAOS_TEST_ARCHES:-x86_64 x86_32 aarch64 arm32 riscv64 riscv32}"
for arch in $test_arches; do
    log="$ROOT_DIR/build/test-dbus-owner-signals-$arch.log"
    echo "[OUTPUT] $arch owner-signal transcript: $log"
    grep -Fq "D-Bus owner-change signals verified" "$log"
done
