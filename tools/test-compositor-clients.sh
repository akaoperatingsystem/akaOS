#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_LOG_DIR="${AKAOS_TEST_LOG_DIR:-$ROOT_DIR/build}"
mkdir -p "$TEST_LOG_DIR"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    "$ROOT_DIR/build.sh" >/dev/null
    # Exercise the same dual-firmware ISO configuration that users boot on
    # both x86 targets; the default build only emits the x86_64 UEFI disk.
    "$ROOT_DIR/tools/build-bios.sh" --no-build >/dev/null
    "$ROOT_DIR/tools/build-x86_32.sh" --firmware both --disk-type iso >/dev/null
    "$ROOT_DIR/tools/build-aarch64.sh" >/dev/null
    "$ROOT_DIR/tools/build-arm32.sh" >/dev/null
    "$ROOT_DIR/tools/build-riscv64.sh" >/dev/null
    "$ROOT_DIR/tools/build-riscv32.sh" >/dev/null
fi

for image in \
    "$ROOT_DIR/build/akaos.iso" \
    "$ROOT_DIR/build/akaos32.iso" \
    "$ROOT_DIR/build/XSCORE_AARCH64.ELF" \
    "$ROOT_DIR/build/XSCORE_ARM32.ELF" \
    "$ROOT_DIR/build/XSCORE_RISCV64.ELF" \
    "$ROOT_DIR/build/XSCORE_RISCV32.ELF"; do
    if [[ ! -f "$image" ]]; then
        echo "[FAIL] Missing compositor test image: $image" >&2
        exit 1
    fi
done

echo "[TEST] Verifying bounded compositor IPC and palette lifecycle on six architectures..."

python3 - "$ROOT_DIR" <<'PY'
import fcntl
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

root_dir = Path(sys.argv[1])
test_iso = Path(os.environ.get("AKAOS_TEST_ISO", root_dir / "build/akaos.iso"))
test_iso32 = Path(os.environ.get("AKAOS_TEST_X86_32_ISO", root_dir / "build/akaos32.iso"))
kernel_dir = Path(os.environ.get("AKAOS_TEST_KERNEL_DIR", root_dir / "build"))
log_dir = Path(os.environ.get("AKAOS_TEST_LOG_DIR", root_dir / "build"))
log_dir.mkdir(parents=True, exist_ok=True)

targets = {
    "x86_64": ("qemu-system-x86_64", ["-machine", "q35", "-cdrom", str(test_iso), "-boot", "d"]),
    "x86_32": ("qemu-system-i386", ["-cdrom", str(test_iso32), "-boot", "d"]),
    "aarch64": ("qemu-system-aarch64", ["-M", "virt", "-cpu", "cortex-a57", "-kernel", str(kernel_dir / "XSCORE_AARCH64.ELF")]),
    "arm32": ("qemu-system-arm", ["-M", "virt,highmem=off", "-cpu", "cortex-a7", "-kernel", str(kernel_dir / "XSCORE_ARM32.ELF")]),
    "riscv64": ("qemu-system-riscv64", ["-M", "virt", "-kernel", str(kernel_dir / "XSCORE_RISCV64.ELF")]),
    "riscv32": ("qemu-system-riscv32", ["-M", "virt", "-kernel", str(kernel_dir / "XSCORE_RISCV32.ELF")]),
}
x86_64_kernel = os.environ.get("AKAOS_TEST_X86_64_KERNEL")
x86_64_uefi_disk = os.environ.get("AKAOS_TEST_X86_64_UEFI_DISK")
if x86_64_uefi_disk:
    ovmf_code = os.environ.get("AKAOS_TEST_OVMF_CODE")
    ovmf_vars_source = os.environ.get("AKAOS_TEST_OVMF_VARS_SOURCE")
    if not ovmf_code or not ovmf_vars_source:
        raise RuntimeError(
            "UEFI test requires AKAOS_TEST_OVMF_CODE and "
            "AKAOS_TEST_OVMF_VARS_SOURCE"
        )
    ovmf_vars = log_dir / "OVMF_VARS.x86_64-test.fd"
    shutil.copyfile(ovmf_vars_source, ovmf_vars)
    targets["x86_64"] = (
        "qemu-system-x86_64",
        ["-machine", "q35", "-boot", "order=c",
         "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
         "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
         "-drive", f"format=raw,file={x86_64_uefi_disk}"],
    )
elif x86_64_kernel:
    kernel_path = Path(x86_64_kernel)
    if not kernel_path.is_file():
        raise RuntimeError(f"x86_64 test kernel does not exist: {kernel_path}")
    targets["x86_64"] = (
        "qemu-system-x86_64",
        ["-machine", "q35", "-kernel", str(kernel_path)],
    )
requested = os.environ.get("AKAOS_TEST_ARCHES", "").split()
test_memory_override = os.environ.get("AKAOS_TEST_MEMORY")
if requested:
    unknown = [name for name in requested if name not in targets]
    if unknown:
        raise RuntimeError("unknown AKAOS_TEST_ARCHES value(s): " + ", ".join(unknown))
    targets = {name: targets[name] for name in requested}

def to_sendkeys(text):
    keys = {
        " ": "spc", "/": "slash", ".": "dot", "-": "minus", "_": "shift-minus",
        "&": "shift-7", ";": "semicolon", "=": "equal", ":": "shift-semicolon", "#": "shift-3",
    }
    result = []
    for ch in text:
        if "a" <= ch <= "z" or "0" <= ch <= "9":
            result.append(f"sendkey {ch} 5\n")
        elif "A" <= ch <= "Z":
            result.append(f"sendkey shift-{ch.lower()} 5\n")
        elif ch in keys:
            result.append(f"sendkey {keys[ch]} 5\n")
    result.append("sendkey ret 5\n")
    return result

