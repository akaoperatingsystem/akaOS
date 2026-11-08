# akaOS Persistent Project Context

## Project identity

akaOS is an original, independently implemented operating system. Its kernel is named **XSCore**. Boot-tested native paths exist for x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32. The long-term goal is a modern Unix-like desktop operating system with its own kernel, userspace, graphics stack, services, desktop, and applications.

akaOS is **not Linux**. XSCore is **not the Linux kernel**. Do not import, embed, or replace XSCore with Linux, and do not turn akaOS into a Linux distribution. Unix and POSIX ideas may guide interfaces and behavior, but akaOS implementations must be original or use deliberately selected, license-compatible components without making Linux a runtime dependency.

The full boot target is x86-64. It supports both UEFI and legacy BIOS firmware:
UEFI uses OVMF and BIOS uses a GRUB Multiboot2 ISO. The development machine
uses the **Ubuntu WSL distribution** only as a host environment, and QEMU is
the primary test platform. On Windows, always select it explicitly with
`wsl.exe -d Ubuntu`; the machine's default WSL distribution may be Arch Linux
and must not be used for akaOS work. `tools/build-arch-smoke.sh` is the
supported compile/link check for the other five CPU targets.

## Native Package Manager (pkg) & External HTTP Mirror (2026-11-02)

akaOS provides a native package management subsystem (`/bin/pkg`) configured by default to connect to the external HTTP mirror at `http://mirror.zyqu.pl/akaos` over HTTP port 80. Software packages use the `.akp` format (POSIX ustar tarball) containing `.PKGINFO` manifests, optional `CONTROL/postinst` and `CONTROL/prerm` lifecycle scripts, and payload files. Local databases in `/var/lib/pkg/` track repository metadata (`packages.db`), installed packages (`installed.db`), and file manifests (`files/<pkg>.list`). The package manager verifies SHA-256 integrity, resolves dependencies recursively, and uses atomic writes for reliability. Host packaging is managed via `tools/aka-makepkg.py`, and an automated test harness is provided in `tools/test-pkg.py`.

## Latest POSIX spawn validation (2026-09-27)

Libc passes XSCore the `posix_spawn_file_actions_t.actions` array directly.
The QEMU POSIX regression redirects a spawned child's stdout through a pipe
using `dup2` and close actions, then checks the captured output. The fresh
2026-09-28 compositor/client suite passed this behavior on all six architectures.
XSCore currently retains exited-process descriptors until the parent reaps the
child, so this test reaps before draining the small output. This is bounded
native behavior, not complete POSIX or stock KDE/GNOME/LXDE/LXQt compatibility.

## Latest verified shared C++ runtime probes (2026-09-29)

The libc heap's global block list is protected across `malloc`, `free`, and
`realloc`; atfork callbacks lock the parent around fork and reset inherited
ownership in the child. Size arithmetic is overflow-checked. libc provides
the bounded Itanium ABI startup/guard/finalization foundation and a fallback
`<new>` declaration header; global allocation operators are supplied by the
linked upstream libc++abi runtime rather than XSCore libc.
Pinned upstream LLVM 21.1.8 now builds static `libc++.a`, `libc++abi.a`, and
`libunwind.a` plus libc++ headers for x86-64, x86-32, AArch64, ARM32, RISC-V64,
and RISC-V32. The six native C++ builders link the archives into
`cxxabi-test`, which now exercises `std::string`, `std::vector`, and
`std::sort` in addition to the bounded Itanium ABI, exception/unwind, RTTI,
allocator, and DSO probes. The complete akaOS-native compositor/client guest
suite passed on all six targets in grouped and per-target runs; every final
transcript contains the libc++ marker. ARM32's targeted compiler-rt archive
includes soft-float EABI comparisons; RISC-V32/RISC-V64 include the required
single/double soft-float comparison helpers. The AArch64 test harness now
allows one second for the background D-Bus session daemon to schedule before
its first client.

The newer shared-runtime path now builds versioned `libunwind.so.1`,
`libc++abi.so.1`, and `libc++.so.1` DSOs for all six architectures and
packages them with the dynamic `cxx-dynamic-test` executable. The native ELF
loader now exposes program-header iteration and address lookup, retains main
image PHDR data, shares its loaded-object/static-TLS namespace with the ELF
interpreter, and resolves the runtime's relocation families. The test exercises
containers, floating-point string conversion, DSO enumeration and unwind
headers, and an exception caught across the shared C++ runtime. Fresh
`build.sh`/cross-builder builds, `tools/test-dynamic-loader.sh`, and the full
`tools/test-compositor-clients.sh` passed on x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32 on 2026-09-28.

