#!/usr/bin/env bash
# Execute the full realistic Unix sequence in akaOS interactive shell and capture screenshot
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/interactive-test-screen.ppm"
SCREEN_PNG="$BUILD_DIR/interactive-test-screen.png"
RUN_LOG="$BUILD_DIR/interactive-test-run.log"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/build.sh" >/dev/null
fi

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.interactive.fd"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG"
: > "$RUN_LOG"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG" <<'EOF'
import sys
import os
import subprocess
import time
from PIL import Image

root_dir, ovmf_code, ovmf_vars, screen_ppm, screen_png, run_log = sys.argv[1:7]

def to_sendkeys(cmd):
    lines = []
    for ch in cmd:
        if 'a' <= ch <= 'z':
            lines.append(f"sendkey {ch}\n")
        elif 'A' <= ch <= 'Z':
            lines.append(f"sendkey shift-{ch.lower()}\n")
        elif '0' <= ch <= '9':
            lines.append(f"sendkey {ch}\n")
        elif ch == ' ':
            lines.append("sendkey spc\n")
        elif ch == '/':
            lines.append("sendkey slash\n")
        elif ch == '.':
            lines.append("sendkey dot\n")
        elif ch == '-':
            lines.append("sendkey minus\n")
        elif ch == '_':
            lines.append("sendkey shift-minus\n")
        elif ch == '>':
            lines.append("sendkey shift-dot\n")
        elif ch == '<':
            lines.append("sendkey shift-comma\n")
        elif ch == '"':
            lines.append("sendkey shift-apostrophe\n")
        elif ch == '$':
            lines.append("sendkey shift-4\n")
        elif ch == '?':
            lines.append("sendkey shift-slash\n")
        elif ch == '|':
            lines.append("sendkey shift-backslash\n")
        elif ch == ':':
            lines.append("sendkey shift-semicolon\n")
        elif ch == ',':
            lines.append("sendkey comma\n")
        elif ch == '=':
            lines.append("sendkey equal\n")
        elif ch == '+':
            lines.append("sendkey shift-equal\n")
        elif ch == '!':
            lines.append("sendkey shift-1\n")
        elif ch == '*':
            lines.append("sendkey shift-8\n")
    lines.append("sendkey ret\n")
    return "".join(lines)

qemu_cmd = [
    "qemu-system-x86_64",
    "-machine", "q35",
    "-smp", os.environ.get("AKAOS_SMP", "1"),
    "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
    "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
    "-drive", f"format=raw,file.locking=off,file={root_dir}/build/akaos.img",
    "-serial", f"file:{run_log}",
    "-monitor", "stdio",
    "-display", "none",
    "-no-reboot"
]

proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

# Wait for shell active in run_log
for _ in range(600):
    try:
        with open(run_log, "r", errors="ignore") as f:
            content = f.read()
            if "[INFO] akaOS shell active" in content:
                break
    except Exception:
        pass
    time.sleep(0.05)

time.sleep(0.5)

def prompt_count():
    try:
        with open(run_log, "r", errors="ignore") as f:
            # The test intentionally changes directories, so match the
            # stable prompt prefix rather than hard-coding /root.
            return f.read().count("root@akaOS:")
    except Exception:
        return 0

def wait_for_prompts(target, timeout=12.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        current = prompt_count()
        if current >= target:
            return current
        time.sleep(0.05)
    return prompt_count()

proc.stdin.write(to_sendkeys("root"))
proc.stdin.write(to_sendkeys("root"))
proc.stdin.flush()
time.sleep(1.0)

seen_prompts = prompt_count()

commands = [
    "pwd",
    "pwd -",
    "touch glob_a glob_b glob_c",
    "rm glob_*",
    "ls glob_*",
    'echo "Testing input redirection" > test.txt',
    'echo "Second line appended" >> test.txt',
    "cat test.txt",
    "chmod 755 test.txt",
    "chown root test.txt",
    "ls -l test.txt",
    "wc -l < test.txt",
    "sync",
    "cat /etc/motd | grep -i xscore | wc -w",
    'echo "User: $USER on $ARCH, status: $?"',
    "uname -a > uname.txt",
    "cat uname.txt | wc -w",
    "hello --session-test",
    "hello --pty-test",
    "hello --proc-test",
    "tty",
    "stty -a",
    "stty raw",
    "stty sane",
    "echo raw_mode_recovered",
    "ps",
    "ted --help",
    "rm test.txt",
    "rm uname.txt",
    "mkdir /deep",
    "touch /deep/item.txt",
    "rm -rf /deep",
    "rm -f /missing_file",
    "rm --help",
    "cd /bin",
    "ls | head -n 4",
    "cd",
    "clear",
    'echo "All akaOS utilities, permissions & editor verified!"',
    "ls /bin | tail -n 8"
]

for cmd in commands:
    send_str = to_sendkeys(cmd)
    proc.stdin.write(send_str)
    proc.stdin.flush()
    # Wait for the shell's next prompt so slower pipelines and filesystem
    # operations cannot leave a backlog that outlives the test.
    seen_prompts = wait_for_prompts(seen_prompts + 1)

time.sleep(0.5)
# Test Up arrow history recall and re-execution
proc.stdin.write("sendkey up\n")
proc.stdin.flush()
time.sleep(0.6)
proc.stdin.write("sendkey ret\n")
proc.stdin.flush()
seen_prompts = wait_for_prompts(seen_prompts + 1)

time.sleep(0.5)
# Send two complete commands without waiting between lines.  This exercises
# the TTY pending-input queue: the second line must survive while userspace is
# still handling the first command.
proc.stdin.write(to_sendkeys("echo tty_burst_one"))
proc.stdin.write(to_sendkeys("echo tty_burst_two"))
proc.stdin.flush()
seen_prompts = wait_for_prompts(seen_prompts + 2)
proc.stdin.write(f"screendump {screen_ppm}\n")
proc.stdin.flush()
time.sleep(0.5)
proc.stdin.write("quit\n")
proc.stdin.flush()
proc.wait()

Image.open(screen_ppm).convert("RGB").save(screen_png)
print("Interactive test screenshot saved:", screen_png)
EOF

grep -Fq "pwd: invalid option or extra operand" "$RUN_LOG"
grep -Fq "ls: cannot access 'glob_*': No such file or directory" "$RUN_LOG"
grep -Fq "sessions, process groups, and controlling TTY verified" "$RUN_LOG"
grep -Fq "PTY master/slave bidirectional I/O verified" "$RUN_LOG"
grep -Fq "termios, ioctl, canonical/raw mode, and window size verified" "$RUN_LOG"
grep -Fq "generated /proc process information verified" "$RUN_LOG"
grep -Fq "per-process /proc/<pid>/status lookup verified" "$RUN_LOG"
grep -Fq "/dev/tty1" "$RUN_LOG"
grep -Fq "rows " "$RUN_LOG"
grep -Fq "raw_mode_recovered" "$RUN_LOG"
grep -Fq "tty_burst_one" "$RUN_LOG"
grep -Fq "tty_burst_two" "$RUN_LOG"
grep -Fq "PID PPID PGID SID STATE" "$RUN_LOG"
echo "[PASS] x86_64 terminal, proc, wildcard, and command checks passed."
