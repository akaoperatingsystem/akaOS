#!/usr/bin/env bash
# Boot the x86_64 UEFI image and verify the real desktop readiness report.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
RUN_LOG="$BUILD_DIR/desktop-readiness-run.log"

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
runtime_wait_iterations = int(
    os.environ.get("AKAOS_DESKTOP_RUNTIME_WAIT_ITERATIONS", "1800")
)
qt_wayland_smoke = os.path.join(root_dir, "build", "QT_WAYLAND_SMOKE.ELF")
qt_quick_wayland_smoke = os.path.join(
    root_dir, "build", "QT_QUICK_WAYLAND_SMOKE.ELF"
)
qt_wayland_smoke_packaged = (
    os.environ.get("AKAOS_TEST_QT_WAYLAND_SMOKE_PACKAGED") == "1"
)
qt_quick_smoke_packaged = (
    os.environ.get("AKAOS_TEST_QT_QUICK_SMOKE_PACKAGED") == "1"
)
if qt_wayland_smoke_packaged and not os.path.isfile(qt_wayland_smoke):
    raise SystemExit("Qt Wayland smoke is marked packaged but its build artifact is missing")
if qt_quick_smoke_packaged and not os.path.isfile(qt_quick_wayland_smoke):
    raise SystemExit("Qt Quick smoke is marked packaged but its build artifact is missing")

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
    "-m", "512M" if (qt_wayland_smoke_packaged or qt_quick_smoke_packaged) else "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={os.environ.get('AKAOS_TEST_DISK_IMAGE', os.path.join(root_dir, 'build', 'akaos.img'))}",
    "-serial", f"file:{run_log}", "-monitor", "stdio", "-display", "none",
    "-no-reboot"
]
proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, text=True)

def contains(text):
    try:
        with open(run_log, "r", errors="ignore") as stream:
            return text in stream.read()
    except FileNotFoundError:
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

if os.environ.get("AKAOS_QT_SMOKE_ONLY") == "1":
    if not os.path.isfile(qt_wayland_smoke):
        proc.kill()
        raise SystemExit("Qt Wayland smoke executable is not built")
    send_text("desktop --env aurora --once; qtwayland-smoke ", key_delay=0.12)
    qt_wait_iterations = int(
        os.environ.get("AKAOS_QT_WAYLAND_WAIT_ITERATIONS", "12000")
    )
    for marker in ("[desktop] Wayland client socket is ready.",
                   "Qt Widgets Wayland QPA painted and exposed a native window"):
        for _ in range(qt_wait_iterations):
            if contains(marker):
                break
            time.sleep(0.05)
        else:
            proc.kill()
            raise SystemExit("Qt Wayland smoke did not complete: " + marker)
    render_errors = (
        "QWaylandShmBuffer: failed:",
        "QWidget::paintEngine:",
        "QPainter::begin: Paint device returned engine == 0",
        "QPainter: Painter not active",
    )
    for render_error in render_errors:
        if contains(render_error):
            proc.kill()
            raise SystemExit("Qt Wayland smoke reported a rendering failure: " +
                             render_error)
    proc.kill()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    print("[PASS] real Qt Widgets Wayland QPA painted a window through Lumen")
    raise SystemExit(0)

if os.environ.get("AKAOS_QT_QUICK_SMOKE_ONLY") == "1":
    if not os.path.isfile(qt_quick_wayland_smoke):
        proc.kill()
        raise SystemExit("Qt Quick Wayland smoke executable is not built")
    send_text("desktop --env aurora --once; qtquick-wayland-smoke ",
              key_delay=0.12)
    qt_quick_wait_iterations = int(
        os.environ.get("AKAOS_QT_QUICK_WAYLAND_WAIT_ITERATIONS", "12000")
    )
    for marker in ("[desktop] Wayland client socket is ready.",
                   "Qt Quick smoke: QGuiApplication initialized",
                   "lumen: Wayland surface presented on framebuffer",
                   "Qt Quick Wayland QPA rendered and exposed a native window"):
        for _ in range(qt_quick_wait_iterations):
            if contains("smoke: command not found"):
                proc.kill()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
                raise SystemExit(
                    "Qt Quick smoke command was not found; see " + run_log
                )
            if contains("Qt Quick Wayland render/exposure failed"):
                proc.kill()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
                raise SystemExit(
                    "Qt Quick Wayland smoke reported a rendering failure; see "
                    + run_log
                )
            if contains(marker):
                break
            time.sleep(0.05)
        else:
            proc.kill()
            raise SystemExit("Qt Quick Wayland smoke did not complete: " + marker)
    proc.kill()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    print("[PASS] real Qt Quick Wayland QPA rendered through Lumen")
    raise SystemExit(0)