Upstream LLVM `libc++abi` now owns the global allocation/deallocation
operators; XSCore libc no longer overrides them with fail-fast behavior.
Static and dynamic C++ guest tests force allocation failure and verify
`std::bad_alloc`, one-shot `std::new_handler` retry, and null from nothrow
`new` on all six targets. The final fresh Ubuntu WSL build and full
`tools/test-compositor-clients.sh` matrix passed on x86_64, x86_32, AArch64,
ARM32, RISC-V64, and RISC-V32; the strict `tools/test-dynamic-loader.sh`
matrix also passed on all six.

The RISC-V futex test now separates 1 ms timeout-result checks from a 50 ms
blocking check because `CLOCK_MONOTONIC` currently derives from centisecond-
resolution `/proc/uptime`. The compositor harness waits two seconds after
starting its Openbox compatibility component before the D-Bus route probe;
the final all-six run passed. These are akaOS-native probes, not a complete
upstream C++/POSIX ABI or desktop port. The `libgtk`/`libglib`/`libQt6*`
objects remain akaOS compatibility implementations, and unmodified
KDE/GNOME/LXDE/LXQt execution remains unverified.

## Latest verified dynamic-runtime substrate (2026-09-24)

The libc `dlfcn` ABI now includes standard `RTLD_DEFAULT`/`RTLD_NEXT` handles.
Default and versioned global lookups walk the bounded loaded-object table and
the built-in TLS resolver; `RTLD_NEXT` begins after the caller's mapped DSO
when identifiable. Explicit handles recurse through transitive `DT_NEEDED`
dependencies with cycle protection, and `dltest` covers that provider path as
well as both global-scope forms. `RTLD_LOCAL`/`RTLD_GLOBAL` now control
process-wide visibility and promote an existing object when reopened
globally. The clean Ubuntu
WSL six-port loader matrix and complete compositor/client lifecycle suite
remain green; the loader probe also closes/reloads a TLS-bearing DSO and
verifies fresh template initialization. This improves plugin probing but does not make unmodified
upstream KDE/GNOME/LXDE/LXQt binaries runnable yet.

The bounded native loader now handles compiler-generated PT_TLS general-dynamic
access on all six ports. AArch64 TLSDESC descriptors use an x8-preserving
resolver shim, while RISC-V32/RISC-V64 use the LLVM DTPMOD/DTPREL relocation
numbers. Clean Ubuntu WSL rebuilds and the strict live dynamic-loader matrix
pass on x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32, including DSO
constructors, DT_NEEDED dependencies, dlsym TLS addresses, and worker-thread
isolation. The same matrix now also exercises bounded `DT_VERSYM`/
`DT_VERDEF`/`DT_VERNEED` selection, explicit `dlvsym()` calls, and a real
versioned `DT_NEEDED` relocation. Static
TLS/TP access is also implemented as a bounded static `PT_TLS` path: per-thread
64 KiB areas, architecture-appropriate TP-relative relocations, aligned
first-fit DSO ranges, template refresh for existing threads, and offset/module
count reclamation on final `dlclose`. The loader matrix checks worker
isolation, a 64-byte-aligned 8 KiB TLS object, and 12 close/reload cycles on
all six ports. Full pthread TLS/NPTL semantics and unmodified
KDE/GNOME/LXDE/LXQt binaries remain future work. Fresh Ubuntu WSL images also
pass the complete six-port compositor/client lifecycle suite; this is native
akaOS test evidence, not an upstream package compatibility claim. The same
fresh x86_64 UEFI guest passes desktop readiness and reports Aurora, LXDE,
LXQt, GNOME, and KDE Plasma as ready native profiles.

## Latest verified desktop substrate (2026-09-19)

