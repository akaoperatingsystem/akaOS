#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${AKAOS_TEST_IMAGE:-$ROOT_DIR/build/akaos.iso}"
INITRAMFS="$ROOT_DIR/build/initramfs.img"

if [[ ! -f "$IMAGE" || ! -f "$INITRAMFS" ]]; then
    echo "Build the x86_64 ISO and initramfs first." >&2
    exit 2
fi
if [[ "${AKAOS_TEST_GTK3_ONLY:-0}" != "1" ]] && {
    [[ ! -f "$ROOT_DIR/build/GTK_UPSTREAM_TEST.ELF" ||
       ! -f "$ROOT_DIR/build/XKBCOMMON_UPSTREAM_TEST.ELF" ]] ||
    ! grep -a -q '/lib/.upstream-gtk/libgtk-4.so.1' "$INITRAMFS" ||
    ! grep -a -q '/lib/libcairo-script-interpreter.so.2' "$INITRAMFS" ||
    ! grep -a -q '/bin/wayland-bridge' "$INITRAMFS" ||
    ! grep -a -q '/usr/share/X11/xkb/rules/evdev' "$INITRAMFS" ||
    ! grep -a -q '/bin/gtk-upstream-test' "$INITRAMFS" ||
    ! grep -a -q '/bin/xkbcommon-upstream-test' "$INITRAMFS"
}; then
    echo "Build the x86_64 ISO with upstream GTK 4 and xkbcommon artifacts first." >&2
    exit 2
fi
if [[ "${AKAOS_TEST_GTK3:-0}" == "1" ]] && {
    [[ ! -f "$ROOT_DIR/build/GTK3_UPSTREAM_TEST.ELF" ]] ||
    ! grep -a -q '/lib/.upstream-gtk3/libgtk-3.so.0' "$INITRAMFS" ||
    ! grep -a -q '/bin/gtk3-upstream-test' "$INITRAMFS"
}; then
    echo "Build the x86_64 image with the upstream GTK 3 Wayland runtime first." >&2
    exit 2
fi

python3 - "$ROOT_DIR" "$IMAGE" <<'PY'
import subprocess
import os
import sys
import time
from pathlib import Path

root = Path(sys.argv[1])
image = Path(sys.argv[2])
log = root / "build" / "test-upstream-gtk4.log"
qemu_error_log = root / "build" / "test-upstream-gtk4.qemu.log"
log.unlink(missing_ok=True)
qemu_error_log.unlink(missing_ok=True)
storage = root / "build" / f"upstream-gtk-test-storage-{os.getpid()}.img"
with storage.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")
qemu_error_stream = qemu_error_log.open("w")
qemu_memory = os.environ.get("AKAOS_TEST_MEMORY", "768M")
qemu_command = [
    "qemu-system-x86_64", "-machine", "q35", "-cpu", "max", "-m", qemu_memory,
    "-cdrom", str(image), "-boot", "d", "-vga", "std",
    "-drive", f"if=none,format=raw,file={storage},id=akaos-storage",
    "-device", "virtio-blk-pci,drive=akaos-storage",
    "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
    "-no-reboot",
]
if os.access("/dev/kvm", os.R_OK | os.W_OK):
    qemu_command[1:1] = ["-accel", "kvm"]
    print("[INFO] GTK smoke QEMU acceleration: KVM", flush=True)
else:
    print("[INFO] GTK smoke QEMU acceleration: TCG (KVM not accessible)",
          flush=True)
proc = subprocess.Popen(
    qemu_command,
    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=qemu_error_stream,
    text=True,
)

def content():
    return log.read_text(errors="ignore") if log.exists() else ""

