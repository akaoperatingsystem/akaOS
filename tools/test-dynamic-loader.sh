#!/usr/bin/env bash
# Exercise the native PT_INTERP handoff on every supported guest.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$ROOT_DIR" <<'PY'
import os
import subprocess
import sys
import time
from pathlib import Path

root = Path(sys.argv[1])
targets = {
    "x86_64": ("qemu-system-x86_64", ["-machine", "q35", "-cdrom", str(root / "build/akaos.iso"), "-boot", "d"]),
    "x86_32": ("qemu-system-i386", ["-cdrom", str(root / "build/akaos32.iso"), "-boot", "d"]),
    "aarch64": ("qemu-system-aarch64", ["-M", "virt", "-cpu", "cortex-a57", "-kernel", str(root / "build/XSCORE_AARCH64.ELF")]),
    "arm32": ("qemu-system-arm", ["-M", "virt,highmem=off", "-cpu", "cortex-a7", "-kernel", str(root / "build/XSCORE_ARM32.ELF")]),
    "riscv64": ("qemu-system-riscv64", ["-M", "virt", "-kernel", str(root / "build/XSCORE_RISCV64.ELF")]),
    "riscv32": ("qemu-system-riscv32", ["-M", "virt", "-kernel", str(root / "build/XSCORE_RISCV32.ELF")]),
}
requested = os.environ.get("AKAOS_TEST_ARCHES", "").split()
test_memory_override = os.environ.get("AKAOS_TEST_MEMORY")
if requested:
    targets = {name: targets[name] for name in requested}

def key_for(ch):
    if "A" <= ch <= "Z":
        return "shift-" + ch.lower()
    return {
        " ": "spc", "/": "slash", ".": "dot", "-": "minus",
        "_": "shift-minus", "&": "shift-7", "=": "equal",
        ":": "shift-semicolon",
    }.get(ch, ch)

def send_line(proc, text, delay):
    for ch in text:
        proc.stdin.write(f"sendkey {key_for(ch)} 5\n")
        proc.stdin.flush()
        time.sleep(delay)
    proc.stdin.write("sendkey ret 5\n")
    proc.stdin.flush()
    time.sleep(0.25)

for name, (binary, args) in targets.items():
    log = root / "build" / f"test-dynamic-loader-{name}.log"
    log.unlink(missing_ok=True)
    input_args = [] if name.startswith("x86") else [
        "-device", "virtio-keyboard-device", "-device", "virtio-mouse-device"
    ]
    display = ["-vga", "std"] if name == "x86_32" else ["-device", "ramfb"]
    test_memory = test_memory_override or ("256M" if name == "x86_64" else "128M")
    proc = subprocess.Popen(
        [binary, *args, "-m", test_memory, *display, *input_args,
         "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none", "-no-reboot"],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        text=True,
    )
    # ARM32's virtio-input IRQ path is intentionally conservative while the
    # guest enables the line discipline; pacing it like the 32-bit x86 guest
    # avoids dropping the first login character when QEMU is under host load.
    delay = 0.25 if name in ("x86_32", "arm32") else (0.10 if name == "x86_64" else 0.05)

    def content():
        return log.read_text(errors="ignore") if log.exists() else ""

    def wait_for(marker, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if marker in content():
                return
            if proc.poll() is not None:
                raise RuntimeError(f"{name}: guest exited while waiting for {marker!r}")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for {marker!r}")

    def wait_for_line(marker, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if any(line.strip() == marker for line in content().splitlines()):
                return
            if proc.poll() is not None:
                raise RuntimeError(f"{name}: guest exited while waiting for line {marker!r}")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for line {marker!r}")

    def wait_for_prompt_count(target_count, timeout):
        prompt = "root@akaOS:/root#"
        deadline = time.time() + timeout
        while time.time() < deadline:
            if content().count(prompt) >= target_count:
                return
            if proc.poll() is not None:
                raise RuntimeError(f"{name}: guest exited while waiting for the shell prompt")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for the shell prompt")

    try:
        wait_for("login:", 300)
        send_line(proc, "root", delay)
        wait_for("Password:", 90)
        send_line(proc, "root", delay)
        wait_for("root@akaOS:/root#", 120)
        # The dynamically linked executable emits this marker only after both
        # its libc-backed file I/O and the DT_NEEDED function check succeed.
        prompt = "root@akaOS:/root#"
        prior_prompt_count = content().count(prompt)
        send_line(proc, "dynamic-hello", delay)
        wait_for_line("dynamic-loader-ok", 120)
        wait_for_prompt_count(prior_prompt_count + 1, 120)
        send_line(proc, "cxx-dynamic-test", delay)
        wait_for("C++ shared runtime entered main", 120)
        wait_for("C++ shared libc++ containers verified", 60)
        wait_for("C++ shared libc++ numeric formatting verified", 30)
        wait_for("C++ shared std::bad_alloc and new_handler semantics verified", 30)
        wait_for("C++ shared runtime ELF images and unwind headers verified", 30)
        wait_for("C++ shared runtime entering exception probe", 30)
        wait_for("C++ shared runtime catch body entered", 30)
        wait_for("C++ shared-runtime exception/unwind verified", 120)
        wait_for("C++ shared runtime dynamic-link test passed", 30)
        wait_for_prompt_count(prior_prompt_count + 1, 120)
        # dltest exercises constructors, DT_NEEDED resolution, and the
        # bounded PT_TLS implementation.  Keep this in the same live guest
        # matrix so every architecture is held to the same loader contract.
        send_line(proc, "dltest", delay)
        wait_for("dynamic main-program handle semantics verified", 120)
        wait_for("dynamic shared object RTLD_NOLOAD semantics verified", 120)
        wait_for("dynamic symbol address lookup verified", 120)
        wait_for("dynamic program headers verified", 120)
        # Require the symbol-version probe explicitly.  The TLS marker comes
        # later in dltest, but keeping both waits makes a regression in
        # DT_VERSYM/DT_VERDEF handling fail at the feature that broke.
        wait_for("dynamic shared object symbol versions verified", 120)
        wait_for("dynamic shared object TLS verified", 120)
        wait_for("dynamic shared object TLS reclamation verified", 120)
        wait_for("dynamic shared object static TLS verified", 120)
        wait_for("dynamic shared object dependency fanout verified (5 providers)", 120)
        wait_for("dynamic shared object capacity verified (12 live handles)", 120)
        wait_for("dynamic shared object per-thread dlerror verified", 120)
        wait_for("dynamic shared object concurrent lifecycle verified (4 workers)", 120)
        print(f"[PASS] {name}: native PT_INTERP loader, DSO constructors, dependencies, symbol versions, dynamic/static TLS, and TLS reclamation verified")
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=10)

print("[PASS] Native PT_INTERP loader verified on all requested architectures.")
PY