The native desktop compatibility layer now has bounded GIO file-content
read/write/existence and asynchronous load/finish operations backed by VFS
syscalls, directory enumeration with `GFileInfo` metadata backed by native
`getdents`/`stat`, canonical shared-DSO alias loading, and a first real dynamic
ELF execution path: XSCore stages `/lib/ld-akaos.so` for bounded `PT_INTERP`
images, preserves `AT_EXECFN`, and the interpreter jumps through
`akaos_dl_entry()` after resolving a `DT_NEEDED` provider. Generated builders link
the shared desktop I/O bridge on all ports, and `/bin/ls` avoids unavailable
32-bit u64 division helpers. The clean Ubuntu WSL compositor/client matrix
passes end to end on x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32,
including D-Bus, POSIX, X11/Wayland, toolkit, GIO, compositor, palette, and
mouse-input checks. Treat this as the current native substrate milestone;
unmodified upstream KDE, GNOME, LXDE, and LXQt remain future integration work.

The same native loader now executes bounded DSO lifecycle callbacks: it parses
`DT_INIT`/`DT_FINI` and init/fini arrays, runs dependency constructors after
relocation, and finalizes objects before their last unmap. A constructor-bearing
demo DSO is checked by `dltest`, and the dynamic-loader matrix passes on all
six ports. Complete auxv/HWCAP values and broader upstream TLS semantics are
still open layers.

XSCore now also emits the standard bounded ELF auxiliary vector on new user
stacks (`AT_PHDR`/`AT_PHENT`/`AT_PHNUM`, page size, entry/base/flags,
credentials, secure mode, `AT_EXECFN`, and `AT_NULL`). The native interpreter
validates these entries, and fresh images pass the six-port dynamic-loader
matrix. Entropy/platform/HWCAP entries and full upstream TLS/auxv behavior
remain future work.

## Important Naming Rule: Keep XSCore and akaOS Separate

“akaOS is the operating system. XSCore is the kernel. Do not use akaOS as a generic prefix throughout XSCore.”

- **akaOS**: Represents the operating system as a whole (product, desktop environment, settings, terminal app, applications, distribution identity, branding).
- **XSCore**: Represents the kernel. Inside kernel code (`xscore/`), prefer clean modular names: `XSCore`, `kernel`, `core`, `arch`, `memory`, `scheduler`, `process`, `thread`, `vfs`, `syscall`, `interrupt`, `timer`, `driver`. Do NOT prefix kernel subsystems with `akaOS*` (e.g., avoid `akaOSKernel`, `akaOSMemory`, `akaOSScheduler`, `akaOSProcess`, `akaOSVFS`).
- **Userspace Ecosystem**: Choose distinct, independent names for new applications, tools, and services (e.g., `ted`, `akash`). Do NOT force the `aka` prefix onto everything (avoid `akaTerminal`, `akaFileManager`, `akaEditor`, `akaBrowser`, `akaSystem`). Established working names are preserved.

## Persistent-memory purpose

The `AI/` directory is the persistent project memory for AI models and human developers. Conversation context is temporary; repository state is persistent. These files must stay provider-neutral and useful to GPT, Claude, Gemini, other models, and humans.

Source code is the final source of truth. Documentation must never be used to justify a claim that the implementation does not support.

## How an AI should take over akaOS

Before modifying anything, a new AI must:

1. Read `AI/CONTEXT.md`.
2. Read `AI/CURRENT_STATE.md`.
3. Read `AI/ARCHITECTURE.md`.
4. Read `AI/TODO.md`.
5. Read the latest entries in `AI/CHANGELOG.md`.
6. Read `AI/DECISIONS.md`.
7. Read `AI/HANDOFF.md`.
8. Inspect Git status. If this directory is still not a Git repository, record that fact rather than pretending history exists.
9. Inspect recent Git commits when Git history exists.
10. Inspect the relevant source code.
11. Verify that the documentation matches the source.
12. Continue from the current milestone.

Do not restart akaOS. Do not assume the documentation is always correct. If documentation conflicts with implementation, inspect the implementation, determine the real state, correct `AI/`, and only then continue.

## Current desktop-session status (2026-09-17)

The native composed-session bridge now registers distinct D-Bus activation
metadata and claims matching well-known names for resident LXDE, LXQt, GNOME,
and KDE companion roles, releasing and cleaning up only entries it owns. The
broker also answers the bounded standard owner/credential/machine-ID,
introspection, activation, and ping calls with explicit errors, while
resident roles expose status/capabilities/introspection/ping. All six
architecture images and the x86_64 live desktop-readiness path were rebuilt
and verified under Ubuntu WSL, including standard-broker and routed
desktop-service probes. This is still an akaOS-native compatibility layer;
full upstream desktop environments remain an open implementation target.

