#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

"$ROOT_DIR/build.sh"
TEST_IMAGE="$ROOT_DIR/build/akaos-test.img"
STORAGE_IMAGE="$ROOT_DIR/build/akaos-virtio-test.img"
TEST_MARKER="$ROOT_DIR/build/AKAOS.TEST"
TEST_LOG="$ROOT_DIR/build/boot-test.log"
cp "$ROOT_DIR/build/akaos.img" "$TEST_IMAGE"
: > "$TEST_MARKER"
mcopy -o -i "$TEST_IMAGE" "$TEST_MARKER" ::/AKAOS.TEST

# Attach a small deterministic secondary disk so the kernel test proves a real
# PCI-discovered virtio-blk sector read without changing the boot image.
python3 - "$STORAGE_IMAGE" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
with path.open("wb") as handle:
    handle.truncate(4 * 1024 * 1024)
    handle.write(b"AKAOS-VIRTIO-BLK-TEST\n")
PY
if [[ "${AKAOS_TEST_VERBOSE:-0}" == "1" ]]; then
    VERBOSE_MARKER="$ROOT_DIR/build/VERBOSE"
    : > "$VERBOSE_MARKER"
    mcopy -o -i "$TEST_IMAGE" "$VERBOSE_MARKER" ::/VERBOSE
fi

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$ROOT_DIR/build/OVMF_VARS.test.fd"
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"

set +e
timeout 90s qemu-system-x86_64 \
    -machine q35 \
    -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$OVMF_VARS" \
    -drive format=raw,file.locking=off,file="$TEST_IMAGE" \
    -drive if=none,format=raw,file="$STORAGE_IMAGE",id=akaos-storage \
    -device virtio-blk-pci,drive=akaos-storage \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -display none \
    -serial file:"$TEST_LOG" \
    -monitor none \
    -no-reboot
QEMU_STATUS=$?
set -e

cat "$TEST_LOG"
if [[ "$QEMU_STATUS" -ne 33 ]]; then
    echo "[FAIL] QEMU exited with status $QEMU_STATUS (expected 33)." >&2
    exit 1
