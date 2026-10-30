#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/tools/build-x86_32.sh"
fi

TEST_LOG="$ROOT_DIR/build/test-x86_32.log"
rm -f "$TEST_LOG"

echo "[TEST] Running akaOS x86_32 full parity test in QEMU..."

python3 - "$ROOT_DIR" "$TEST_LOG" <<'PY'
import subprocess, sys, time
from pathlib import Path

root_dir, log_path_str = sys.argv[1:3]
log_path = Path(log_path_str)
log_path.unlink(missing_ok=True)

qemu = subprocess.Popen([
    'qemu-system-i386', '-kernel', f'{root_dir}/build/XSCORE32.ELF',
    '-serial', f'file:{log_path}',
    '-monitor', 'stdio', '-display', 'none', '-m', '128M', '-no-reboot'
], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True)

def content():
    return log_path.read_text(errors='ignore') if log_path.exists() else ''

cur_offset = 0

# x86_32's software MMU and userspace self-tests are noticeably slower under
# some Ubuntu-WSL hosts; allow the same generous boot window as the native
# parity runners instead of misreporting a slow start as a keyboard failure.
def wait_for(text, timeout=90):
    global cur_offset
    deadline = time.time() + timeout
    while time.time() < deadline:
        c = content()
        idx = c.find(text, cur_offset)
        if idx != -1:
            cur_offset = idx + len(text)
            return
        if qemu.poll() is not None: raise RuntimeError('QEMU exited early')
        time.sleep(0.05)
    raise RuntimeError(f'timed out waiting for {text!r}')

def send_line(line):
    keys = {' ': 'spc', '/': 'slash', '-': 'minus', '.': 'dot', '_': 'shift-minus', '=': 'equal', '*': 'shift-8', '\t': 'tab'}
    for ch in line:
        qemu.stdin.write(f'sendkey {keys.get(ch, ch)}\n')
    qemu.stdin.write('sendkey ret\n')
    qemu.stdin.flush()
    time.sleep(0.5)

try:
    wait_for('login:')
    send_line('root')
    wait_for('Password:')
    send_line('root')
    wait_for('root@akaOS:/root#')
    send_line('hello --heap-test')
    wait_for('malloc fragmentation, back-reference free, and aligned allocation verified', 30)
    wait_for('posix_memalign alignment and error contract verified', 30)
    wait_for('heap allocator self-test passed')
    wait_for('root@akaOS:/root#')
    send_line('uname -a')
    wait_for('x86_32')
    wait_for('root@akaOS:/root#')
    send_line('uname a')
    wait_for("uname: extra operand 'a'")
    wait_for('root@akaOS:/root#')
    send_line('uname -')
    wait_for("uname: extra operand '-'")
    wait_for('root@akaOS:/root#')
    send_line('whoami')
    wait_for('root\n')
    wait_for('root@akaOS:/root#')
    send_line('pwd')
    wait_for('/root\n')
    wait_for('root@akaOS:/root#')
    send_line('pwd -')
    wait_for('pwd: invalid option or extra operand')
    wait_for('root@akaOS:/root#')
    send_line('touch glob_a glob_b glob_c')
    wait_for('root@akaOS:/root#')
    send_line('rm glob_*')
    wait_for('root@akaOS:/root#')
    send_line('ls glob_*')
    wait_for("cannot access 'glob_*': No such file or directory")
    wait_for('root@akaOS:/root#')
    send_line('ls /bin')
    wait_for('akash')
    wait_for('root@akaOS:/root#')
    send_line('hello --stack-growth-test')
    wait_for('stack growth verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --vm-test')
    wait_for('demand paging and COW verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --usercopy-test')
    wait_for('safe user copy-in/copy-out verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --mmap-test')
    wait_for('POSIX mmap private and immutable shared-file mappings verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --shared-memory-test')
    wait_for('POSIX shared memory and mprotect verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --poll-test')
    wait_for('poll readiness and input event descriptor verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --devfs-test')
    wait_for('visible /dev character devices verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --session-test')
    wait_for('sessions, process groups, and controlling TTY verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --pty-test')
    wait_for('PTY master/slave bidirectional I/O verified')
    wait_for('termios, ioctl, canonical/raw mode, and window size verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --proc-test')
    wait_for('generated /proc process information verified')
    wait_for('per-process /proc/<pid>/status lookup verified')
    wait_for('root@akaOS:/root#')
    send_line('tty')
    wait_for('/dev/tty1')
    wait_for('root@akaOS:/root#')
    send_line('stty -a')
    wait_for('speed 0 baud; rows')
    wait_for('root@akaOS:/root#')
    send_line('ps')
    wait_for('PID PPID PGID SID STATE')
    wait_for('root@akaOS:/root#')
    send_line('lumen --self-test')
    wait_for('userspace display surface protocol verified')
    wait_for('root@akaOS:/root#')
    send_line('hello --signal-test')
    wait_for('user-defined signal handling (sigaction) verified')
    wait_for('root@akaOS:/root#')
    # Test Linux-style Tab autocompletion:
    # 1. Command autocompletion: whoa<TAB> -> completes to whoami and outputs root
    send_line('whoa\t')
    wait_for('root\n')
    wait_for('root@akaOS:/root#')

    # 2. Path autocompletion: cat /etc/mo<TAB> -> completes to cat /etc/motd and outputs motd
    send_line('cat /etc/mo\t')
    wait_for('akaOS XSCore initramfs')
    wait_for('root@akaOS:/root#')

    # Test history recall via Up/Down arrow
    send_line('echo history_up_arrow_test')
    wait_for('history_up_arrow_test')
    wait_for('root@akaOS:/root#')
    qemu.stdin.write('sendkey up\nsendkey ret\n')
    qemu.stdin.flush()
    time.sleep(0.5)
    wait_for('history_up_arrow_test')
    wait_for('root@akaOS:/root#')

    send_line('echo x86_32_full_parity_verified')
    wait_for('x86_32_full_parity_verified')
    wait_for('root@akaOS:/root#')
    send_line('exit')
    wait_for(' exited\n')
    wait_for('login:')
    time.sleep(1)