The latest clean verification also hardens the ARM32 libc `poll()` wrapper:
translated `revents` are copied by a no-inline helper after a blocking SVC so
the scheduler cannot invalidate an r14-held stack pointer. The canonical TTY
input buffer remains 1024 bytes. The six-port compositor/client lifecycle
matrix and x86_64 desktop readiness both pass with temporary diagnostics
removed. This is a native substrate milestone, not proof that unmodified
upstream KDE, GNOME, LXDE, or LXQt binaries are 100% runnable.

## Current high-level architecture

The implemented system currently consists of:

- A freestanding UEFI loader in `boot/uefi`.
- A separate GRUB Multiboot2 legacy-BIOS handoff in `xscore/arch/x86_64` and
  `xscore/kernel/bios.c`, sharing the plain `akaos_boot_info` contract.
- A versioned loader-to-kernel handoff ABI in `sdk/include/akaos/boot_info.h`.
- The XSCore kernel in `xscore`, including CPU tables, exceptions, paging, physical memory, heap, protected kernel stacks, Local APIC, APIC timer, bounded PCI discovery, legacy VirtIO and BIOS ATA PIO sector I/O, a persistent akaFS VFS overlay, cooperative kernel threads, private per-process page-table roots, process-owned descriptor tables, a reusable process lifecycle, dynamic VFS/initramfs archive loading, pathname ELF execution, a syscall gate, kernel TTY support, boot graphics, and the userspace `/bin/akash` shell launcher.
- A tiny standalone native C Ring 3 test program, native utility suite, userspace
  shell, and minimal libc/runtime in `userspace`.
- Build, QEMU, test, font-generation, capture, and shared interactive terminal
  configuration tooling in the repository root and `tools`.

There is a read-only initramfs seed, bounded writable and checksummed akaFS
persistence layer, minimal native libc/runtime subset, bounded pipe IPC, PID 1,
credentials, signals/job control, a mount namespace table, hardware-backed
VirtIO networking, BSD-style sockets, the Lumen compositor, Aurora desktop,
graphical terminal, and native applications. These remain deliberately bounded
original akaOS implementations rather than Linux components.

## Actual repository layout

- `AI/`: persistent project memory and handoff documents.
- `boot/uefi/`: UEFI loader and local UEFI protocol definitions.
- `xscore/arch/x86_64/`: GDT/IDT/TSS, assembly entry paths, context switching, APIC, timer, stacks, linker script.
- `xscore/kernel/`: kernel entry, boot UI, TTY, scheduler accounting, kernel threads, descriptor core, reusable internal process lifecycle, syscalls, and Ring 3 self-test.
- `xscore/memory/`: physical page allocator, virtual memory manager, kernel heap.
- `xscore/include/xscore/`: XSCore subsystem interfaces.
- `sdk/include/akaos/`: shared stable boot handoff definitions.
- `userspace/`: native startup code, minimal libc headers/sources, user linker script, userspace `/bin/akash` shell, and separate native utility C programs.
- `tools/`: host-side dependency/resource discovery, font generation, QEMU
  helpers, screenshot helpers, BIOS image/test helpers, and
  the six-target architecture smoke builder (`build-arch-smoke.sh`,
  `arch-smoke/`).
- `docs/`: existing milestone documentation; some sections are older than the source and must be checked before reuse.
- `build/`: ignored generated objects, images, firmware-variable copies, logs, screenshots, and generated font headers.

Planned directories such as `graphics/`, `ui/`, `desktop/`, `applications/`, and a dedicated `tests/` tree do not currently exist. Create them only when real implementation work requires them.

## Development philosophy

- Implement real mechanisms and test them; never print fake progress.
- Keep serial diagnostics available even when the graphical boot is quiet.
- Separate debug logging from visual boot and console rendering.
- Add the smallest complete foundation for the next milestone, with explicit limitations.
- Prefer stable, narrow subsystem interfaces over cross-layer shortcuts.
- Keep current architecture-specific code under `xscore/arch/x86_64`; add separate
  `xscore/arch/<target>` implementations for real ports rather than hiding
  architecture branches in common kernel code.
- Treat memory safety, privilege boundaries, page permissions, and interrupt correctness as core design requirements.
- Do not claim POSIX compliance, security, preemption, process isolation, or driver support until tests prove it.
- Preserve working code and unrelated user changes.
- Treat documented `build.sh`/`run.sh` architecture, firmware, and disk-type
  values as a uniform public API: never add compatibility exits between valid
  values; route them to the architecture's bootable backend.