def run_target(name, qemu_bin, kernel_args):
    log_path = log_dir / f"test-compositor-clients-{name}.log"
    log_path.unlink(missing_ok=True)
    # x86 production uses its native i8042 keyboard and PS/2 mouse. Native
    # virt machines use the kernel's MMIO VirtIO input driver.
    # The x86_64 kernel embeds the full upstream graphics/font initramfs in
    # its loadable data segment. Keep enough RAM for that segment plus guest
    # working memory; small native targets fit in the architecture default.
    test_memory = test_memory_override or ("512M" if name == "x86_64" else "128M")
    input_devices = [] if name.startswith("x86") else ["-device", "virtio-keyboard-device", "-device", "virtio-mouse-device"]
    display_device = ["-vga", "std"] if name.startswith("x86") else ["-device", "ramfb"]
    command = [qemu_bin, *kernel_args, "-m", test_memory, *display_device, *input_devices,
               "-serial", f"file:{log_path}", "-monitor", "stdio", "-display", "none", "-no-reboot"]
    if os.environ.get("AKAOS_TEST_QEMU_DEBUG") == "1":
        qemu_debug_log = log_dir / f"test-compositor-clients-{name}-qemu.log"
        qemu_debug_flags = os.environ.get(
            "AKAOS_TEST_QEMU_DEBUG_FLAGS", "int,guest_errors"
        )
        command += ["-d", qemu_debug_flags, "-D", str(qemu_debug_log)]
    qemu = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, text=True)
    offset = 0

    def content():
        return log_path.read_text(errors="ignore") if log_path.exists() else ""

    def wait_for(needle, timeout=45):
        nonlocal offset
        if timeout == 30:
            timeout = 45
        if name == "x86_32" and timeout < 60:
            timeout = 60
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = content()
            index = data.find(needle, offset)
            if index != -1:
                offset = index + len(needle)
                return
            if "[FATAL]" in data:
                raise RuntimeError(
                    f"{name}: guest reported a fatal exception while waiting "
                    f"for {needle!r}; guest output:\n{data[-6000:]}"
                )
            if qemu.poll() is not None:
                raise RuntimeError(f"{name}: QEMU exited while waiting for {needle!r}")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for {needle!r}")

    def wait_for_all(needles, timeout=45, start_offset=None):
        nonlocal offset
        if name == "x86_32" and timeout < 90:
            timeout = 90
        if start_offset is None:
            start_offset = offset
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = content()
            missing = [needle for needle in needles if needle not in data[start_offset:]]
            if not missing:
                last_match = max(
                    data.find(needle, start_offset) + len(needle)
                    for needle in needles
                )
                offset = max(offset, last_match)
                return
            if "[FATAL]" in data:
                raise RuntimeError(
                    f"{name}: guest reported a fatal exception while waiting for "
                    f"required output {missing[0]!r}; guest output:\n{data[-6000:]}"
                )
            if qemu.poll() is not None:
                raise RuntimeError(
                    f"{name}: QEMU exited while waiting for required output "
                    f"{missing[0]!r}"
                )
            time.sleep(0.05)
        raise RuntimeError(
            f"{name}: timed out waiting for required output: {', '.join(missing)}"
        )

    def wait_for_one_of(needles, timeout=45):
        nonlocal offset
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = content()
            matches = [
                (index, needle)
                for needle in needles
                if (index := data.find(needle, offset)) != -1
            ]
            if matches:
                index, needle = min(matches)
                offset = index + len(needle)
                return needle
            if "[FATAL]" in data:
                raise RuntimeError(
                    f"{name}: guest reported a fatal exception while waiting for "
                    f"one of {needles!r}; guest output:\n{data[-6000:]}"
                )
            if qemu.poll() is not None:
                raise RuntimeError(f"{name}: QEMU exited while waiting for {needles!r}")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for one of {needles!r}")

    def wait_for_prompt_count(target_count, timeout=45):
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = content()
            if data.count(prompt) >= target_count:
                return
            if "[FATAL]" in data or "ESR:" in data:
                raise RuntimeError(
                    f"{name}: guest faulted while waiting for the shell prompt; "
                    f"guest output:\n{data[-6000:]}"
                )
            if qemu.poll() is not None:
                raise RuntimeError(f"{name}: QEMU exited while waiting for the shell prompt")
            time.sleep(0.05)
        raise RuntimeError(f"{name}: timed out waiting for {target_count} shell prompts")

    def send_line(line):
        if qemu.stdin is None:
            raise RuntimeError(f"{name}: QEMU monitor closed")
        for key in to_sendkeys(line):
            qemu.stdin.write(key)
            qemu.stdin.flush()
            # Keep a generous inter-key gap for both PS/2 and virtio-input:
            # busy guests can otherwise drop events from the bounded TTY queue.
            time.sleep(0.25)
        time.sleep(0.25)

    prompt = "root@akaOS:/root#"
    try:
        # Native RISC-V/ARM guests can spend over a minute streaming their
        # firmware/device inventory before the TTY is ready, especially when
        # WSL is starting several QEMU instances.  Give the actual boot a
        # generous bound so scheduler/firmware latency is not reported as a
        # functional login failure.
        boot_timeout = int(os.environ.get("AKAOS_TEST_BOOT_TIMEOUT", "300"))
        wait_for("login:", boot_timeout)
        send_line("root")
        wait_for("Password:")
        send_line("root")
        # A native guest can spend a noticeable amount of time faulting in
        # the shell image after authentication when several emulators have
        # just run on a busy host.  Keep the login handshake bounded, but do
        # not mistake that legitimate demand-paged startup for a lost key.
        wait_for(prompt, 90)
        if os.environ.get("AKAOS_TEST_DBUS_REGISTRATION_ONLY") == "1":
            send_line("dbus-daemon --session & sleep 1")
            wait_for(prompt, 20)
            send_line("hello --dbus-registration-test")
            wait_for("D-Bus per-user service registration verified", 45)
            return
        if os.environ.get("AKAOS_TEST_SOCKET_ONLY") == "1":
            send_line("hello --unix-socket-test")
            wait_for("AF_UNIX stream transport verified", 45)
            wait_for("AF_UNIX peer credentials verified", 45)
            wait_for("AF_UNIX SCM_RIGHTS descriptor passing verified", 45)
            wait_for("AF_UNIX SCM_RIGHTS close-on-exec verified", 45)
            wait_for("AF_UNIX socket names, type query, and bidirectional half-close verified", 45)
            return
        if os.environ.get("AKAOS_TEST_WAYLAND_UPSTREAM_ONLY") == "1":
            if name != "x86_64":
                raise RuntimeError("upstream Wayland smoke is currently x86_64-only")
            send_line("wayland-upstream-test")
            wait_for("Upstream Wayland 1.26 registry binding and memfd SHM verified", 90)
            wait_for(prompt, 20)
            return
        if os.environ.get("AKAOS_TEST_QTXDG_ONLY") == "1":
            if name != "x86_64":
                raise RuntimeError("upstream qtxdg-tools is currently x86_64-only")
            qtxdg_timeout = int(os.environ.get("AKAOS_TEST_QTXDG_TIMEOUT", "300"))
            send_line("/usr/local/bin/qtxdg-mat --help")
            wait_for("Available commands:", qtxdg_timeout)
            wait_for(prompt, 20)
            return
        if os.environ.get("AKAOS_TEST_POSIX_ONLY") == "1":
            posix_start_offset = len(content())
            send_line("hello-pi --posix-test")
            wait_for_all((
                "ISO C qsort ordering and duplicate handling verified",
                "POSIX endian constants and byte-swap macros verified",
                "POSIX directory-relative mkdirat/openat/fchdir/renameat/unlinkat verified",
                "POSIX 255-byte NAME_MAX and long-name VFS lookup verified",
                "POSIX AF_UNIX socketpair bidirectional data and flags verified",
                "POSIX wordexp quoting, environment, tilde, glob, and offset semantics verified",
                "POSIX pipe2 atomic CLOEXEC, nonblocking I/O, and flag validation verified",
                "POSIX linkat reports unsupported hard-link capability",
                "POSIX pipe FIONREAD byte availability verified",
                "POSIX resource-limit query and core-disable semantics verified",
                "POSIX sysconf reentrant account buffer limits verified",
                "ISO C bsearch sorted lookup and miss handling verified",
                "POSIX strdup/strndup allocation and contents verified",
                "ISO C atol/atof integer and floating parsing verified",
                "frexp/floor/ceil/log shared math primitives verified",
                "ISO C ldexp scaling and range handling verified",
                "POSIX Cairo math/allocation primitives sqrt/fabs/atan2/exp/sin/log2/log10/exp2/cbrt/nearbyint/fmax/fmin/modf/lrint/fenv/posix_memalign verified",
                "POSIX expm1/log1p, hyperbolic/inverse-hyperbolic, remainder/remquo, copysign, and domain semantics verified",
                "POSIX setjmp/longjmp caller-frame and zero-value semantics verified",
                "POSIX tmpfile anonymous temporary stream verified",
                "POSIX getline/getdelim growth and exclusive mkstemp creation verified",
                "POSIX open_memstream flush and close snapshot verified",
                "POSIX memfd fstat, seek, read/write, and shared mapping verified",
                "POSIX shm_open O_CLOEXEC descriptor semantics verified",
                "POSIX strtok and strtok_r tokenization verified",
                "POSIX memfd growth with active shared mappings verified",
                "POSIX read/write short transfers above the syscall I/O limit verified",
                "POSIX memfd seals, shared mapping, and msync coherence verified",
                "POSIX regular-file ftruncate grow, shrink, zero-fill, and access verified",
                "POSIX IPv4/IPv6 header constants and network-interface layouts verified",
                "POSIX flock shared/exclusive conflicts and close release verified",
                "POSIX fnmatch pathname, class, period, and escape rules verified",
                "ISO C rand/srand sequence and rand_r state verified",
                "ISO C strncat bounded append verified",
                "ISO C strspn/strcspn byte-set scans verified",
                "POSIX ctime/asctime and reentrant variants verified",
                "POSIX difftime arithmetic verified",
                "POSIX TZ offset, localtime, and mktime round-trip verified",
                "POSIX signal-set and mask operations verified",
                "POSIX SA_SIGINFO signal number, code, address and context verified",
                "POSIX SA_SIGINFO SIGCHLD reaping from interruptible poll verified",
                "POSIX waitpid nonzero exit and signal status encoding verified",
                "POSIX signalfd mask, nonblocking read, poll readiness, and signal record verified",
                "POSIX passwd lookup and reentrant account records verified",
                "POSIX close-on-exec child verified",
                "POSIX fcntl descriptor flags and execvp PATH search verified",
                "POSIX spawn child signal state verified",
                "POSIX spawn signal attributes and child inheritance verified",
                "akaOS spawn() environment inheritance verified",
                "POSIX getppid fork ancestry verified",
                "POSIX orphan reparenting and wait ownership verified",
                "POSIX environment vector rebase verified",
                "POSIX spawn file actions verified",
                "POSIX environment and spawn verified",
            ), 45, posix_start_offset)
            wait_for(prompt, 20)
            send_line("hello --mmap-test")
            wait_for(
                "POSIX mmap private and immutable shared-file mappings verified",
                45,
            )
            if os.environ.get("AKAOS_TEST_SKIP_EPOLL", "0") != "1":
                wait_for(prompt, 20)
                send_line("hello --epoll-test")
                wait_for("epoll descriptor readiness verified", 45)
                wait_for(
                    "epoll accepts native descriptors above 16 and 20 simultaneous watches",
                    45,
                )
            wait_for(prompt, 20)
            prior_prompt_count = content().count(prompt)
            send_line("hello --exit-group-test")
            wait_for("POSIX detached worker started before exit-group", 45)
            wait_for_prompt_count(prior_prompt_count + 1, 45)
            if name == "x86_64":
                wait_for(prompt, 20)
                send_line("wayland-upstream-test")
                wait_for("Upstream Wayland 1.26 registry binding and memfd SHM verified", 90)
                wait_for(prompt, 20)
            return
        if os.environ.get("AKAOS_TEST_DESKTOP_RUNTIME_ONLY") == "1":
            send_line("dbus-daemon --session & sleep 1")
            wait_for(prompt, 20)
            send_line("hello --dbus-registration-test")
            wait_for("D-Bus per-user service registration verified", 30)
            wait_for(prompt, 20)
            send_line("hello --desktop-runtime-test")
            runtime_timeout = int(
                os.environ.get("AKAOS_TEST_DESKTOP_RUNTIME_TIMEOUT", "300")
            )
            desktop_result = wait_for_one_of((
                "desktop shared runtime ABI verified",
                "D-Bus async watch state:",
                "desktop runtime test failed",
            ), runtime_timeout)
            if desktop_result != "desktop shared runtime ABI verified":
                wait_for(prompt, 20)
                raise RuntimeError(
                    f"{name}: desktop runtime stopped at {desktop_result!r}; "
                    f"guest output:\n{content()[-6000:]}"
                )
            wait_for("desktop widget tree and activation signals verified", 60)
            wait_for("GApplication lifecycle verified", 30)
            wait_for("GTK window state and lifecycle verified", 30)
            wait_for("Qt core and widget bridge verified", 30)
            wait_for("GAction map and activation verified", 30)
            wait_for(prompt, 20)
            return
        send_line("cxxabi-test")
        wait_for("C++ global constructor entered:", 30)
        wait_for("C++ static stack protector guard initialized", 30)
        wait_for("C++ exception catch and stack-unwind destructor verified", 30)
        wait_for("C++ RTTI catch and dynamic_cast verified", 30)
        wait_for("C++ libc++ containers, strings, and algorithms verified", 30)
        wait_for("C++ global constructor verified", 30)
        wait_for("C++ global new/delete and over-aligned allocation verified", 30)
        wait_for("C++ allocator overflow rejection verified", 30)
        wait_for("C++ std::bad_alloc, new_handler, and nothrow failure verified", 30)
        wait_for("C++ thread-safe local-static initialization verified", 60)
        wait_for("C++ pthread concurrent malloc/realloc/new/delete verified", 90)
        if name == "x86_32":
            wait_for("x86_32 x87 control-state isolation verified", 30)
        wait_for("C++ ABI constructor/guard tests passed", 20)
        wait_for("C++ atexit callback order verified", 20)
        wait_for("C++ local-static destructor finalized", 20)
        wait_for("C++ global destructor finalized last", 20)
        wait_for(prompt, 20)
        send_line("cxx-dynamic-test")
        wait_for("C++ shared stack protector guard initialized", 30)
        wait_for("C++ shared runtime entered main", 90)
        wait_for("C++ shared libc++ containers verified", 30)
        wait_for("C++ shared libc++ numeric formatting verified", 30)
        wait_for("C++ shared std::bad_alloc and new_handler semantics verified", 30)
        wait_for("C++ shared runtime ELF images and unwind headers verified", 30)
        wait_for("C++ shared runtime entering exception probe", 30)
        wait_for("C++ shared runtime catch body entered", 30)
        wait_for("C++ shared-runtime exception/unwind verified", 90)
        wait_for("C++ shared runtime dynamic-link test passed", 30)
        wait_for(prompt, 30)
        send_line("dltest")
        wait_for("dynamic symbol address lookup verified", 30)
        wait_for("dynamic program headers verified", 30)
        wait_for("C++ DSO global constructor ran", 30)
        wait_for("C++ DSO constructor verified", 30)
        wait_for("C++ DSO global destructor ran", 30)
        wait_for("C++ DSO load/close lifecycle verified", 30)
        wait_for("dynamic shared object verified", 30)
        wait_for("dynamic shared object dependency fanout verified (5 providers)", 30)
        wait_for("dynamic shared object capacity verified (12 live handles)", 30)
        wait_for("dynamic shared object per-thread dlerror verified", 30)
        wait_for("dynamic shared object concurrent lifecycle verified (4 workers)", 30)
        wait_for(prompt, 20)
        # Start the bounded session bus as a background job.  Native guests
        # The background command returns before the daemon has necessarily
        # opened its session socket, especially on slower emulated CPUs. Give
        # its startup path a small scheduling window before the first client.
        send_line("dbus-daemon --session & sleep 1")
        wait_for(prompt, 20)
        send_line("hello --dbus-binary-test")
        wait_for("D-Bus binary framing verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-names-test")
        wait_for("D-Bus name service verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-standard-test")
        wait_for("D-Bus standard broker methods verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-endian-test")
        wait_for("D-Bus big-endian frame interoperability verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-marshaller-test")
        wait_for("D-Bus generic signature marshaller verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-match-test")
        wait_for("D-Bus match and name-owner semantics verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-route-test")
        wait_for("D-Bus method routing and UNIX_FD passing verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-activation-test")
        wait_for("D-Bus service activation verified", 30)
        wait_for(prompt, 20)
        send_line("hello --dbus-registration-test")
        wait_for("D-Bus per-user service registration verified", 30)
        wait_for(prompt, 20)
        send_line("hello --pthread-test")
        wait_for("POSIX executable TLS isolation verified", 60)
        wait_for("POSIX per-thread errno isolation verified", 60)
        wait_for("POSIX monotonic condition-variable clock selection verified", 30)
        wait_for("pthread_atfork ordering and post-fork thread state verified", 10)
        wait_for("POSIX initial and worker pthread stack bounds verified", 10)
        wait_for("pthread compatibility and shared thread verified (attributes, rwlock, timed wait, semaphore, barrier, cleanup, cancellation, 24-worker scheduler scaling, blocking and tick-rounded futex waits)", 30)
        wait_for(prompt, 20)
        send_line("hello --shared-memory-test")
        wait_for("Process-shared futex blocking and wake verified", 30)
        wait_for("Process-shared pthread sync verified", 30)
        wait_for(prompt, 20)
        send_line("hello --unix-socket-test")
        wait_for("AF_UNIX stream transport verified", 30)
        wait_for("AF_UNIX peer credentials verified", 10)
        wait_for("AF_UNIX SCM_RIGHTS descriptor passing verified", 10)
        wait_for("AF_UNIX SCM_RIGHTS close-on-exec verified", 10)
        wait_for("AF_UNIX socket names, type query, and bidirectional half-close verified", 20)
        wait_for(prompt, 20)
        send_line("hello --select-test")
        wait_for("select() descriptor readiness verified", 30)
        wait_for(prompt, 20)
        if os.environ.get("AKAOS_TEST_SKIP_EPOLL", "0") != "1":
            send_line("hello --epoll-test")
            wait_for("epoll descriptor readiness verified", 30)
            wait_for(
                "epoll accepts native descriptors above 16 and 20 simultaneous watches",
                30,
            )
            wait_for(prompt, 20)
        if os.environ.get("AKAOS_TEST_SKIP_EVENTFD", "0") != "1":
            send_line("hello --eventfd-test")
            wait_for("eventfd sixteen concurrent handles verified", 30)
            wait_for("eventfd readiness and counter semantics verified", 30)
            wait_for(prompt, 20)
        if os.environ.get("AKAOS_TEST_SKIP_TIMERFD", "0") != "1":
            send_line("hello --timerfd-test")
            wait_for("timerfd readiness, interval, and disarm semantics verified", 30)
            wait_for(prompt, 20)
        send_line("hello --getaddrinfo-test")
        wait_for("getaddrinfo() IPv4 resolution verified", 30)
        wait_for(prompt, 20)
        send_line("hello --locale-test")
        wait_for("locale and UTF-8 compatibility verified", 30)
        wait_for("POSIX nl_langinfo locale codeset verified", 30)
        wait_for(prompt, 20)
        posix_start_offset = len(content())
        send_line("hello-pi --posix-test")
        wait_for_all((
            "POSIX 255-byte NAME_MAX and long-name VFS lookup verified",
            "POSIX AF_UNIX socketpair bidirectional data and flags verified",
            "ISO C qsort ordering and duplicate handling verified",
            "ISO C bsearch sorted lookup and miss handling verified",
            "POSIX strdup/strndup allocation and contents verified",
            "ISO C atol/atof integer and floating parsing verified",
            "frexp/floor/ceil/log shared math primitives verified",
            "ISO C ldexp scaling and range handling verified",
            "POSIX Cairo math/allocation primitives sqrt/fabs/atan2/exp/sin/log2/log10/exp2/cbrt/nearbyint/fmax/fmin/modf/lrint/fenv/posix_memalign verified",
            "POSIX expm1/log1p, hyperbolic/inverse-hyperbolic, remainder/remquo, copysign, and domain semantics verified",
            "POSIX setjmp/longjmp caller-frame and zero-value semantics verified",
            "POSIX tmpfile anonymous temporary stream verified",
            "POSIX getline/getdelim growth and exclusive mkstemp creation verified",
            "POSIX open_memstream flush and close snapshot verified",
            "POSIX memfd fstat, seek, read/write, and shared mapping verified",
            "POSIX shm_open O_CLOEXEC descriptor semantics verified",
            "POSIX strtok and strtok_r tokenization verified",
            "POSIX memfd growth with active shared mappings verified",
            "POSIX read/write short transfers above the syscall I/O limit verified",
            "POSIX memfd seals, shared mapping, and msync coherence verified",
            "POSIX regular-file ftruncate grow, shrink, zero-fill, and access verified",
            "POSIX IPv4/IPv6 header constants and network-interface layouts verified",
            "POSIX flock shared/exclusive conflicts and close release verified",
            "POSIX fnmatch pathname, class, period, and escape rules verified",
            "ISO C rand/srand sequence and rand_r state verified",
            "ISO C strncat bounded append verified",
            "ISO C strspn/strcspn byte-set scans verified",
            "POSIX ctime/asctime and reentrant variants verified",
            "POSIX difftime arithmetic verified",
            "POSIX TZ offset, localtime, and mktime round-trip verified",
            "POSIX signal-set and mask operations verified",
            "POSIX SA_SIGINFO signal number, code, address and context verified",
            "POSIX signalfd mask, nonblocking read, poll readiness, and signal record verified",
            "POSIX passwd lookup and reentrant account records verified",
            "POSIX close-on-exec child verified",
            "POSIX fcntl descriptor flags and execvp PATH search verified",
            "POSIX spawn child signal state verified",
            "POSIX spawn signal attributes and child inheritance verified",
            "akaOS spawn() environment inheritance verified",
            "POSIX getppid fork ancestry verified",
            "POSIX orphan reparenting and wait ownership verified",
            "POSIX environment vector rebase verified",
            "POSIX spawn file actions verified",
            "POSIX environment and spawn verified",
        ), 90, posix_start_offset)
        wait_for(prompt, 20)
        send_line("hello --display-protocol-test")
        wait_for("X11 window lifecycle verified", 30)
        wait_for("X11 protocol handshake verified", 30)
        wait_for("Wayland xdg-shell negotiation verified", 30)
        wait_for("Wayland protocol handshake verified", 30)
        wait_for(prompt, 20)
        if name == "x86_64" and os.environ.get("AKAOS_TEST_QTXDG_TOOLS") == "1":
            qtxdg_timeout = int(os.environ.get("AKAOS_TEST_QTXDG_TIMEOUT", "300"))
            send_line("/usr/local/bin/qtxdg-mat --help")
            wait_for("Available commands:", qtxdg_timeout)
            wait_for(prompt, 20)
        if name == "x86_64":
            send_line("wayland-upstream-test")
            wait_for("Upstream Wayland 1.26 registry binding and memfd SHM verified", 90)
            wait_for(prompt, 20)
        # Confirm the session bus still answers after the long protocol/runtime
        # probes; do not restart it and hide a session-lifetime regression.
        send_line("hello --dbus-registration-test")
        wait_for("D-Bus per-user service registration verified", 30)
        wait_for(prompt, 20)
        send_line("hello --desktop-runtime-test")
        # The complete GLib/GIO/GTK/Qt exercise is CPU-heavy under the native
        # RISC-V QEMU translators. Bound it generously enough to test guest
        # behavior rather than host emulation throughput.
        wait_for("desktop widget tree and activation signals verified", 120)
        wait_for("GApplication lifecycle verified", 30)
        wait_for("GTK window state and lifecycle verified", 30)
        wait_for("Qt core and widget bridge verified", 30)
        wait_for("GAction map and activation verified", 30)
        wait_for(prompt, 20)
        send_line("echo rtc_begin_%s" % name)
        wait_for("rtc_begin_%s" % name, 20)
        wait_for(prompt, 20)
        send_line("cat /proc/realtime")
        wait_for(prompt, 20)
        send_line("echo rtc_end_%s" % name)
        wait_for("rtc_end_%s" % name, 20)
        wait_for(prompt, 20)
        send_line("echo compositor_client_ipc_%s_verified" % name)
        wait_for("compositor_client_ipc_%s_verified" % name, 20)
        wait_for(prompt, 20)
        # Enter the complete sequence before Lumen takes exclusive ownership
        # of keyboard input.  This directly exercises one real compositor
        # client's attach, damage and destroy lifecycle without depending on
        # the desktop shell's launcher policy.
        send_line("hello --dbus-properties-signal-test")
        wait_for("D-Bus PropertiesChanged signals verified", 60)
        wait_for(prompt, 20)
        send_line("lumen --input-test & sleep 1; "
                  "openbox --component & sleep 2; "
                  "hello --dbus-desktop-service-test; pkill openbox; "
                  "xsdisplay --check; hello --toolkit-test; palette --resize-test")
        # Native ARM/RISC-V guests can briefly yield while the resident
        # desktop role registers its bus name.  Give the first routed call a
        # two minutes so the matrix does not fail solely on guest startup
        # jitter; subsequent protocol checks remain bounded at 20 seconds.
        wait_for("D-Bus desktop service routing verified", 120)
        wait_for("XSDisplay READY", 20)
        wait_for("toolkit display bridge verified", 20)
        wait_for("palette: queued surface", 20)
        wait_for("palette: resize configure queued", 20)
        wait_for("palette: resize configure acknowledged", 20)
        wait_for("palette: damage update queued", 20)
        wait_for("palette: destroy queued", 20)
        qemu.stdin.write("mouse_move 8 8\n")
        qemu.stdin.flush()
        wait_for("lumen: mouse motion verified", 20)
        qemu.stdin.write("mouse_button 1\nmouse_button 0\n")
        qemu.stdin.flush()
        wait_for("lumen: mouse button verified", 20)
        time.sleep(0.5)
    finally:
        if qemu.poll() is None:
            qemu.kill()
        qemu.wait()
    data = content()
    if "[FATAL]" in data:
        raise RuntimeError(f"{name}: compositor test emitted a fatal trap")
    if os.environ.get("AKAOS_TEST_POSIX_ONLY") == "1":
        return
    import re
    if not re.search(r"\n1[0-9]{9}\r?\n", data):
        raise RuntimeError(f"{name}: /proc/realtime did not return a current epoch")