def wait_for(marker, timeout=120, completed_prompt_count=None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        output = content()
        if "[FATAL]" in output:
            time.sleep(0.1)
            output = content()
            raise RuntimeError(f"guest raised a fatal exception while waiting for {marker!r}; guest output:\n{output[-6000:]}")
        if marker in output:
            return
        if (completed_prompt_count is not None and
                output.count("root@akaOS:/root#") >= completed_prompt_count):
            raise RuntimeError(f"GTK smoke command returned without {marker!r}; guest output:\n{output[-6000:]}")
        if proc.poll() is not None:
            qemu_error_stream.flush()
            qemu_errors = qemu_error_log.read_text(errors="ignore")
            raise RuntimeError(
                f"QEMU exited while waiting for {marker!r}; stderr:\n{qemu_errors[-3000:]}"
            )
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {marker!r}; guest output:\n{content()[-6000:]}")

def wait_for_count(marker, minimum, timeout=45):
    deadline = time.time() + timeout
    while time.time() < deadline:
        output = content()
        if "[FATAL]" in output:
            time.sleep(0.1)
            output = content()
            raise RuntimeError(f"guest raised a fatal exception while waiting for another {marker!r}; guest output:\n{output[-6000:]}")
        if output.count(marker) >= minimum:
            return
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for another {marker!r}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {minimum} occurrences of {marker!r}")

def wait_for_logged(marker, timeout=45, completed_prompt_count=None):
    if marker in content():
        return
    wait_for(marker, timeout, completed_prompt_count)

def key_for(ch):
    if "A" <= ch <= "Z":
        return "shift-" + ch.lower()
    return {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
            "_": "shift-minus", "&": "shift-7", "=": "equal",
            ":": "shift-semicolon", ";": "semicolon"}.get(ch, ch)

def send_line(text):
    for ch in text:
        proc.stdin.write(f"sendkey {key_for(ch)} 5\n")
        proc.stdin.flush()
        time.sleep(0.08)
    proc.stdin.write("sendkey ret 5\n")
    proc.stdin.flush()
    time.sleep(0.3)

def send_monitor_command(text):
    proc.stdin.write(text + "\n")
    proc.stdin.flush()
    time.sleep(0.2)

def wait_prompt(min_count=1, timeout=45):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if content().count("root@akaOS:/root#") >= min_count:
            return
        if proc.poll() is not None:
            raise RuntimeError("QEMU exited while waiting for the root prompt")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for the root prompt; guest output:\n{content()[-6000:]}")

try:
    subsurface_only = os.environ.get("AKAOS_TEST_SUBSURFACE_ONLY") == "1"
    gtk3_only = os.environ.get("AKAOS_TEST_GTK3_ONLY") == "1"
    skip_gtk_preflight = os.environ.get("AKAOS_TEST_SKIP_GTK_PREFLIGHT") == "1"
    gtk3_startup_timeout = int(
        os.environ.get("AKAOS_TEST_GTK3_STARTUP_TIMEOUT", "120"))
    test_prompt_count = 2 if subsurface_only or skip_gtk_preflight or gtk3_only else 5
    wait_for("login:", 300)
    send_line("root")
    wait_for("Password:", 90)
    send_line("root")
    wait_prompt()
    if not subsurface_only and not skip_gtk_preflight and not gtk3_only:
        send_line("xkbcommon-upstream-test")
        wait_for("upstream-xkbcommon-smoke-ok", 180, 2)
        wait_prompt(2)
        gtk_types_command = "gtk-upstream-test --types-only"
        if os.environ.get("AKAOS_TEST_GTK_TRACE") == "1":
            gtk_types_command = "env AKAOS_GTK_TRACE=1 " + gtk_types_command
        send_line(gtk_types_command)
        gtk_type_timeout = int(
            os.environ.get("AKAOS_TEST_GTK_TYPE_TIMEOUT", "90"))
        wait_for("upstream-gtk-types-only-ok", gtk_type_timeout, 3)
        wait_prompt(3)
        send_line("gtk-upstream-test --environment-test")
        gtk_environment_timeout = int(
            os.environ.get("AKAOS_TEST_GTK_ENV_TIMEOUT", "90"))
        wait_for("upstream-gtk-environment-mutation-ok",
                 gtk_environment_timeout, 4)
        wait_prompt(4)
    # Lumen consumes /dev/input, so preload the entire line before it starts.
    # Leave enough startup time for the bridge to publish wayland-0 before the
    # client connects on slower guests.
    run_command = "lumen --input-test & sleep 1; env AKAOS_XKB_DEBUG=1 wayland-bridge & sleep 3; "
    if not subsurface_only:
        if not gtk3_only:
            gtk_command = "gtk-upstream-test --external-wayland-smoke"
            gtk_env = []
            if os.environ.get("AKAOS_TEST_GTK_TRACE") == "1":
                gtk_env.append("AKAOS_GTK_TRACE=1")
            if os.environ.get("AKAOS_TEST_WAYLAND_DEBUG") == "1":
                gtk_env.append("WAYLAND_DEBUG=1")
            gtk_theme = os.environ.get("AKAOS_TEST_GTK_THEME")
            if gtk_theme:
                gtk_env.append("GTK_THEME=" + gtk_theme)
            if gtk_env:
                gtk_command = "env " + " ".join(gtk_env) + " " + gtk_command
            run_command += gtk_command + "; "
        if os.environ.get("AKAOS_TEST_GTK3") == "1":
            run_command += "echo gtk3-upstream-starting; gtk3-upstream-test; "
    run_command += "wayland-upstream-test --external-subsurface-smoke"
    gtk3_prior_frame_count = content().count(
        "lumen: Wayland surface presented on framebuffer")
    send_line(run_command)
    if not subsurface_only and gtk3_only:
        wait_for("gtk3-upstream-starting", 15)
        wait_for("gtk3-wayland-widget-layout-verified", gtk3_startup_timeout)
        wait_for("gtk3-wayland-window-ready-for-input", gtk3_startup_timeout)
        wait_for_count("lumen: Wayland surface presented on framebuffer",
                       gtk3_prior_frame_count + 1, 45)
        send_monitor_command("sendkey ret 50")
        wait_for("gtk3-wayland-button-clicked", 45)
        wait_for("gtk3-wayland-client-exit", 45)
    elif not subsurface_only:
        gtk_ready_timeout = int(
            os.environ.get("AKAOS_TEST_GTK_READY_TIMEOUT", "300"))
        wait_for("upstream-gtk-wayland-window-ready-for-input",
                 gtk_ready_timeout)
        wait_for("upstream-gtk-wayland-widget-layout-verified", 45)
        wait_for("lumen: Wayland surface presented on framebuffer", 90)
        send_monitor_command("sendkey a 50")
        wait_for("upstream-gtk-wayland-key-pressed", 45)
        wait_for("lumen: Wayland cursor surface presented on framebuffer", 45)
        send_monitor_command("mouse_button 1")
        send_monitor_command("mouse_button 0")
        wait_for("upstream-gtk-wayland-button-clicked", 45)
        wait_for("upstream-gtk-wayland-popup-mapped", 45)
        wait_for("lumen: Wayland popup surface presented on framebuffer", 45)
        send_monitor_command("sendkey esc 50")
        wait_for("upstream-gtk-wayland-popup-closed-after-escape", 45, 5)
        wait_for("upstream-gtk-wayland-popup-unmapped-after-escape", 45, 5)
        wait_for("upstream-gtk-wayland-popup-ready-for-reopen", 45, 5)
        send_monitor_command("sendkey b 50")
        wait_for("upstream-gtk-wayland-key-after-popup-dismissed", 45, 5)
        send_monitor_command("mouse_button 1")
        send_monitor_command("mouse_button 0")
        wait_for("upstream-gtk-wayland-popup-reopened", 45, 5)
        for _ in range(7):
            send_monitor_command("mouse_move 15 15 0")
        send_monitor_command("mouse_move 15 1 0")
        send_monitor_command("mouse_move 10 1 0")
        send_monitor_command("mouse_button 1")
        send_monitor_command("mouse_button 0")
        wait_for("upstream-gtk-wayland-popup-closed-after-outside-click", 45, 5)
        wait_for("upstream-gtk-wayland-popup-unmapped-after-outside-click", 45, 5)
        for _ in range(7):
            send_monitor_command("mouse_move -15 -15 0")
        send_monitor_command("mouse_move -15 -2 0")
        send_monitor_command("mouse_move -10 0 0")
        send_monitor_command("sendkey c 50")
        wait_for("upstream-gtk-wayland-key-after-outside-dismissal", 45, 5)
        wait_for("upstream-gtk-lumen-client-exit", 45)
        if os.environ.get("AKAOS_TEST_GTK3") == "1":
            wait_for("gtk3-upstream-starting", 15)
            prior_frame_count = content().count(
                "lumen: Wayland surface presented on framebuffer")
            wait_for("gtk3-wayland-widget-layout-verified", 45)
            wait_for("gtk3-wayland-window-ready-for-input", 45)
            wait_for_count("lumen: Wayland surface presented on framebuffer",
                           prior_frame_count + 1, 45)
            send_monitor_command("sendkey ret 50")
            wait_for("gtk3-wayland-button-clicked", 45)
            wait_for("gtk3-wayland-client-exit", 45)
    wait_for("wayland-subsurface-child-commits-staged", 45, test_prompt_count)
    time.sleep(0.2)
    if "lumen: Wayland subsurface tree presented on framebuffer" in content():
        raise RuntimeError("synchronized sub-surface committed before its parent")
    wait_for_logged("wayland-subsurface-parent-commit-complete", 45, test_prompt_count)
    wait_for_logged("lumen: Wayland subsurface tree presented on framebuffer", 45, test_prompt_count)
    wait_for_logged("wayland-subsurface-nested-tree-verified", 45, test_prompt_count)
    wait_for_logged("lumen: Wayland nested subsurface tree presented on framebuffer", 45, test_prompt_count)
    wait_for_logged("wayland-subsurface-sibling-z-order-verified", 45, test_prompt_count)
    wait_for("wayland-subsurface-parent-ready-for-drag", 45, test_prompt_count)
    for _ in range(13):
        send_monitor_command("mouse_move 0 -10 0")
    send_monitor_command("mouse_button 1")
    wait_for("lumen: titlebar drag started at", 45, 5)
    for _ in range(6):
        send_monitor_command("mouse_move 5 5 0")
    send_monitor_command("mouse_button 0")
    wait_for("lumen: titlebar drag moved window to", 45, 5)
    wait_for_logged("wayland-subsurface-parent-move-propagated", 45, 5)
    wait_for("wayland-subsurface-parent-ready-for-move-request", 45,
             test_prompt_count)
    for _ in range(13):
        send_monitor_command("mouse_move 0 10 0")
    send_monitor_command("mouse_button 1")
    wait_for("wayland-xdg-toplevel-move-request-sent", 45,
             test_prompt_count)
    for _ in range(6):
        send_monitor_command("mouse_move 5 5 0")
    send_monitor_command("mouse_button 0")
    wait_for("wayland-xdg-toplevel-move-propagated", 45,
             test_prompt_count)
    wait_for("wayland-subsurface-parent-ready-for-max-resize", 45,
             test_prompt_count)
    send_monitor_command("mouse_button 1")
    wait_for("wayland-xdg-toplevel-resize-request-1-sent", 45,
             test_prompt_count)
    send_monitor_command("mouse_move 15 0 0")
    wait_for("wayland-toplevel-resize-max-applied", 45, test_prompt_count)
    send_monitor_command("mouse_button 0")
    wait_for("wayland-subsurface-parent-ready-for-min-resize", 45,
             test_prompt_count)
    send_monitor_command("mouse_button 1")
    wait_for("wayland-xdg-toplevel-resize-request-2-sent", 45,
             test_prompt_count)
    send_monitor_command("mouse_move -25 0 0")
    wait_for("wayland-toplevel-resize-min-applied", 45, test_prompt_count)
    send_monitor_command("mouse_button 0")
    wait_for("wayland-toplevel-maximized-state-applied", 45,
             test_prompt_count)
    wait_for("wayland-toplevel-fullscreen-state-applied", 45,
             test_prompt_count)
    wait_for("wayland-toplevel-windowed-state-restored", 45,
             test_prompt_count)
    wait_for("wayland-toplevel-minimized-state-applied", 45,
             test_prompt_count)
    wait_prompt(test_prompt_count)
    if not subsurface_only and not skip_gtk_preflight and not gtk3_only:
        print("[PASS] Upstream xkbcommon 1.13.2 parsed a keymap and translated shifted keys in x86_64 akaOS.")
        print("[PASS] Upstream GTK 4.22.5 loaded and registered widget GTypes in x86_64 akaOS.")
    if not subsurface_only:
        print("[PASS] GTK Wayland keyboard/pointer, Escape/outside-click popup dismissal, and focus restoration passed through Lumen.")
        if os.environ.get("AKAOS_TEST_GTK3") == "1":
            print("[PASS] Upstream GTK 3.24.52 mapped a GtkBox window through the akaOS Wayland bridge and handled keyboard activation.")
    print("[PASS] Upstream xdg_toplevel.move requests move the parent and subsurface tree.")
    print("[PASS] Upstream xdg_toplevel.resize honors min/max size hints and updates the SHM slot.")
    print("[PASS] Upstream xdg_toplevel maximize/fullscreen/restore/minimize states reach Lumen.")
    print("[PASS] Synchronized child commits, sibling z-order, and parent movement passed on x86_64.")
finally:
    if proc.poll() is None:
        proc.kill()
        proc.wait(timeout=10)
    qemu_error_stream.flush()
    qemu_error_stream.close()
    storage.unlink(missing_ok=True)
PY