- Apply every new architecture-relevant request across all supported CPU
  families: x86 (`x86_64`, `x86_32`), ARM (`aarch64`, `arm32`), and RISC-V
  (`riscv64`, `riscv32`). Do not implement or verify such work on only one
  family unless the user explicitly narrows the requested scope.
- Never modify the unrelated CrazyQ Pro project while working on akaOS.

## Automatic memory update rule

After every significant implementation:

1. Update source code.
2. Build.
3. Test.
4. Update `AI/CURRENT_STATE.md`.
5. Update `AI/TODO.md`.
6. Append to `AI/CHANGELOG.md`.
7. Update `AI/HANDOFF.md`.
8. Update `AI/ARCHITECTURE.md` if architecture changed.
9. Append to `AI/DECISIONS.md` if a major architectural decision changed or was added.

Never allow the persistent memory to knowingly become stale.
# 2026-09-21 continuation context

The loader now includes a bounded static PT_TLS tranche in addition to the
earlier general-dynamic implementation: per-thread 64 KiB areas, static TP
relocations, template initialization, and coherent GD/TLSDESC/dlsym access.
XSCore stores TP per user thread and exposes SET_TP/GET_TP (69/70), with
architecture-specific return hooks. ARM32 uses `-mtp=soft` and a
register-preserving `__aeabi_read_tp` syscall stub so r9 remains an ordinary
callee-saved register. `dltest` covers dynamic TLS, static initial-exec TLS,
worker isolation, and DSO reload reset. A clean build and live loader matrix
pass on x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32.

Scalable TLS module/thread teardown, full POSIX pthread variants, complete
toolkit implementations, and unmodified upstream KDE/GNOME/LXDE/LXQt stacks
remain unfinished; do not claim desktop compatibility solely from the native
profile launchers.

The next desktop-runtime increment is complete: the GLib-shaped compatibility
DSO now provides a stable non-null default main context, context-aware loop
creation, and bounded context ref/unref helpers. The dlsym-driven hello probe
checks this contract, and the x86_64 readiness matrix passes after the change.
The source remains a bounded compatibility layer rather than an upstream
GLib/GTK/Qt implementation; pollable sources and broad toolkit ABI coverage
are still future work.

Pollable sources are now present: the runtime maps `g_unix_fd_add*()` watches
to the native POLL syscall and dispatches G_IO conditions alongside timers.
The pipe-based live probe passes in the x86_64 desktop image, while GIOChannel
wrappers now pass as well. A first asynchronous native D-Bus PING transaction
now runs through that descriptor-watch path; full binary message queues and
per-context source ownership remain the next compatibility boundary.

GIOChannel wrappers are now available on top of that path: Unix channels hold
a bounded reference while watched, expose their descriptor, and release the
watch/channel pair with destroy-notify semantics. The live x86_64 probe passes
both raw FD and GIOChannel callbacks. The async D-Bus PING path is live-tested;
per-context ownership and full request/response transport remain the next
boundary.

All six rebuilt guests now pass the compositor/client matrix after the
GIOChannel change. This verifies the shared-library runtime remains integrated
with D-Bus routing, X11/Wayland handshakes, Lumen input, and palette surface
lifecycles across x86, ARM, and RISC-V.
 
The next transport boundary is now partially implemented: the public native
D-Bus ABI has a fixed-size `akaos_dbus_async_ping` object. It writes the native
PING request once, then its pump checks POLLIN without blocking and consumes a
short/partial PONG response until complete. `hello --desktop-runtime-test`
registers the live session-bus descriptor with `g_unix_fd_add()` and verifies
the callback-driven result. The x86_64 readiness matrix and all-six guest
compositor matrix are green. This proves asynchronous descriptor integration,
not a complete libdbus implementation; binary request queues, cancellation,
reconnect, and source ownership per `GMainContext` remain open.

The async proof now includes the standard wire path: an already authenticated
binary connection sends the D-Bus Ping method call, and a fixed 512-byte
caller-owned parser reads/validates one aligned METHOD_RETURN frame through
`g_unix_fd_add()`. The live marker is
`D-Bus asynchronous binary descriptor watch verified`; x86_64 readiness and
all-six architecture compositor probes pass it. Reusable queues, serial
matching, cancellation/reconnect, and per-context ownership are still open.

