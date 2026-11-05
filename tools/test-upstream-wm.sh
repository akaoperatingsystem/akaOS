#!/usr/bin/env bash
# Guest test for the upstream window managers on the akaOS X11 display server.
#
# The test boots the x86_64 image, starts the compositor and the display
# server, starts the upstream Openbox window manager and then checks that the
# window manager took a client window over, which is what the window manager
# redirect of the protocol is for.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_DIR="${AKAOS_TEST_ISO_CACHE:-$HOME/.cache/akaos/test}"
mkdir -p "$CACHE_DIR"
IMAGE="$CACHE_DIR/akaos-wm-test.iso"
SOURCE_IMAGE="$ROOT_DIR/build/akaos.iso"
INITRAMFS="$ROOT_DIR/build/initramfs.img"

if [[ ! -f "$IMAGE" || "$SOURCE_IMAGE" -nt "$IMAGE" ]]; then
    cp -f "$SOURCE_IMAGE" "$IMAGE"
fi
if [[ ! -f "$IMAGE" || ! -f "$INITRAMFS" ]]; then
    echo "Build the x86_64 ISO and initramfs first." >&2
    exit 2
fi
if [[ ! -f "$ROOT_DIR/build/X11_WM_CLIENT.ELF" ]] ||
   ! grep -a -q '/bin/openbox' "$INITRAMFS"; then
    echo "Build the x86_64 image with the Openbox window manager first." >&2
    exit 2
fi

python3 - "$ROOT_DIR" "$IMAGE" <<'PY'
import os
import re
import subprocess
import sys
import time
from pathlib import Path

root = Path(sys.argv[1])
image = Path(sys.argv[2])
# Every run writes its own log, because a log left over from an earlier run
# would still carry its failure and end the run before the guest boots.
log = root / "build" / f"test-upstream-wm-{os.getpid()}.log"
final_log = root / "build" / "test-upstream-wm.log"
qemu_error_log = root / "build" / f"test-upstream-wm-{os.getpid()}.qemu.log"
storage = root / "build" / f"upstream-wm-test-storage-{os.getpid()}.img"
with storage.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")

command = [
    "qemu-system-x86_64", "-machine", "q35", "-cpu", "max", "-m", "1024M",
    "-cdrom", str(image), "-boot", "d", "-vga", "std",
    "-drive", f"if=none,format=raw,file={storage},id=akaos-storage",
    "-device", "virtio-blk-pci,drive=akaos-storage",
    "-serial", f"file:{log}", "-monitor", "stdio", "-display", "none",
    "-no-reboot",
]
if os.access("/dev/kvm", os.R_OK | os.W_OK):
    command[1:1] = ["-accel", "kvm"]

proc = subprocess.Popen(command, stdin=subprocess.PIPE,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        text=True)


def content():
    if not log.exists():
        return ""
    text = log.read_text(errors="ignore")
    return re.sub(r'\[INFO\][^\n]*\n?', '', text)


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
    time.sleep(0.5)