for name, (qemu_bin, kernel_args) in targets.items():
    print(f"[TEST] {name}: compositor queue + palette attach/damage/destroy", flush=True)
    run_target(name, qemu_bin, kernel_args)
    print(f"[PASS] {name}: compositor client lifecycle verified", flush=True)

print("[PASS] Compositor IPC lifecycle verification passed on " + ", ".join(targets) + ".")
PY

test_arches="${AKAOS_TEST_ARCHES:-x86_64 x86_32 aarch64 arm32 riscv64 riscv32}"
for arch in $test_arches; do
    log="$TEST_LOG_DIR/test-compositor-clients-$arch.log"
    echo "[OUTPUT] $arch compositor transcript: $log"
    cat "$log"
    if [[ "${AKAOS_TEST_DESKTOP_RUNTIME_ONLY:-0}" == 1 ]]; then
        grep -Fq "desktop shared runtime ABI verified" "$log"
        grep -Fq "desktop widget tree and activation signals verified" "$log"
        grep -Fq "GApplication lifecycle verified" "$log"
        grep -Fq "GTK window state and lifecycle verified" "$log"
        grep -Fq "Qt core and widget bridge verified" "$log"
        grep -Fq "GAction map and activation verified" "$log"
        continue
    fi
    if [[ "${AKAOS_TEST_DBUS_REGISTRATION_ONLY:-0}" == 1 ]]; then
        grep -Fq "D-Bus per-user service registration verified" "$log"
        continue
    fi
    if [[ "${AKAOS_TEST_SOCKET_ONLY:-0}" == 1 ]]; then
        grep -Fq "AF_UNIX socket names, type query, and bidirectional half-close verified" "$log"
        continue
    fi
    if [[ "${AKAOS_TEST_POSIX_ONLY:-0}" == 1 ]]; then
        grep -Fq "POSIX fcntl descriptor flags and execvp PATH search verified" "$log"
        grep -Fq "POSIX AF_UNIX socketpair bidirectional data and flags verified" "$log"
        grep -Fq "POSIX wordexp quoting, environment, tilde, glob, and offset semantics verified" "$log"
        grep -Fq "POSIX pipe2 atomic CLOEXEC, nonblocking I/O, and flag validation verified" "$log"
        grep -Fq "POSIX spawn signal attributes and child inheritance verified" "$log"
        grep -Fq "POSIX signal-set and mask operations verified" "$log"
        grep -Fq "POSIX SA_SIGINFO SIGCHLD reaping from interruptible poll verified" "$log"
        grep -Fq "POSIX waitpid nonzero exit and signal status encoding verified" "$log"
        grep -Fq "POSIX signalfd mask, nonblocking read, poll readiness, and signal record verified" "$log"
        grep -Fq "POSIX open_memstream flush and close snapshot verified" "$log"
        grep -Fq "POSIX getline/getdelim growth and exclusive mkstemp creation verified" "$log"
        grep -Fq "POSIX memfd seals, shared mapping, and msync coherence verified" "$log"
        grep -Fq "POSIX regular-file ftruncate grow, shrink, zero-fill, and access verified" "$log"
        grep -Fq "POSIX IPv4/IPv6 header constants and network-interface layouts verified" "$log"
        grep -Fq "POSIX memfd fstat, seek, read/write, and shared mapping verified" "$log"
        grep -Fq "POSIX shm_open O_CLOEXEC descriptor semantics verified" "$log"
        grep -Fq "POSIX strtok and strtok_r tokenization verified" "$log"
        grep -Fq "POSIX read/write short transfers above the syscall I/O limit verified" "$log"
        continue
    fi
    grep -Fq "palette: queued surface" "$log"
    grep -Fq "palette: resize configure queued" "$log"
    grep -Fq "palette: resize configure acknowledged" "$log"
    grep -Fq "palette: damage update queued" "$log"
    grep -Fq "palette: destroy queued" "$log"
    grep -Fq "lumen: mouse motion verified" "$log"
    grep -Fq "lumen: mouse button verified" "$log"
    grep -Fq "XSDisplay READY" "$log"
    grep -Fq "toolkit display bridge verified" "$log"
    grep -Fq "compositor_client_ipc_${arch}_verified" "$log"
    grep -Fq "dynamic shared object verified" "$log"
    grep -Fq "C++ global constructor verified" "$log"
    grep -Fq "C++ std::bad_alloc, new_handler, and nothrow failure verified" "$log"
    grep -Fq "C++ shared std::bad_alloc and new_handler semantics verified" "$log"
    grep -Fq "C++ thread-safe local-static initialization verified" "$log"
    grep -Fq "C++ atexit callback order verified" "$log"
    grep -Fq "C++ local-static destructor finalized" "$log"
    grep -Fq "C++ global destructor finalized last" "$log"
    grep -Fq "C++ DSO global constructor ran" "$log"
    grep -Fq "C++ DSO constructor verified" "$log"
    grep -Fq "C++ DSO global destructor ran" "$log"
    grep -Fq "C++ DSO load/close lifecycle verified" "$log"
    grep -Fq "dynamic shared object per-thread dlerror verified" "$log"
    grep -Fq "dynamic shared object concurrent lifecycle verified (4 workers)" "$log"
    grep -Fq "D-Bus binary framing verified" "$log"
    grep -Fq "D-Bus name service verified" "$log"
    grep -Fq "D-Bus standard broker methods verified" "$log"
    grep -Fq "D-Bus big-endian frame interoperability verified" "$log"
    grep -Fq "D-Bus generic signature marshaller verified" "$log"
    grep -Fq "D-Bus match and name-owner semantics verified" "$log"
    grep -Fq "D-Bus method routing and UNIX_FD passing verified" "$log"
    grep -Fq "Wayland xdg-shell negotiation verified" "$log"
    grep -Fq "desktop widget tree and activation signals verified" "$log"
    grep -Fq "GApplication lifecycle verified" "$log"
    grep -Fq "GTK window state and lifecycle verified" "$log"
    grep -Fq "Qt core and widget bridge verified" "$log"
    grep -Fq "GAction map and activation verified" "$log"
    grep -Fq "D-Bus desktop service routing verified" "$log"
    grep -Fq "D-Bus service activation verified" "$log"
    grep -Fq "D-Bus per-user service registration verified" "$log"
    grep -Fq "POSIX per-thread errno isolation verified" "$log"
    grep -Fq "POSIX executable TLS isolation verified" "$log"
    grep -Fq "POSIX monotonic condition-variable clock selection verified" "$log"
    grep -Fq "POSIX initial and worker pthread stack bounds verified" "$log"
      grep -Fq "pthread compatibility and shared thread verified (attributes, rwlock, timed wait, semaphore, barrier, cleanup, cancellation, 24-worker scheduler scaling, blocking and tick-rounded futex waits)" "$log"
    grep -Fq "AF_UNIX stream transport verified" "$log"
    grep -Fq "AF_UNIX socket names, type query, and bidirectional half-close verified" "$log"
    grep -Fq "select() descriptor readiness verified" "$log"
    if [[ "${AKAOS_TEST_SKIP_EPOLL:-0}" != 1 ]]; then
        grep -Fq "epoll descriptor readiness verified" "$log"
    fi
    if [[ "${AKAOS_TEST_SKIP_EVENTFD:-0}" != 1 ]]; then
        grep -Fq "eventfd sixteen concurrent handles verified" "$log"
        grep -Fq "eventfd readiness and counter semantics verified" "$log"
    fi
    if [[ "${AKAOS_TEST_SKIP_TIMERFD:-0}" != 1 ]]; then
        grep -Fq "timerfd readiness, interval, and disarm semantics verified" "$log"
    fi
    grep -Fq "getaddrinfo() IPv4 resolution verified" "$log"
    grep -Fq "locale and UTF-8 compatibility verified" "$log"
    grep -Fq "POSIX resource-limit query and core-disable semantics verified" "$log"
    grep -Fq "POSIX sysconf reentrant account buffer limits verified" "$log"
    grep -Fq "POSIX spawn file actions verified" "$log"
    grep -Fq "POSIX getppid fork ancestry verified" "$log"
    grep -Fq "POSIX orphan reparenting and wait ownership verified" "$log"
    grep -Fq "POSIX environment and spawn verified" "$log"
    grep -Fq "ISO C qsort ordering and duplicate handling verified" "$log"
    grep -Fq "ISO C bsearch sorted lookup and miss handling verified" "$log"
    grep -Fq "POSIX strdup/strndup allocation and contents verified" "$log"
    grep -Fq "ISO C rand/srand sequence and rand_r state verified" "$log"
    grep -Fq "ISO C atol/atof integer and floating parsing verified" "$log"
    grep -Fq "frexp/floor/ceil/log shared math primitives verified" "$log"
    grep -Fq "ISO C ldexp scaling and range handling verified" "$log"
    grep -Fq "POSIX Cairo math/allocation primitives sqrt/fabs/atan2/exp/sin/log2/log10/exp2/cbrt/nearbyint/fmax/fmin/modf/lrint/fenv/posix_memalign verified" "$log"
    grep -Fq "POSIX expm1/log1p, hyperbolic/inverse-hyperbolic, remainder/remquo, copysign, and domain semantics verified" "$log"
    grep -Fq "POSIX fnmatch pathname, class, period, and escape rules verified" "$log"
    grep -Fq "ISO C strncat bounded append verified" "$log"
    grep -Fq "ISO C strspn/strcspn byte-set scans verified" "$log"
    grep -Fq "POSIX ctime/asctime and reentrant variants verified" "$log"
    grep -Fq "POSIX difftime arithmetic verified" "$log"
    grep -Fq "POSIX TZ offset, localtime, and mktime round-trip verified" "$log"
    grep -Fq "POSIX signal-set and mask operations verified" "$log"
    grep -Fq "POSIX signalfd mask, nonblocking read, poll readiness, and signal record verified" "$log"
    grep -Fq "POSIX open_memstream flush and close snapshot verified" "$log"
    grep -Fq "POSIX getline/getdelim growth and exclusive mkstemp creation verified" "$log"
    grep -Fq "POSIX memfd seals, shared mapping, and msync coherence verified" "$log"
    grep -Fq "POSIX regular-file ftruncate grow, shrink, zero-fill, and access verified" "$log"
    grep -Fq "POSIX IPv4/IPv6 header constants and network-interface layouts verified" "$log"
    grep -Fq "POSIX memfd fstat, seek, read/write, and shared mapping verified" "$log"
    grep -Fq "POSIX shm_open O_CLOEXEC descriptor semantics verified" "$log"
    grep -Fq "POSIX strtok and strtok_r tokenization verified" "$log"
    grep -Fq "POSIX memfd growth with active shared mappings verified" "$log"
    grep -Fq "POSIX read/write short transfers above the syscall I/O limit verified" "$log"
    grep -Fq "POSIX passwd lookup and reentrant account records verified" "$log"
    grep -Fq "POSIX close-on-exec child verified" "$log"
    grep -Fq "POSIX fcntl descriptor flags and execvp PATH search verified" "$log"
    grep -Fq "POSIX spawn child signal state verified" "$log"
    grep -Fq "POSIX spawn signal attributes and child inheritance verified" "$log"
    grep -Fq "akaOS spawn() environment inheritance verified" "$log"
    grep -Fq "X11 window lifecycle verified" "$log"
    grep -Fq "X11 protocol handshake verified" "$log"
    grep -Fq "Wayland protocol handshake verified" "$log"
    awk '{ sub(/\r$/, ""); if ($0 ~ /^1[0-9]{9}$/) found = 1 } END { exit !found }' "$log"
done

echo "[PASS] akaOS compositor client IPC and lifecycle tests passed."