fi
grep -Fq "[INFO] XSCore entry reached" "$TEST_LOG"
grep -Fq "[INFO] UEFI framebuffer initialized" "$TEST_LOG"
grep -Fq "[INFO] GDT initialized" "$TEST_LOG"
grep -Fq "[INFO] IDT initialized with 32 exception vectors" "$TEST_LOG"
grep -Fq "[INFO] Breakpoint exception handled by XSCore IDT" "$TEST_LOG"
grep -Fq "[INFO] Physical page allocator verified" "$TEST_LOG"
grep -Fq "[INFO] Null-page guard active" "$TEST_LOG"
grep -Fq "[INFO] XSCore virtual memory active" "$TEST_LOG"
grep -Fq "[INFO] Kernel heap verified" "$TEST_LOG"
grep -Fq "[INFO] Section permissions:" "$TEST_LOG"
grep -Fq "[INFO] Kernel boot stack allocated" "$TEST_LOG"
grep -Fq "[INFO] TSS installed" "$TEST_LOG"
grep -Fq "[INFO] 8259 PIC disabled" "$TEST_LOG"
grep -Fq "[INFO] Local APIC enabled" "$TEST_LOG"
grep -Fq "[INFO] APIC timer running at 100 Hz" "$TEST_LOG"
grep -Fq "[INFO] Kernel boot screen rendered" "$TEST_LOG"
grep -Fq "[INFO] Kernel thread subsystem initialized" "$TEST_LOG"
grep -Fq "[INFO] Kernel thread worker entered" "$TEST_LOG"
grep -Fq "[INFO] Timer requested deferred preemption" "$TEST_LOG"
grep -Fq "[INFO] Worker resumed after safe preemption checkpoint" "$TEST_LOG"
grep -Fq "[INFO] Safe deferred preemptions: 1" "$TEST_LOG"
grep -Fq "[INFO] Exited kernel thread resources reclaimed" "$TEST_LOG"
grep -Fq "[INFO] Kernel thread self-test passed" "$TEST_LOG"
grep -Fq "[ OK ] Kernel threads active" "$TEST_LOG"
grep -Fq "[PASS] akaOS Phase 3 milestone complete" "$TEST_LOG"
grep -Fq "[INFO] Read-only initramfs mounted at /" "$TEST_LOG"
grep -Fq "[INFO] VFS paths /bin and /etc verified" "$TEST_LOG"
grep -Fq "[INFO] PCI configuration-space scan complete" "$TEST_LOG"
grep -Fq "[INFO] virtio-blk legacy device ready" "$TEST_LOG"
grep -Fq "[INFO] virtio-blk sector read verified" "$TEST_LOG"
grep -Fq "[INFO] Ring 3 system-call gate installed" "$TEST_LOG"
grep -Fq "[INFO] Created isolated user process 1" "$TEST_LOG"
grep -Fq "[INFO] Created isolated user process 2" "$TEST_LOG"
grep -Fq "[INFO] Guarded per-task privilege-entry stacks verified" "$TEST_LOG"
grep -Fq "[INFO] Clean executable text pages are shared from page cache" "$TEST_LOG"
grep -Fq "[INFO] Clean file-page cache accounting verified" "$TEST_LOG"
grep -Fq "[INFO] Executable loaded through VFS path /bin/hello" "$TEST_LOG"
grep -Fq "[INFO] Userspace ELF reservation permissions verified" "$TEST_LOG"
test "$(grep -Fc "akaOS XSCore initramfs" "$TEST_LOG")" -eq 8
grep -Fq "[INFO] VFS open/read/EOF/error syscalls verified" "$TEST_LOG"
grep -Fq "[INFO] Per-process stdin/stdout/stderr tables verified" "$TEST_LOG"
test "$(grep -Fc "[USER] Hello from akaOS C userspace" "$TEST_LOG")" -eq 3
test "$(grep -Fc "stack growth verified" "$TEST_LOG")" -eq 3
grep -Fq "[INFO] User process 1 exited" "$TEST_LOG"
grep -Fq "[INFO] User process 2 exited" "$TEST_LOG"
grep -Fq "[INFO] User process 3 exited" "$TEST_LOG"
grep -Fq "[INFO] Blocking wait for process 1" "$TEST_LOG"
grep -Fq "[INFO] Waiter resumed for process 1" "$TEST_LOG"
grep -Fq "[INFO] Reclaimed process and thread slots reused" "$TEST_LOG"
grep -Fq "[INFO] Anonymous heap page committed on demand" "$TEST_LOG"
grep -Fq "[INFO] Copy-on-Write user fault resolved" "$TEST_LOG"
grep -Fq "[INFO] File-backed ELF page loaded on demand" "$TEST_LOG"
grep -Fq "[INFO] Executable image reservations are initially non-resident" "$TEST_LOG"
grep -Fq "[INFO] Demand paging and Copy-on-Write fork verified" "$TEST_LOG"
grep -Fq "demand paging and COW verified" "$TEST_LOG"
grep -Fq "POSIX fork continuation verified" "$TEST_LOG"
grep -Fq "[INFO] POSIX execve process image replacement verified" "$TEST_LOG"
grep -Fq "POSIX execve userspace verified" "$TEST_LOG"
grep -Fq "POSIX fork-exec-wait lifecycle verified" "$TEST_LOG"
grep -Fq "POSIX mmap/munmap private mappings verified" "$TEST_LOG"
grep -Fq "POSIX shared memory and mprotect verified" "$TEST_LOG"
grep -Fq "safe user copy-in/copy-out verified" "$TEST_LOG"
grep -Fq "[INFO] Safe user copy-in/copy-out verified" "$TEST_LOG"
grep -Fq "user-defined signal handling (sigaction) verified" "$TEST_LOG"
grep -Fq "[INFO] User-defined signal handling (sigaction) verified" "$TEST_LOG"
grep -Fq "[INFO] Demand-paged private mmap/munmap verified" "$TEST_LOG"
grep -Fq "[INFO] POSIX shared memory and mprotect verified" "$TEST_LOG"
grep -Fq "poll readiness and input event descriptor verified" "$TEST_LOG"
grep -Fq "[INFO] poll readiness and input event descriptors verified" "$TEST_LOG"
grep -Fq "visible /dev character devices verified" "$TEST_LOG"
grep -Fq "[INFO] Visible /dev character devices verified" "$TEST_LOG"
grep -Fq "userspace framebuffer device verified" "$TEST_LOG"
grep -Fq "userspace display surface protocol verified" "$TEST_LOG"
grep -Fq "[INFO] Userspace display surface protocol verified" "$TEST_LOG"
grep -Fq "[INFO] SIGINT trampoline termination verified" "$TEST_LOG"
grep -Fq "[INFO] Process and thread physical pages fully reclaimed" "$TEST_LOG"
grep -Fq "[INFO] Blocking process wait and thread reclamation verified" "$TEST_LOG"
grep -Fq "[INFO] Descriptor write/dup/dup2/close syscalls verified" "$TEST_LOG"
grep -Fq "[INFO] Native C runtime and libc wrappers verified" "$TEST_LOG"
grep -Fq "[INFO] Internal process API create/wait/reap verified" "$TEST_LOG"
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "motd")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "hello")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "cat")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "ls")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "head")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "tail")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "wc")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "uname")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "whoami")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "sync")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "init")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "reboot")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "poweroff")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "chmod")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "chown")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "ted")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "cp")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "mv")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "less")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "more")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "find")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "sort")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "uniq")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "xargs")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "test")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "[")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "sshd")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "ssh")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "kill")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "pkill")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "uptime")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "free")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "watch")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "sed")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "diff")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "cmp")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "patch")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "tar")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "gzip")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "gunzip")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "route")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "traceroute")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "dig")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "arp")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "lscpu")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "lspci")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "gpuinfo")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "sndinfo")" -eq 1
test "$(tr -d '\r' < "$TEST_LOG" | grep -Fxc "acpi")" -eq 1
grep -Fq "[INFO] Command-line arguments argc/argv verified" "$TEST_LOG"
grep -Fq "[INFO] Stream utilities find/sort/uniq/test/[ and akash scripting engine verified" "$TEST_LOG"
grep -Fq "[INFO] SSH suite and akaos-config daemon integration verified" "$TEST_LOG"
grep -Fq "[INFO] Scripting & admin utilities sleep/which/env/cut/tr/df/du verified" "$TEST_LOG"
grep -Fq "[INFO] System & process control suite kill/pkill/uptime/free/watch verified" "$TEST_LOG"
grep -Fq "[INFO] Unix text transformation & diff suite sed/diff/cmp/patch verified" "$TEST_LOG"
grep -Fq "[INFO] Archiving & compression suite tar/gzip/gunzip verified" "$TEST_LOG"
grep -Fq "[INFO] Advanced shell job control & pipelines in akash verified" "$TEST_LOG"
grep -Fq "[INFO] Manual page keyword lookup utility apropos verified" "$TEST_LOG"
grep -Fq "[INFO] Spell checker utility aspell verified" "$TEST_LOG"
grep -Fq "[INFO] Network diagnostics & management suite netstat/route/traceroute/dig/arp verified" "$TEST_LOG"
grep -Fq "[INFO] Hardware drivers and diagnostics suite lscpu/lspci/gpuinfo/sndinfo verified" "$TEST_LOG"
grep -Fq "[INFO] ACPI diagnostics utility acpi verified" "$TEST_LOG"
grep -Fq "[INFO] VFS stat/getdents syscalls verified" "$TEST_LOG"
grep -Fq "[INFO] Real error exit codes (\$?) and dynamic VFS mutations verified" "$TEST_LOG"
grep -Fq "[INFO] Native /bin/cat, /bin/ls, /bin/mkdir, /bin/touch, /bin/rm, /bin/cp, /bin/mv utilities verified" "$TEST_LOG"
grep -Fq "[INFO] Native /bin/head, /bin/tail, /bin/wc, /bin/uname, /bin/whoami, /bin/sync, /bin/reboot, /bin/poweroff utilities verified" "$TEST_LOG"
grep -Fq "[INFO] Native /bin/chmod, /bin/chown, /bin/ted utilities and permissions verified" "$TEST_LOG"
grep -Fq "[INFO] Dynamic multi-block VFS allocation and page reclamation verified" "$TEST_LOG"
grep -Fq "[INFO] User-mode interrupt-return preemption observed" "$TEST_LOG"
grep -Fq "[PASS] Process isolation and lifecycle self-test passed" "$TEST_LOG"
grep -Fq "[PASS] akaOS user-mode milestone complete" "$TEST_LOG"
if [[ "${AKAOS_TEST_VERBOSE:-0}" == "1" ]]; then
    grep -Fq "[INFO] Verbose boot flag detected" "$TEST_LOG"
else
    grep -Fq "[INFO] Quiet boot: early console hidden, splash will follow" "$TEST_LOG"
fi
grep -Fq "[INFO] virtio-blk MSI-X interrupt received" "$TEST_LOG"
grep -Fq "[PASS] SMP concurrent work and shared-counter stress verified" "$TEST_LOG"
echo "[PASS] Automated UEFI boot test completed."
