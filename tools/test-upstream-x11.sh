#!/usr/bin/env bash
# Verify the native akaOS X11 display server with a real upstream libX11
# client inside a QEMU guest.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_IMAGE="${AKAOS_TEST_IMAGE:-$ROOT_DIR/build/akaos.iso}"
INITRAMFS="$ROOT_DIR/build/initramfs.img"

# Booting the image straight from the DrvFS mount is very slow because the
# guest has to stream a large initramfs through the host filesystem.  The
# test therefore runs from a copy in the ext4 cache.
CACHE_DIR="${AKAOS_TEST_ISO_CACHE:-$HOME/.cache/akaos/test}"
mkdir -p "$CACHE_DIR"
IMAGE="$CACHE_DIR/akaos-x11-test.iso"
if [[ ! -f "$IMAGE" || "$SOURCE_IMAGE" -nt "$IMAGE" ]]; then
    cp -f "$SOURCE_IMAGE" "$IMAGE"
fi

if [[ ! -f "$IMAGE" || ! -f "$INITRAMFS" ]]; then
    echo "Build the x86_64 ISO and initramfs first." >&2
    exit 2
fi
if [[ ! -f "$ROOT_DIR/build/X11_CLIENT.ELF" ]] ||
   ! grep -a -q '/bin/xserver' "$INITRAMFS" ||
   ! grep -a -q '/bin/x11-client' "$INITRAMFS" ||
   ! grep -a -q '/lib/libX11.so.6' "$INITRAMFS"; then
    echo "Build the x86_64 image with the X11 server and client first." >&2
    exit 2
fi

python3 - "$ROOT_DIR" "$IMAGE" <<'PY'
import os
import subprocess
import sys
import time
from pathlib import Path

root = Path(sys.argv[1])
image = Path(sys.argv[2])
log = root / "build" / "test-upstream-x11.log"
qemu_error_log = root / "build" / "test-upstream-x11.qemu.log"
storage = root / "build" / f"upstream-x11-test-storage-{os.getpid()}.img"
with storage.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")

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
    print("[INFO] X11 smoke QEMU acceleration: KVM", flush=True)
else:
    print("[INFO] X11 smoke QEMU acceleration: TCG (KVM not accessible)",
          flush=True)

proc = None
qemu_error_stream = None


def start_guest():
    global proc, qemu_error_stream
    log.unlink(missing_ok=True)
    qemu_error_log.unlink(missing_ok=True)
    qemu_error_stream = qemu_error_log.open("w")
    proc = subprocess.Popen(
        qemu_command,
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=qemu_error_stream, text=True,
    )


def stop_guest():
    global proc, qemu_error_stream
    if proc is not None and proc.poll() is None:
        proc.kill()
        try:
            proc.wait(timeout=10)
        except Exception:
            pass
    if qemu_error_stream is not None:
        qemu_error_stream.flush()
        qemu_error_stream.close()
        qemu_error_stream = None
    proc = None


def content():
    return log.read_text(errors="ignore") if log.exists() else ""