The reusable queue tranche is now implemented: `akaos_dbus_async_client` is a
four-slot caller-owned standard-binary request queue with serial allocation,
incremental writes/reads, reply matching, bounded METHOD_RETURN/ERROR bodies,
and cancellation. The hello probe queues `Ping` and `GetId` concurrently
through `g_unix_fd_add()` and reports `D-Bus asynchronous request queue
verified`; all six compositor guests pass the marker. Reconnect, per-context
source ownership, and the complete upstream libdbus type/message API remain
future work.

Per-context source ownership is now implemented in the bounded runtime:
`g_main_context_new()` creates a second context, thread-default push/pop selects
where source-registration APIs attach, and every source retains/release its
context while loop/iteration/pending/poll dispatch filters by that owner. The
live hello probe proves a pipe event registered on the second context is not
consumed by the default context; all six compositor guests report
`GLib per-context source ownership verified`. D-Bus reconnect and the complete
upstream GLib source API remain future work.

The async D-Bus client now also has an authenticated reconnect boundary:
`akaos_dbus_async_client_reconnect()` closes the previous connection, clears
pending serial/reply and partial-frame state, re-runs the bounded binary
handshake, and starts a fresh serial epoch. The live desktop probe removes the
old FD source, attaches a replacement, and completes a new Ping; all six
guest transcripts report `D-Bus asynchronous reconnect verified`. Explicit
cancelled-reply/reset ownership is covered below; richer retry backoff/address
parsing remains open.

The async queue now gives cancellation and close explicit ownership semantics:
cancelled serials remain one-shot slots until `take_reply()` returns
`ECANCELED`, while `akaos_dbus_async_client_close()` closes the descriptor and
clears replies, serial state, and partial frames. The live probe reports
`D-Bus asynchronous cancellation lifecycle verified` and
`D-Bus asynchronous cancellation and close lifecycle verified` on x86_64 and
all five non-x86/x86_32 guests. Automatic retries and complete upstream D-Bus
serial/message semantics remain future work.

The async client now also exposes a bounded caller-driven retry operation.
`akaos_dbus_async_client_retry()` accepts one to three attempts, clears stale
descriptor, pending/cancelled reply, partial-frame, and serial state between
attempts, and authenticates a fresh connection. It does not create a hidden
worker or silently reattach an event-loop source; callers own replacement watch
registration. The live probe verifies close -> retry(2) -> fresh Ping on all
six guests with `D-Bus bounded retry policy verified`. Richer backoff/address
parsing and complete upstream D-Bus serial/message semantics remain future
work.

The pthread layer now includes the bounded cleanup ABI used by common
GLib/Qt worker code. `pthread_cleanup_push()`/`pthread_cleanup_pop()` keep a
per-thread LIFO stack, while `pthread_exit()` and cancellation drain that
stack before key destructors and static-TLS release. The initial thread gets a
separate stack even though its TID is nonzero. Normal pop execution and
cancelled-worker cleanup pass in x86_64 readiness and all six compositor
guests. Clone flags, asynchronous scheduler interruption, and complete POSIX
teardown semantics remain future work.

### Desktop D-Bus Properties continuation (2026-09-22)

The next desktop-enablement tranche is typed service discovery. `dbus.c` now
has an endpoint-aware binary method-call helper, header signature extraction,
and a bounded Properties dispatcher at `/org/akaos/Desktop`. It returns string
and boolean variants plus six fixed properties in `a{sv}` form, with explicit
errors for unknown interfaces/properties and read-only `Set`. The desktop
service test now verifies `Get`, `GetAll`, introspection, and explicit path /
interface headers. Remember that nested D-Bus alignment is absolute to the
message body, and avoid pointer tables in resident images.

Verification checkpoint (2026-09-22): rebuilt all six architecture images and
ran `tools/test-compositor-clients.sh` successfully. Lumen input, Openbox
activation, desktop D-Bus Properties routing, XSDisplay, toolkit bridge, and
palette lifecycle passed on every guest. Remaining work is the bounded
property-change/full-marshaller layer needed before upstream desktop packages
can be considered genuinely compatible.

