#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
ISO_FILE="${AKAOS_LXQT_TEST_ISO:-$BUILD_DIR/akaos.iso}"
KERNEL_FILE="${AKAOS_LXQT_TEST_KERNEL:-}"
LOG_FILE="${AKAOS_LXQT_TEST_LOG:-$BUILD_DIR/upstream-lxqt-panel-test.log}"
SCREENSHOT_FILE="${AKAOS_LXQT_TEST_SCREENSHOT:-$BUILD_DIR/upstream-lxqt-panel.ppm}"
TASKBAR_SCREENSHOT_FILE="${AKAOS_LXQT_TASKBAR_SCREENSHOT:-$BUILD_DIR/upstream-lxqt-taskbar.ppm}"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    AKAOS_QT_LOADER_TRACE=1 \
        "$ROOT_DIR/build.sh" --arch x86_64 --firmware both --disk-type iso >/dev/null
    export AKAOS_LXQT_TEST_REQUIRE_PANEL_TRACE=1
fi
if [[ -n "$KERNEL_FILE" && ! -f "$KERNEL_FILE" ]]; then
    echo "Missing LXQt test kernel: $KERNEL_FILE" >&2
    exit 2
fi
if [[ -z "$KERNEL_FILE" && ! -f "$ISO_FILE" ]]; then
    echo "Missing LXQt test ISO: $ISO_FILE; build the x86_64 dual-firmware image first" >&2
    exit 2
fi

python3 - "$ROOT_DIR" "$LOG_FILE" "$SCREENSHOT_FILE" "$TASKBAR_SCREENSHOT_FILE" <<'PY'
import os
import subprocess
import sys
import time
from pathlib import Path

root = Path(sys.argv[1])
log_file = Path(sys.argv[2])
screenshot = Path(sys.argv[3])
taskbar_screenshot = Path(sys.argv[4])
log_file.parent.mkdir(parents=True, exist_ok=True)
screenshot.parent.mkdir(parents=True, exist_ok=True)
taskbar_screenshot.parent.mkdir(parents=True, exist_ok=True)
log_file.unlink(missing_ok=True)
screenshot.unlink(missing_ok=True)
taskbar_screenshot.unlink(missing_ok=True)

iso = str(Path(os.environ.get("AKAOS_LXQT_TEST_ISO", root / "build" / "akaos.iso")))
# Treat media bytes as raw data and escape commas in QEMU option values.
iso = iso.replace(",", ",,")
kernel = os.environ.get("AKAOS_LXQT_TEST_KERNEL")
boot_media = (["-kernel", kernel] if kernel else [
    "-drive", "media=cdrom,index=2,format=raw,readonly=on,file.driver=file,"
    f"file.filename={iso}",
    "-boot", "d",
])
qemu = subprocess.Popen(
    [
        "qemu-system-x86_64", "-machine", "q35", "-m", "512M",
        *boot_media,
        "-vga", "std", "-serial", f"file:{log_file}", "-monitor", "stdio",
        "-display", "none", "-no-reboot",
    ],
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    text=True,
)

def content():
    try:
        return log_file.read_text(errors="ignore")
    except FileNotFoundError:
        return ""