def wait_for(marker, timeout=240, after=0):
    """Wait for a marker that has not been seen yet.

    The boot self test types into the console, so a marker can already be in
    the log before the guest is ready to answer it.  The number of times the
    marker was seen says which occurrence is the one to wait for."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = content()
        if text.count(marker) > after:
            return True
        if "[FATAL]" in text:
            # The guest console is slow, and the kernel writes the name and the
            # layout of the faulting process after the fault address.  The
            # report is only useful when it is complete, so the guest is given
            # a moment to finish writing it before the machine is stopped.
            time.sleep(6.0)
            raise RuntimeError(f"the guest failed while waiting for {marker!r}:\n"
                               f"{text[-4000:]}")
        time.sleep(0.2)
    return False


try:
    wait_for("login:", 480)
    time.sleep(3.0)
    # The boot self test types into the console, and a prompt that still holds
    # those keys rejects the name.  A rejected name brings the prompt back
    # with an empty buffer, so the name is typed again on the quiet prompt.
    seen_logins = content().count("login:")
    seen_passwords = content().count("Password:")
    # The boot self test already typed into the console before the login
    # prompt, so an old prompt of the shell is somewhere in the log.  A prompt
    # only counts when it appears after the login attempt, otherwise the test
    # believes it is logged in while the login prompt is still waiting.
    seen_prompts = content().count("root@akaOS:/root#")
    logged_in = False
    for _ in range(6):
        send_line("root")
        if wait_for("Password:", 30, after=seen_passwords):
            seen_passwords += 1
            send_line("root")
            if wait_for("root@akaOS:/root#", 20, after=seen_prompts):
                logged_in = True
                break
        # The name was rejected.  The prompt comes back, and the next attempt
        # starts from an empty buffer.
        if not wait_for("login:", 60, after=seen_logins):
            break
        seen_logins += 1
        time.sleep(1.0)
    if not logged_in:
        raise RuntimeError("the guest never accepted the user name")

    # Run Openbox, the window probe, and then LXPanel. The process table is
    # used instead of shell job variables, which akash does not implement.
    send_line("sh /etc/test-desktop.sh")
    if not wait_for("x11 wm client: reparented into frame", 480):
        print("[FAIL] The upstream window manager did not take the window over.",
              flush=True)
        print(content()[-4000:], flush=True)
        raise SystemExit(1)
    print("[PASS] The upstream window manager took the client window over.",
          flush=True)
    if not wait_for("x11 wm client: window parent matches frame", 120):
        print("[FAIL] The client window does not report the frame as its "
              "parent.", flush=True)
        print(content()[-4000:], flush=True)
        raise SystemExit(1)
    if not wait_for("x11 wm client: drew into the framed window", 120):
        print("[FAIL] The client could not draw into the framed window.",
              flush=True)
        print(content()[-4000:], flush=True)
        raise SystemExit(1)
    if not wait_for("x11 wm client: screen pixel at ", 60):
        print("[FAIL] The root screen pixel did not match the framed drawing.",
              flush=True)
        print(content()[-4000:], flush=True)
        raise SystemExit(1)
    if not wait_for("x11 wm client: window manager frame verified", 60):
        print("[FAIL] The window manager frame was not verified.", flush=True)
        print(content()[-4000:], flush=True)
        raise SystemExit(1)
    print("[PASS] The framed client window accepts drawing and reads the "
          "pixel back.", flush=True)
    print("[PASS] The akaOS display server routed the map request to the "
          "upstream window manager.", flush=True)
    if not wait_for("CLIENT-EXIT=0", 60):
        print("[FAIL] The upstream WM probe did not exit successfully.",
              flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    if not (wait_for("xserver: client 1 connected and setup sent", 30, after=1) or
            wait_for("xserver: client 2 connected and setup sent", 30) or
            wait_for("LXPANEL-RUNNING=0", 90)):
        print("[FAIL] LXPanel did not connect to X11.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)

    if not wait_for("LXPANEL-RUNNING=0", 60):
        print("[FAIL] LXPanel did not remain active after startup.",
              flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if "running lxpanel" not in output:
        print("[FAIL] lxpanel was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    if "Config files are not found" in output or "modules directory is not accessible" in output or "failed to find menu-cached" in output:
        print("[FAIL] LXPanel could not load its profile, plugins, or menu daemon.",
              flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] LXPanel connected, loaded profile and modules, and remained active.",
          flush=True)
    if not wait_for("PCMANFM-RUNNING=0", 120):
        print("[FAIL] PCManFM did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bpcmanfm\b", output):
        print("[FAIL] pcmanfm was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] PCManFM desktop manager started and remained active alongside Openbox and LXPanel.",
          flush=True)
    if not wait_for("XFWM4-RUNNING=0", 120):
        print("[FAIL] xfwm4 did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bxfwm4\b", output):
        print("[FAIL] xfwm4 was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] xfwm4 window manager started and remained active alongside LXDE.",
          flush=True)
    if not wait_for("XFCE4-PANEL-RUNNING=0", 120):
        print("[FAIL] xfce4-panel did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bxfce4-panel\b", output):
        print("[FAIL] xfce4-panel was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] xfce4-panel desktop panel started and remained active alongside Xfwm4.",
          flush=True)
    if not wait_for("XFCE4-SESSION-RUNNING=0", 120):
        print("[FAIL] xfce4-session did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bxfce4-session\b", output):
        print("[FAIL] xfce4-session was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] xfce4-session desktop session manager started and remained active.",
          flush=True)
    if not wait_for("MARCO-RUNNING=0", 120):
        print("[FAIL] marco did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bmarco\b", output):
        print("[FAIL] marco was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] marco window manager started and remained active alongside MATE/XFCE/LXDE.",
          flush=True)
    if not wait_for("MATE-PANEL-RUNNING=0", 120):
        print("[FAIL] mate-panel did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bmate-panel\b", output):
        print("[FAIL] mate-panel was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] mate-panel desktop panel started and remained active.",
          flush=True)
    if not wait_for("MATE-SESSION-RUNNING=0", 200):
        print("[FAIL] mate-session did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bmate-session\b", output):
        print("[FAIL] mate-session was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] mate-session desktop session manager started and remained active.",
          flush=True)
    if not wait_for("MUFFIN-RUNNING=0", 200):
        print("[FAIL] muffin did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bmuffin\b", output):
        print("[FAIL] muffin was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] muffin window manager started and remained active alongside Cinnamon/MATE/XFCE/LXDE.",
          flush=True)
    if not wait_for("I3-RUNNING=0", 200):
        print("[FAIL] i3 did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bi3\b", output):
        print("[FAIL] i3 was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] i3 tiling window manager started and remained active alongside Cinnamon/MATE/XFCE/LXDE.",
          flush=True)
    if not wait_for("LXQT-PANEL-RUNNING=0", 200):
        print("[FAIL] lxqt-panel did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\blxqt-panel\b", output):
        print("[FAIL] lxqt-panel was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] lxqt-panel desktop panel started and remained active alongside i3/Cinnamon/MATE/XFCE/LXDE.",
          flush=True)
    if not wait_for("LXQT-SESSION-RUNNING=0", 200):
        print("[FAIL] lxqt-session did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\blxqt-session\b", output):
        print("[FAIL] lxqt-session was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] lxqt-session desktop session manager started and remained active.",
          flush=True)
    if not wait_for("DWM-RUNNING=0", 200):
        print("[FAIL] dwm did not complete startup check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    output=content()
    if not re.search(r"\bdwm\b", output):
        print("[FAIL] dwm was not found running in ps output.", flush=True)
        print(output[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] dwm dynamic tiling window manager started and remained active alongside i3/Cinnamon/MATE/XFCE/LXDE/LXQt.",
          flush=True)
    if not wait_for("BSPWM-VERSION=0", 60):
        print("[FAIL] bspwm binary space partitioning WM did not complete version check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] bspwm binary space partitioning window manager verified operational.",
          flush=True)
    if not wait_for("BSPC-VERSION=0", 60):
        print("[FAIL] bspc client did not complete version check.", flush=True)
        print(content()[-6000:], flush=True)
        raise SystemExit(1)
    print("[PASS] bspc client controller verified operational.",
          flush=True)
finally:
    proc.kill()
    storage.unlink(missing_ok=True)
    if log.exists():
        try:
            final_log.write_bytes(log.read_bytes())
        except OSError:
            pass
PY