finally:
    qemu.kill()
    qemu.wait()
PY

echo "[OUTPUT] x86_32 Serial Log:"
cat "$TEST_LOG"

grep -Fq "[PASS] Process isolation and lifecycle self-test passed" "$TEST_LOG"
grep -Fq "[PASS] akaOS user-mode milestone complete" "$TEST_LOG"
grep -Fq "x86_32" "$TEST_LOG"
grep -Fq "root@akaOS:/root#" "$TEST_LOG"
grep -Fq "akash" "$TEST_LOG"
grep -Fq "stack growth verified" "$TEST_LOG"
grep -Fq "demand paging and COW verified" "$TEST_LOG"
grep -Fq "safe user copy-in/copy-out verified" "$TEST_LOG"
grep -Fq "POSIX mmap private and immutable shared-file mappings verified" "$TEST_LOG"
grep -Fq "posix_memalign alignment and error contract verified" "$TEST_LOG"
grep -Fq "POSIX shared memory and mprotect verified" "$TEST_LOG"
grep -Fq "poll readiness and input event descriptor verified" "$TEST_LOG"
grep -Fq "visible /dev character devices verified" "$TEST_LOG"
grep -Fq "uname: extra operand 'a'" "$TEST_LOG"
grep -Fq "uname: extra operand '-'" "$TEST_LOG"
grep -Fq "userspace display surface protocol verified" "$TEST_LOG"
grep -Fq "user-defined signal handling (sigaction) verified" "$TEST_LOG"
grep -Fq "POSIX fork continuation verified" "$TEST_LOG"
grep -Fq "[INFO] POSIX execve process image replacement verified" "$TEST_LOG"
grep -Fq "POSIX execve userspace verified" "$TEST_LOG"
grep -Fq "POSIX fork-exec-wait lifecycle verified" "$TEST_LOG"
grep -Fq "[INFO] Copy-on-Write user fault resolved" "$TEST_LOG"
grep -Fq "[INFO] File-backed ELF page loaded on demand" "$TEST_LOG"
grep -Fq "akaOS XSCore initramfs" "$TEST_LOG"
grep -Fq "history_up_arrow_test" "$TEST_LOG"
grep -Fq "x86_32_full_parity_verified" "$TEST_LOG"
if [ "$(grep -Fc 'login:' "$TEST_LOG")" -lt 2 ]; then
    echo "[FAIL] x86_32 shell exit did not return to login" >&2
    exit 1
fi
if grep -Fq "[FATAL]" "$TEST_LOG"; then
    echo "[FAIL] x86_32 emitted a fatal kernel error" >&2
    exit 1
fi
if grep -Fq "root@akaOS:/root# root@akaOS:/root#" "$TEST_LOG"; then
    echo "[FAIL] x86_32 TTY rendered a duplicated shell prompt" >&2
    exit 1
fi

echo "[PASS] akaOS x86_32 native port full parity verification passed."