def wait_for(marker, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data = content()
        if marker in data:
            return data
        if "[FATAL]" in data or "CPU exception vector" in data:
            raise RuntimeError(f"guest fault while waiting for {marker!r}:\n{data[-5000:]}")
        if qemu.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for {marker!r}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {marker!r}:\n{content()[-5000:]}")

def wait_for_after(marker, offset, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data = content()
        if marker in data[offset:]:
            return data
        if "[FATAL]" in data or "CPU exception vector" in data:
            raise RuntimeError(f"guest fault while waiting for {marker!r}:\n{data[-5000:]}")
        if qemu.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for {marker!r}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {marker!r}:\n{content()[-5000:]}")

key_names = {
    " ": "spc", "/": "slash", ".": "dot", "-": "minus",
    "_": "shift-minus", "&": "shift-7", ";": "semicolon",
    "=": "equal", "$": "shift-4", "?": "shift-slash",
}

def send_line(line):
    if qemu.stdin is None:
        raise RuntimeError("QEMU monitor closed")
    for char in line:
        if "a" <= char <= "z" or "0" <= char <= "9":
            key = char
        elif "A" <= char <= "Z":
            key = "shift-" + char.lower()
        elif char in key_names:
            key = key_names[char]
        else:
            raise ValueError(f"test command contains unsupported key {char!r}")
        qemu.stdin.write(f"sendkey {key} 10\n")
        qemu.stdin.flush()
        time.sleep(0.12)
    qemu.stdin.write("sendkey ret 5\n")
    qemu.stdin.flush()
    time.sleep(1.0)

def stop_qemu():
    qemu.terminate()
    try:
        qemu.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu.kill()
        qemu.wait(timeout=5)

def capture_screenshot(target=screenshot):
    if qemu.stdin is None or qemu.poll() is not None:
        return
    qemu.stdin.write(f"screendump {target}\n")
    qemu.stdin.flush()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and not target.is_file():
        time.sleep(0.1)

try:
    panel_wait_seconds = int(
        os.environ.get("AKAOS_LXQT_PANEL_WAIT_SECONDS", "180")
    )
    wait_for("login:", int(os.environ.get("AKAOS_LXQT_BOOT_WAIT_SECONDS", "120")))
    send_line("root")
    wait_for("Password:", 20)
    send_line("root")
    wait_for("root@akaOS:/root#", 30)

    command_start = len(content())
    send_line("env")
    wait_for_after("root@akaOS:/root#", command_start, 15)

    command_start = len(content())
    send_line("/bin/dbus-update-activation-environment --help")
    wait_for_after("Add specified variables to D-Bus activation environment.",
                   command_start, 30)
    wait_for_after("root@akaOS:/root#", command_start, 30)

    command_start = len(content())
    send_line("dbus-daemon --session &")
    wait_for_after("root@akaOS:/root#", command_start, 30)
    command_start = len(content())
    send_line("sleep 2")
    wait_for_after("root@akaOS:/root#", command_start, 10)
    command_start = len(content())
    send_line("/bin/dbus-update-activation-environment --all; echo DBUS_UPDATE_STATUS=$?")
    dbus_output = wait_for_after(
        "DBUS_UPDATE_STATUS=0", command_start, 30
    )
    if "[dbus] UpdateActivationEnvironment bytes=" not in dbus_output or \
            "valid=1" not in dbus_output or \
            "UpdateActivationEnvironment reply failed" in dbus_output:
        raise RuntimeError("D-Bus broker rejected the activation environment")
    marker_position = dbus_output.rfind("DBUS_UPDATE_STATUS=0")
    wait_for_after("root@akaOS:/root#", marker_position + 1, 30)

    command_start = len(content())
    send_line("hello --posix-test")
    posix_output = wait_for_after(
        "POSIX SIGCHLD child-exit notification verified", command_start, 300
    )
    marker_position = posix_output.rfind(
        "POSIX SIGCHLD child-exit notification verified"
    )
    wait_for_after("root@akaOS:/root#", marker_position + 1, 30)

    command_start = len(content())
    send_line("lumen --self-test")
    lumen_output = wait_for_after(
        "Wayland premultiplied ARGB pixel composition verified",
        command_start, 30
    )
    marker_position = lumen_output.rfind(
        "Wayland premultiplied ARGB pixel composition verified"
    )
    wait_for_after("root@akaOS:/root#", marker_position + 1, 30)

    send_line("wayland-upstream-test --foreign-toplevel-smoke &")
    send_line("env QT_LOGGING_RULES=lxqt-session.debug=true desktop --env lxqt --once &")
    initial_process_offset = len(content())
    send_line("ps")
    time.sleep(2.0)
    initial_process_table = content()[initial_process_offset:]
    try:
        wait_for("[desktop] Starting Lumen window compositor...", 60)
        wait_for("lumen: framebuffer compositor active", 30)
        wait_for("[desktop] Starting lxqt desktop environment...", 30)
    except RuntimeError as error:
        process_start = len(content())
        process_detail = initial_process_table[-4000:]
        try:
            send_line("ps")
            data = wait_for_after("root@akaOS:/root#", process_start, 20)
            process_detail += data[process_start:][-4000:]
        except RuntimeError as process_error:
            process_detail = f"Could not capture guest process table: {process_error}"
        raise RuntimeError(f"{error}\nGuest process table:\n{process_detail}") from error
    wait_for("[desktop] akaOS Desktop Environment (lxqt) is running.",
             panel_wait_seconds)
    dbus_output = wait_for("[dbus] UpdateActivationEnvironment bytes=",
                           panel_wait_seconds)
    dbus_update = dbus_output.rsplit(
        "[dbus] UpdateActivationEnvironment bytes=", 1
    )[-1]
    if "valid=1" not in dbus_update or \
            "UpdateActivationEnvironment reply failed" in dbus_update:
        raise RuntimeError(f"D-Bus activation environment update was rejected: {dbus_update[:160]}")
    if "dbus-updatee-activation-environment failed:" in content():
        raise RuntimeError("LXQt's D-Bus activation environment helper failed")
    wait_for("[dbus] UpdateActivationEnvironment bytes=", panel_wait_seconds)
    if "dbus-updatee-activation-environment failed:" in content():
        raise RuntimeError("LXQt could not update its D-Bus activation environment")
    wait_for("wayland-bridge: layer-shell configure", panel_wait_seconds)
    wait_for("wayland-bridge: layer-shell mapped", panel_wait_seconds)
    wait_for("lumen: layer-shell mapped", panel_wait_seconds)
    if os.environ.get("AKAOS_LXQT_TEST_REQUIRE_PANEL_TRACE") == "1":
        wait_for("[ld] ready: /bin/lxqt-panel", panel_wait_seconds)
    manager_marker = "wayland-bridge: foreign-toplevel manager bound"
    first_manager_offset = content().find(manager_marker)
    if first_manager_offset < 0:
        raise RuntimeError("no Wayland foreign-toplevel manager client bound")
    wait_for_after(manager_marker,
                   first_manager_offset + len(manager_marker),
                   panel_wait_seconds)
    wait_for("lumen: Wayland surface presented on framebuffer",
             panel_wait_seconds)
    wait_for("wayland-foreign-toplevel-metadata-verified", panel_wait_seconds)
    wait_for("wayland-foreign-toplevel-activate-focus-verified",
             panel_wait_seconds)
    capture_screenshot(taskbar_screenshot)
    if not taskbar_screenshot.is_file() or not taskbar_screenshot.read_bytes().startswith(b"P6"):
        raise RuntimeError("QEMU did not capture the taskbar with a foreign toplevel open")
    wait_for("wayland-bridge: foreign-toplevel close requested",
             panel_wait_seconds)
    wait_for("wayland-xdg-toplevel-close-event-received", panel_wait_seconds)
    wait_for("wayland-foreign-toplevel-lifecycle-and-actions-verified",
             panel_wait_seconds)
    if "Falling back to dummy backend" in content():
        raise RuntimeError("LXQt could not initialize its Wayland window-management backend")

    capture_screenshot()
    if not screenshot.is_file() or screenshot.stat().st_size == 0:
        raise RuntimeError("QEMU did not produce a non-empty LXQt session screenshot")
    if not screenshot.read_bytes().startswith(b"P6"):
        raise RuntimeError("QEMU screenshot is not a binary PPM image")

    print("[PASS] upstream LXQt session and panel stayed live on the x86_64 Wayland image")
    print(f"[IMAGE] {screenshot}")
    print(f"[TASKBAR] {taskbar_screenshot}")
finally:
    if not screenshot.is_file():
        capture_screenshot()
    stop_qemu()
PY