send_text("hello --heap-test ")
for marker in ("malloc fragmentation, back-reference free, and aligned allocation verified",
               "posix_memalign alignment and error contract verified",
               "heap allocator self-test passed"):
    for _ in range(400):
        if contains(marker):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit("allocator contract probe did not execute: " + marker)

send_text("hello-pi --posix-test ")
for marker in (
    "POSIX Cairo math/allocation primitives sqrt/fabs/atan2/exp/sin/log2/log10/exp2/cbrt/nearbyint/fmax/fmin/modf/lrint/fenv/posix_memalign verified",
    "POSIX expm1/log1p, hyperbolic/inverse-hyperbolic, remainder/remquo, copysign, and domain semantics verified",
    "POSIX environment and spawn verified",
):
    for _ in range(runtime_wait_iterations):
        if contains(marker):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit("POSIX compatibility probe did not execute: " + marker)
send_text("", wait_for_prompt=True)

send_text("hello-pi ")

for _ in range(600):
    if contains("[USER] Hello from akaOS C userspace"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("ET_DYN PIE smoke program did not execute")

send_text("hello-pi --pthread-test ")
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

send_text("dbus-daemon --session & ")
send_text("ps ")
for _ in range(400):
    if contains("running dbus-daemon"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("session D-Bus daemon did not remain active")

send_text("hello-pi --dbus-test ")
for _ in range(600):
    if contains("D-Bus session bus verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("D-Bus session client smoke test did not execute")

send_text("hello-pi --dbus-system-test ")
for _ in range(600):
    if contains("D-Bus system bus verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("D-Bus system client smoke test did not execute")

send_text("hello-pi --dbus-binary-test ")
for _ in range(600):
    if contains("D-Bus binary framing verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("binary D-Bus client smoke test did not execute")

send_text("hello-pi --dbus-standard-test ")
for _ in range(600):
    if contains("D-Bus standard broker methods verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("standard D-Bus broker method smoke test did not execute")

send_text("hello-pi --dbus-marshaller-test ")
for _ in range(600):
    if contains("D-Bus generic signature marshaller verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("generic D-Bus signature marshaller smoke test did not execute")

send_text("desktop --check ")

for _ in range(400):
    if contains("KDE Plasma (kde): COMPATIBILITY ONLY"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("desktop readiness report did not finish")

send_text("hello-pi --desktop-runtime-test ")
for _ in range(runtime_wait_iterations):
    if contains("GLib descriptor source readiness verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("desktop FD source probe did not execute")

for _ in range(runtime_wait_iterations):
    if contains("D-Bus asynchronous descriptor watch verified"):
        break
    time.sleep(0.05)
else:
    proc.kill()
    raise SystemExit("asynchronous D-Bus descriptor probe did not execute")

for _ in range(runtime_wait_iterations):
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

send_text("hello-pi --display-protocol-test ")
for marker in ("X11 protocol handshake verified", "Wayland protocol handshake verified"):
    for _ in range(600):
        if contains(marker):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit("display protocol bridge probe did not execute: " + marker)

# Exercise the composed display-manager path and direct aliases in one
# canonical input line.  The first desktop invocation starts Lumen, which may
# consume later keyboard events; queuing the whole sequence mirrors the shell's
# normal semicolon execution contract and avoids racing that ownership change.
composed_command = (
    "desktop --env lxde --once; desktop --env lxqt --once; "
    "desktop --env gnome --once; desktop --env kde --once; "
    "startlxde --once; startlxqt --once; gnome-shell --once; plasma --once; "
    "openbox --component & sleep 1; hello-pi --dbus-desktop-service-test; "
    "pkill openbox; desktop --env aurora --once"
)
if qt_wayland_smoke_packaged:
    composed_command += "; qtwayland-smoke"
if qt_quick_smoke_packaged:
    composed_command += "; qtquick-wayland-smoke"
send_text(composed_command + " ")
for env, label in (("lxde", "LXDE"), ("lxqt", "LXQt"),
                   ("gnome", "GNOME"), ("kde", "KDE Plasma")):
    for _ in range(500):
        if contains(label + " session components started"):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit(env + " composed session did not start components")
    for _ in range(500):
        if contains(label + " session components stopped"):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit(env + " composed session did not supervise exits")
for profile, label in (("startlxde", "LXDE"), ("startlxqt", "LXQt"),
                       ("gnome-shell", "GNOME"), ("plasma", "KDE Plasma")):
    for _ in range(500):
        if contains(label + " session running"):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit(profile + " compatibility session did not run")

# The resident component was queued with the composed sequence above because
# the first desktop invocation can take input ownership.  It must publish its
# role activation definition, and SIGTERM must remove it before the shell
# continues.
for marker in ("openbox D-Bus activation service registered",
               "openbox D-Bus well-known name claimed",
               "D-Bus desktop service routing verified",
               "desktop component D-Bus well-known name released",
               "desktop component D-Bus activation service unregistered"):
    for _ in range(500):
        if contains(marker):
            break
        time.sleep(0.05)
    else:
        proc.kill()
        raise SystemExit("resident component D-Bus lifecycle did not complete: " + marker)

if qt_wayland_smoke_packaged:
    # The Wayland QPA probe is last because desktop startup may give keyboard
    # ownership to Lumen; the whole command was queued before that handoff.
    qt_wayland_wait_iterations = int(
        os.environ.get("AKAOS_QT_WAYLAND_WAIT_ITERATIONS", "12000")
    )
    for marker in ("[desktop] Wayland client socket is ready.",
                   "Qt Widgets Wayland QPA painted and exposed a native window"):
        for _ in range(qt_wayland_wait_iterations):
            if contains(marker):
                break
            time.sleep(0.05)
        else:
            proc.kill()
            raise SystemExit("live Qt Wayland smoke did not complete: " + marker)
    render_errors = (
        "QWaylandShmBuffer: failed:",
        "QWidget::paintEngine:",
        "QPainter::begin: Paint device returned engine == 0",
        "QPainter: Painter not active",
    )
    for render_error in render_errors:
        if contains(render_error):
            proc.kill()
            raise SystemExit("live Qt Wayland smoke reported a rendering failure: " +
                             render_error)

if qt_quick_smoke_packaged:
    qt_quick_wait_iterations = int(
        os.environ.get("AKAOS_QT_QUICK_WAYLAND_WAIT_ITERATIONS", "12000")
    )
    for marker in ("[desktop] Wayland client socket is ready.",
                   "Qt Quick Wayland QPA rendered and exposed a native window"):
        for _ in range(qt_quick_wait_iterations):
            if contains(marker):
                break
            time.sleep(0.05)
        else:
            proc.kill()
            raise SystemExit("Qt Quick Wayland smoke did not complete: " + marker)

proc.stdin.write("quit\n")
proc.stdin.flush()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

with open(run_log, "r", errors="ignore") as stream:
    transcript = stream.read()
required = [
    "akaOS desktop runtime readiness",
    "Stock KDE/GNOME/LXDE/LXQt sessions are not installed",
    "[USER] Hello from akaOS C userspace",
    "posix_memalign alignment and error contract verified",
    "POSIX expm1/log1p, hyperbolic/inverse-hyperbolic, remainder/remquo, copysign, and domain semantics verified",
    "POSIX initial and worker pthread stack bounds verified",
    "pthread compatibility and shared thread verified (attributes, rwlock, timed wait, semaphore, barrier, cleanup, cancellation, 24-worker scheduler scaling, blocking and tick-rounded futex waits)",
    "dynamic shared object verified",
    "running dbus-daemon",
    "D-Bus session bus verified",
    "D-Bus system bus verified",
    "D-Bus binary framing verified",
    "Aurora (aurora): READY",
    "LXDE (lxde): COMPATIBILITY ONLY (upstream session not installed)",
    "LXQt (lxqt): COMPATIBILITY ONLY (upstream session not installed)",
    "GNOME (gnome): COMPATIBILITY ONLY (upstream session not installed)",
      "KDE Plasma (kde): COMPATIBILITY ONLY (upstream session not installed)",
      "GLib descriptor source readiness verified",
      "GLib per-context source ownership verified",
      "D-Bus asynchronous descriptor watch verified",
      "D-Bus asynchronous binary descriptor watch verified",
      "D-Bus asynchronous request queue verified",
      "D-Bus asynchronous cancellation lifecycle verified",
      "D-Bus asynchronous reconnect verified",
      "D-Bus bounded retry policy verified",
      "D-Bus asynchronous cancellation and close lifecycle verified",
      "desktop shared runtime ABI verified",
    "X11 protocol handshake verified",
    "Wayland protocol handshake verified",
    "LXDE session running",
    "LXQt session running",
    "GNOME session running",
    "KDE Plasma session running",
    "LXDE session components started",
    "LXQt session components started",
    "GNOME session components started",
    "KDE Plasma session components started",
    "LXDE session components stopped",
    "LXQt session components stopped",
    "GNOME session components stopped",
    "KDE Plasma session components stopped",
]
if qt_wayland_smoke_packaged:
    required.extend([
        "Qt 6 Wayland QPA runtime: INSTALLED",
        "[desktop] Wayland client socket is ready.",
        "Qt Widgets Wayland QPA painted and exposed a native window",
    ])
if qt_quick_smoke_packaged:
    required.extend([
        "[desktop] Wayland client socket is ready.",
        "Qt Quick Wayland QPA rendered and exposed a native window",
    ])
for marker in required:
    if marker not in transcript:
        raise SystemExit(f"missing readiness marker: {marker}")
print("[PASS] live desktop readiness matrix verified in x86_64 guest")
PY