Follow-up verification (2026-09-22): native roles now emit a standard
`PropertiesChanged` readiness signal (`sa{sv}as`) and the hello probe subscribes
through `AddMatch`, validates headers, and decodes all six mixed string/boolean
properties independent of dictionary order. The signal path passes in the full
x86_64/x86_32/AArch64/ARM32/RISC-V64/RISC-V32 matrix. General-purpose D-Bus
marshalling and unmodified upstream desktop compatibility remain open.

Latest continuation (2026-09-22): the standard broker probe now exercises
`GetConnectionUnixProcessID`, `GetConnectionCredentials`, `GetAllMatchRules`,
`ListQueuedOwners`, `UpdateActivationEnvironment`, and `ReloadConfig`; the
methods are advertised by introspection and return bounded typed values. A
large introspection reply uncovered a kernel TCP framing defect, so
`xscore_socket_send` now emits ordered sub-MTU segments for every stream
write. The x86_64 readiness gate and the complete six-port compositor/client
matrix pass after this change. Full arbitrary-signature D-Bus marshalling and
unmodified upstream desktop packages remain future work.

Latest continuation (2026-09-22): the userspace builders now produce a real
shared `libc.so.6` ET_DYN and package it in every initramfs. Dynamic programs
resolve libc through PT_INTERP/DT_NEEDED; i386 uses a dedicated GOT-aware
dynamic CRT and RV32 accepts the shared-data `R_RISCV_32` relocation. The
PT_INTERP loader regression and the complete compositor/client matrix pass on
x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32. This is an akaOS-native
runtime foundation, not yet unmodified KDE/GNOME/LXDE/LXQt compatibility.

## 2026-09-22 generic D-Bus codec state

The current initramfs images include a reusable D-Bus signature/body codec and
`hello --dbus-marshaller-test`. It supports aligned little-endian primitives,
arrays, structs, dictionary entries, variants, bounded recursion, and malformed
body rejection. Current x86_64/x86_32 dual-firmware images and all four native
images pass the compositor/client matrix; x86_64 desktop readiness also passes.
The codec still needs a public generic decoder/value-tree API, strict object
path/signature checks, and broker dispatch for arbitrary signatures. Full
unmodified KDE/GNOME/LXDE/LXQt remains future work.

The latest rebuild fixed D-Bus array alignment/length semantics, added strict
path/signature checks at transport boundaries, and added the caller-owned
generic decoder. All six compositor/client guests and the x86_64
desktop-readiness guest pass again. Arbitrary broker dispatch and real upstream
toolkit ABI coverage remain open.

Latest continuation (2026-09-23): XSCore syscall 71 now supports blocking
futex WAIT/WAKE. Shared keys use the mapped physical word; the private flag
uses address-space plus virtual address. Scheduler pending-wake state prevents
lost wakes during the block transition. Mutex, condition, rwlock, once,
barrier, and semaphore wait loops use it. Process page lookup now includes mmap
and heap pages. The parent-waits/child-wakes shared-mapping test and full
compositor/client matrix pass on all six ports; x86_64 desktop readiness also
passes. At that milestone timed condition/semaphore operations still polled
and futex timeout/bitset plus robust/PI behavior were absent; the later
scheduler/timed-wait update below supersedes that status. Process-shared
pthread mutex, condition, rwlock, barrier, and semaphore tests now pass on all
six guests. Before the scheduler scaling continuation below, the fixed
capacity was 16 threads. Do not claim unmodified KDE/GNOME/LXDE/LXQt
compatibility.

Latest continuation (2026-09-23): XSCore's fixed TCB capacity is now 64, its
per-CPU ready bitmap is 64-bit, and libc's bounded pthread-record pool is 64.
`hello --pthread-test` creates 24 simultaneously gated workers, checks each
worker's TLS value and join result, and passes in the six-port
`tools/test-compositor-clients.sh` matrix. The x86_32 process mmap-page cap is
still 128 pages (512 KiB): increasing it reproduced a boot-time supervisor
page fault, so the stress test uses 16 KiB worker stacks. This is a measured
capacity improvement, not full POSIX or upstream desktop support. Relative
timed futex waits now round up, and a blocked caller sleeps when no thread is
READY; four 1 ms waits pass on all six ports. The full compositor/client
matrix and x86_64 readiness gate pass, and the x86_32 dual-firmware ISO
reaches login under UEFI and BIOS (UEFI test uses 256 MiB because 128 MiB did
not complete OVMF/GRUB startup). Futex bitsets, robust/PI semantics, and stock
KDE/GNOME/LXDE/LXQt package support remain unfinished.