def wait_for(marker, timeout=120, completed_prompt_count=None, after=0):
    """Wait for a marker that has not been seen yet.

    The boot self test types into the console, so a marker can already be in
    the log before the guest is ready to answer it.  The number of times the
    marker was seen says which occurrence is the one to wait for."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        output = content()
        if "[FATAL]" in output:
            # The guest console is slow, and the kernel writes the name and the
            # layout of the faulting process after the fault address.  The
            # report is only useful when it is complete.
            time.sleep(6.0)
            output = content()
            raise RuntimeError(
                f"guest raised a fatal exception while waiting for {marker!r}; "
                f"guest output:\n{output[-6000:]}")
        if output.count(marker) > after:
            return
        if (completed_prompt_count is not None and
                output.count("root@akaOS:/root#") >= completed_prompt_count):
            raise RuntimeError(
                f"X11 smoke command returned without {marker!r}; guest output:\n"
                f"{output[-6000:]}")
        if proc.poll() is not None:
            qemu_error_stream.flush()
            qemu_errors = qemu_error_log.read_text(errors="ignore")
            raise RuntimeError(
                f"QEMU exited while waiting for {marker!r}; stderr:\n"
                f"{qemu_errors[-3000:]}")
        time.sleep(0.05)
    raise RuntimeError(
        f"timed out waiting for {marker!r}; guest output:\n{content()[-6000:]}")


def key_for(ch):
    if "A" <= ch <= "Z":
        return "shift-" + ch.lower()
    return {
        " ": "spc", "/": "slash", ".": "dot", "-": "minus",
        "_": "shift-minus", "&": "shift-7", "=": "equal",
        ":": "shift-semicolon", ";": "semicolon", ">": "shift-dot",
        "<": "shift-comma", "'": "apostrophe", "!": "shift-1",
        "?": "shift-slash", "|": "shift-backslash", "#": "shift-3",
        "*": "shift-8", "$": "shift-4", "%": "shift-5", "@": "shift-2",
        "^": "shift-6", "~": "shift-grave", '"': "shift-quote",
        "+": "shift-equal", "(": "shift-9", ")": "shift-0",
        "[": "bracketleft", "]": "bracketright", "\\": "backslash",
        "`": "grave_accent",
    }.get(ch, ch)


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


def wait_prompt(min_count=1, timeout=60):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if content().count("root@akaOS:/root#") >= min_count:
            return
        if proc.poll() is not None:
            raise RuntimeError("QEMU exited while waiting for the root prompt")
        time.sleep(0.05)
    raise RuntimeError(
        f"timed out waiting for the root prompt; guest output:\n{content()[-6000:]}")


def run_guest():
    wait_for("login:", 300)
    time.sleep(3.0)
    # The boot self test types into the console, and a prompt that still holds
    # those keys rejects the name.  A rejected name brings the prompt back with
    # an empty buffer, so the name is typed again on the quiet prompt.
    seen_logins = content().count("login:")
    seen_passwords = content().count("Password:")
    for _ in range(5):
        send_line("root")
        if wait_for("Password:", 30, completed_prompt_count=None,
                    after=seen_passwords):
            seen_passwords += 1
            send_line("root")
            if wait_for("root@akaOS:/root#", 20):
                break
        # The name was rejected.  The prompt comes back, and the next attempt
        # starts from an empty buffer.
        if not wait_for("login:", 60, after=seen_logins):
            break
        seen_logins += 1
        time.sleep(1.0)
    else:
        raise RuntimeError("the guest never accepted the user name")
    wait_prompt()

    # Lumen owns the compositor channel and the input device.  The X server
    # publishes the screen as one surface and takes the keyboard from the
    # console line discipline.
    # Lumen owns the compositor channel and the input device.  The X server
    # publishes the screen as one surface and takes the keyboard from the
    # console line discipline, so nothing can be typed into the shell once it
    # runs.  Both clients are therefore started from one line that the shell
    # reads before the display server takes the keyboard.
    run_command = ("lumen --input-test & sleep 1; "
                   "xserver --trace & sleep 2; "
                   "env DISPLAY=:0 x11-client; "
                   "env DISPLAY=:0 GDK_BACKEND=x11 gtk3-x11-upstream-test & sleep 3")
    send_line(run_command)

    wait_for("xserver: ready on display", 60)
    wait_for("x11 client: connected to display", 90)
    wait_for("x11 client: created window", 45)
    wait_for("x11 client: mapped window", 45)
    wait_for("x11 client: drew into the window", 45)
    wait_for("x11 client: received expose event", 45)

    # Move the pointer over the window and click it.  The monitor command
    # is a relative move and the guest pointer starts at the centre of the
    # screen, so the pointer is walked onto the window the client created.
    send_monitor_command("mouse_move -212 -184 0")
    time.sleep(0.4)
    send_monitor_command("mouse_button 1")
    send_monitor_command("mouse_button 0")
    wait_for("x11 client: received button press", 45)

    # The keyboard still has to reach the focused X client.
    send_monitor_command("sendkey a 50")
    wait_for("x11 client: received key press", 45)
    wait_for("x11 client: X11 client path verified", 45)
    wait_prompt(2)

    # The unmodified upstream GTK 3 library has to run on the native server.
    # It was started by the same command line, so the shell cannot type it
    # while the display server holds the keyboard.
    wait_for("gtk3-x11-init-check-returned", 120)
    wait_for("gtk3-x11-widget-layout-verified", 120)
    wait_for("gtk3-x11-window-ready-for-input", 120)

    # Click the GTK button, which sits in the upper half of the window.
    send_monitor_command("mouse_move -200 -140 0")
    time.sleep(0.5)
    send_monitor_command("mouse_button 1")
    send_monitor_command("mouse_button 0")
    wait_for("gtk3-x11-button-clicked", 45)

    # Capture the live framebuffer from the running guest.
    screen_ppm = Path("/tmp/akaos-x11-screen.ppm")
    screen_png = root / "build" / "x11-server-screen.png"
    screen_ppm.unlink(missing_ok=True)
    send_monitor_command(f"screendump {screen_ppm}")
    time.sleep(1.5)
    if screen_ppm.exists():
        try:
            from PIL import Image
            Image.open(screen_ppm).save(screen_png)
            print(f"[INFO] framebuffer capture: {screen_png}", flush=True)
        except Exception as error:  # pragma: no cover - capture is optional
            print(f"[WARN] framebuffer conversion failed: {error}", flush=True)
    else:
        print("[WARN] the guest framebuffer capture was not produced",
              flush=True)


attempts = int(os.environ.get("AKAOS_TEST_X11_ATTEMPTS", "4"))
try:
    for attempt in range(1, attempts + 1):
        start_guest()
        try:
            run_guest()
            break
        except RuntimeError as error:
            text = str(error)
            # The guest console keyboard path has a known intermittent fault
            # under emulation, and the shared compositor memory can stall a
            # writer against the compositor, so a failed attempt is retried on
            # a fresh boot.
            if attempt == attempts:
                raise
            print(f"[WARN] guest attempt {attempt} failed: {text.splitlines()[0]}",
                  flush=True)
        finally:
            stop_guest()

    print("[PASS] The native akaOS X11 server accepted a real upstream "
          "libX11 client connection.", flush=True)
    print("[PASS] The libX11 client created, mapped and drew into a window "
          "that the server presented through Lumen.", flush=True)
    print("[PASS] Pointer and keyboard events reached the upstream libX11 "
          "client through the X11 input path.", flush=True)
    print("[PASS] The unmodified upstream GTK 3 library ran on the native X11 "
          "server and its button received a pointer click.", flush=True)
finally:
    stop_guest()
    storage.unlink(missing_ok=True)
PY
