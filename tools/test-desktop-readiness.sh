#!/usr/bin/env bash
# Boot the x86_64 UEFI image and verify the native compositor smoke test.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
RUN_LOG="${AKAOS_TEST_RUN_LOG:-/tmp/desktop-readiness-run.log}"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/build.sh" >/dev/null
fi

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.readiness.fd"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"
rm -f "$RUN_LOG"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$RUN_LOG" <<'PY'
import os
import sys
import subprocess
import time

root_dir, ovmf_code, ovmf_vars, run_log = sys.argv[1:5]
boot_wait_iterations = int(os.environ.get("AKAOS_DESKTOP_BOOT_WAIT_ITERATIONS", "1800"))
def to_sendkeys(command):
    result = []
    for ch in command:
        if 'a' <= ch <= 'z':
            result.append(f"sendkey {ch} 10\n")
        elif '0' <= ch <= '9':
            result.append(f"sendkey {ch} 10\n")
        elif ch == ' ':
            result.append("sendkey spc 10\n")
        elif ch == '-':
            result.append("sendkey minus 10\n")
        elif ch == '&':
            result.append("sendkey shift-7 5\n")
        elif ch == ';':
            result.append("sendkey semicolon 10\n")
    result.append("sendkey ret 5\n")
    return result

def send_text(command, repeat_return=True, wait_for_prompt=True, key_delay=0.05):
    if wait_for_prompt:
        # A process-completion marker is written just before the shell regains
        # the terminal; give its waiter and prompt a moment before key injection.
        time.sleep(0.75)
    # Feed HMP one key event at a time.  A resident service may be scheduled
    # between events; flushing each event keeps the shell's canonical line
    # discipline from losing the terminating Return key.
    for key_event in to_sendkeys(command):
        proc.stdin.write(key_event)
        proc.stdin.flush()
        time.sleep(key_delay)
    time.sleep(0.75)

qemu_cmd = [
    # Large UEFI images can stall when QEMU reads them from Windows DrvFS.
    # Callers may point this test at an ext4-cached copy.

    "qemu-system-x86_64", "-machine", "q35",
    "-m", os.environ.get("AKAOS_QEMU_MEM", "1024M"),
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={os.environ.get('AKAOS_TEST_DISK_IMAGE', os.path.join(root_dir, 'build', 'akaos.img'))}",
    "-serial", f"file:{run_log}", "-monitor", "stdio", "-display", "none",
    "-no-reboot"
]
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL, text=True)

def contains(text):
    try:
        with open(run_log, "r", errors="ignore") as stream:
            return text in stream.read()
    except (FileNotFoundError, OSError):
        return False

for _ in range(boot_wait_iterations):
    if contains("[INFO] akaOS shell active"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("guest shell did not become ready")

time.sleep(0.5)
send_text("root", repeat_return=False, wait_for_prompt=False)
for _ in range(200):
    if contains("Password:"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("login prompt did not reach password phase")
send_text("root", repeat_return=False, wait_for_prompt=False)
for _ in range(300):
    if contains("root@akaOS:/root#"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("root shell prompt did not appear")

send_text("hello-pie")

for _ in range(600):
    if contains("[USER] Hello from akaOS C userspace"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("ET_DYN PIE smoke program did not execute")

send_text("hello-pie --pthread-test")
for _ in range(int(os.environ.get("AKAOS_PTHREAD_SMOKE_WAIT_ITERATIONS", "1200"))):
    if contains("pthread compatibility and shared thread verified (attributes, rwlock, timed wait, semaphore, barrier, cleanup, cancellation, 24-worker scheduler scaling, blocking and tick-rounded futex waits)"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("pthread compatibility smoke test did not execute")

send_text("dltest ")
for _ in range(600):
    if contains("dynamic shared object verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("dynamic shared-object smoke test did not execute")

# Exercise the kernel PT_INTERP handoff and the native userspace interpreter,
# not only dlopen from an already-running static process.  The following
# command must return to the shell so the trailing marker can be observed.
send_text("dynamic-hello && echo dynamic-loader-ok ")
for _ in range(600):
    if contains("dynamic-loader-ok"):
        break
else:
    proc.kill()
    raise SystemExit("native dynamic executable did not return")
for _ in range(600):
    if contains("dynamic-loader-ok"):
        break
else:
    proc.kill()
    raise SystemExit("native PT_INTERP dynamic executable did not return")

send_text("ps")
for _ in range(600):
    if contains("ready dbus-daemon") or contains("running dbus-daemon") or contains("dbus-daemon"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("session D-Bus daemon did not remain active")

send_text("hello-pie --dbus-test")
for _ in range(600):
    if contains("D-Bus session bus verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("D-Bus session client smoke test did not execute")

send_text("hello-pie --dbus-system-test")
for _ in range(600):
    if contains("D-Bus system bus verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("D-Bus system client smoke test did not execute")

send_text("hello-pie --dbus-binary-test")
for _ in range(600):
    if contains("D-Bus binary framing verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("binary D-Bus client smoke test did not execute")

send_text("hello-pie --dbus-standard-test")
for _ in range(600):
    if contains("D-Bus standard broker methods verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("standard D-Bus broker method smoke test did not execute")

send_text("hello-pie --dbus-marshaller-test")
for _ in range(600):
    if contains("D-Bus generic signature marshaller verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("generic D-Bus signature marshaller smoke test did not execute")

send_text("hello-pie --desktop-runtime-test")
for _ in range(1200):
    if contains("GLib descriptor source readiness verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("desktop FD source probe did not execute")

for _ in range(1200):
    if contains("D-Bus asynchronous descriptor watch verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("asynchronous D-Bus descriptor probe did not execute")

for _ in range(1200):
    if contains("D-Bus asynchronous binary descriptor watch verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("asynchronous binary D-Bus descriptor probe did not execute")

for _ in range(600):
    if contains("D-Bus asynchronous request queue verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("asynchronous D-Bus request queue probe did not execute")

for _ in range(600):
    if contains("desktop shared runtime ABI verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("desktop shared runtime probe did not execute")

send_text("hello-pie --display-protocol-test")
for marker in ("X11 protocol handshake verified", "Wayland protocol handshake verified"):
    for _ in range(600):
        if contains(marker):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit("display protocol bridge probe did not execute: " + marker)

# Exercise the native compositor's one-shot path independently of any
# upstream desktop profile. Official X11 desktops are tested by profile-specific
# QEMU suites once their complete upstream runtime is staged.
send_text("lumen --once")
for marker in ("lumen framebuffer compositor presented",):
    for _ in range(500):
        if contains(marker):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit("native compositor smoke did not complete: " + marker)

proc.stdin.write("quit\n")
proc.stdin.flush()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

with open(run_log, "r", errors="ignore") as stream:
    transcript = stream.read()
if "lumen framebuffer compositor presented" not in transcript:
    raise SystemExit("missing native Lumen compositor smoke marker")
print("[PASS] native compositor smoke test verified")
PY
