# akaOS Architecture

This document describes the architecture that is present in source as of
2026-09-08. Planned components are labeled clearly.

## Architecture portability boundary

The complete kernel/userspace system is boot-tested on x86-64, x86_32,
AArch64, ARM32, RISC-V64, and RISC-V32. The x86 ports boot from firmware media;
the ARM and RISC-V ports are native direct-kernel QEMU `virt` targets.
`sdk/include/akaos/architecture.h` defines their shared compiler-derived IDs.

## D-Bus wire codec boundary

`userspace/libc/src/dbus_codec.c` supplies the body signature parser,
validator, encoder, and caller-owned decoder used by userspace D-Bus paths.
The explicit-byte-order APIs support both standard `l` and `B` body encodings;
legacy APIs remain little-endian. A shared message normalizer accepts either
marker, validates protocol-v1 frame and typed header-field lengths, converts
header numeric/string-length values and body values, and canonicalizes frames
to little-endian before the current bounded client or `dbus-daemon` dispatch
parses them. Client response paths likewise normalize either byte order. The
codec enforces the 255-byte signature limit, bounded nesting, container
alignment/length boundaries, valid object paths, strict UTF-8 strings, and no
embedded NUL bytes. Six-port regressions cover malformed wire rejection,
byte-exact endian vectors, and live big-endian `Hello`/`RequestName`/
`ReleaseName` calls. The broker's `ListNames` string-array elements are aligned
to 4 bytes. This is still a bounded D-Bus subset: complete standard method and
policy semantics, arbitrary service interoperability, and upstream desktop
compatibility remain outstanding.

## Process parentage and orphan reaping

XSCore stores PPID in each process record and exposes the current process's
value through syscall 73 (`getppid`). The kernel registers the launched
`/bin/init` supervisor as the active orphan reaper. On parent exit, the kernel
reparents its visible children before publishing the parent as EXITED, using
the process-table lock plus each child's process lock; `waitpid` snapshots
ownership and state under the same lock order. If init is restarted, its new
instance adopts visible processes temporarily detached while no reaper was
active. `userspace/bin/init` polls its login child and uses nonblocking
`waitpid(-1, ...)` to collect adopted zombies while the session is live.

Every boot route must register init: the common x86 startup path and the
separate direct-kernel entrypoints for AArch64, ARM32, RISC-V64, and RISC-V32.
The QEMU regression forks a grandchild, waits/reaps its intermediate parent,
checks the grandchild's PPID changed to a live nonzero process, and verifies
the former parent gets `ECHILD`. This is tested across all six ports, but does
not imply complete POSIX process lifecycle semantics.

## AF_UNIX peer identity and D-Bus authentication

XSCore snapshots the effective process PID/UID/GID at UNIX stream `listen()`
and `connect()` time. Once connected, the client sees the listener snapshot
and the accepted server endpoint sees the connector snapshot through libc's
`getsockopt(SOL_SOCKET, SO_PEERCRED)` wrapper. The syscall record and
`struct ucred` use fixed-width fields across 32-bit and 64-bit ports.
Unsupported options/families and unconnected sockets fail explicitly.

The native D-Bus broker checks EXTERNAL's UID claim against the kernel peer UID
and stores credentials with each authenticated connection. Its standard
Unix-user, process-ID, and credentials-dictionary methods return the captured
identity. The listener's legacy TCP endpoint is not an authenticated D-Bus
transport. System-bus policy, credential delegation, and full upstream D-Bus
semantics remain planned.

## AF_UNIX ancillary descriptor transfer

libc `sendmsg()`/`recvmsg()` and XSCore syscalls 74/75 share native-width
`iovec`, `msghdr`, and `cmsghdr` layouts; compile-time offset/size assertions
guard the kernel/libc ABI on 32-bit and 64-bit ports. Connected AF_UNIX streams
queue `SCM_RIGHTS` references at byte positions in the receive stream. A
receiver can atomically install up to four open-file descriptions, gets
`MSG_CTRUNC` when its control area cannot hold all queued references, and
leaves records queued if descriptor allocation fails. Normal reads discard
ancillary rights they consume. Socket-close teardown collects undelivered
references, and the message syscalls hold a reference to the socket file while
operating to prevent concurrent close/reuse races. Cross-process QEMU tests
pass a pipe writer and verify writes through the received descriptor on all
six supported ports.

This remains a bounded subset: AF_UNIX stream only; at most four rights per
position and eight queued records; control data is capped at 256 bytes; no
socket descriptors are transferable because cyclic socket references need
garbage collection; only a small flag subset is accepted; and D-Bus `h` body
values are not yet connected to the fd-list/ancillary path. It is not complete
POSIX/Linux socket compatibility.

## Composed desktop session service boundary

`userspace/bin/desktop-compat` is the common native implementation behind the
profile aliases for LXDE, LXQt, GNOME, and KDE Plasma. Resident `--component`
processes register bounded per-role activation definitions and claim matching
well-known names on the native session bus (`org.akaos.LXDE.*`,
`org.akaos.LXQt.*`, `org.akaos.GNOME.*`, and `org.akaos.KDE.*`), releasing and
unregistering only definitions they own. `--once` lifecycle probes skip
persistent registration. This is an akaOS-native service and toolkit
compatibility boundary; it does not claim binary compatibility with the
complete upstream D-Bus, GTK, Qt, or desktop projects.

## Naming Philosophy & Subsystem Separation

“akaOS is the operating system. XSCore is the kernel. Do not use akaOS as a generic prefix throughout XSCore.”

The repository maintains a clean conceptual hierarchy:

```text
akaOS/
├── boot/
├── xscore/
│   ├── arch/
│   ├── memory/
│   ├── kernel/ (process, sched, thread, syscall, vfs, tty, fb_console, pci, drivers)
│   └── include/xscore/
│
├── userspace/ (libc, crt, bin utilities: akash, ted, uname, whoami, etc.)
├── graphics/
├── desktop/
├── applications/
└── AI/
```

- **XSCore** is the independent kernel core (memory management, scheduler, virtual filesystem, interrupts, timers, device drivers).
- **akaOS** is the overarching operating system product (desktop environment, terminal, applications, services, and distribution identity).
- **Independent Userspace Naming**: Applications and utilities feature distinct, standalone names (e.g. `ted`, `akash`, `login`) rather than repetitive `aka` prefixes.

## Native AArch64 direct-kernel path

`tools/build-aarch64.sh` builds the AArch64 CRT/libc, all 26 native userspace
programs, a 35-entry initramfs, the XSCore AArch64 entry/vector code, four-level
MMU, direct 128 MiB PMM setup, kernel heap, VFS/TTY/process/syscall/thread
subsystems, and `build/XSCORE_AARCH64.ELF`. QEMU's `virt` machine loads this
kernel directly with `-M virt -cpu cortex-a57 -kernel ... -serial stdio`.
The runner disables QEMU's separate monitor, multiplexes the PL011 serial
console, and uses QEMU RAMFB plus a VirtIO keyboard for its graphical console.

The AArch64 exception path saves/restores the complete user register frame.
Its thread context also carries `ELR_EL1`, `SPSR_EL1`, and `SP_EL0`; these are
CPU-global registers and must be restored when a shell blocks in `waitpid`
while a child runs. The native test `tools/test-aarch64.sh` verifies login,
`uname`, `whoami`, `pwd`, external `ls`, child exit/reap, and shell prompt
resumption.

## Native ARM32 direct-kernel path

`tools/build-arm32.sh` builds an ARMv7-A EABI CRT/libc, all 26 native
userspace programs, a 35-entry initramfs, and the ARM32 XSCore entry/vector,
short-descriptor MMU, direct 128 MiB PMM, heap, VFS/TTY/process/thread/syscall
subsystems into `build/XSCORE_ARM32.ELF`. QEMU's `virt` machine loads it with
`-M virt -cpu cortex-a7 -kernel ... -serial stdio`.
The runner disables QEMU's separate monitor, multiplexes the PL011 serial
console, and uses QEMU RAMFB plus a VirtIO keyboard for its graphical console.

The ARM32 user image is linked at `0x10000000`, where the loader maps it, so
static GOT/data references remain valid without a dynamic relocator. The MMU
uses 16 KiB-aligned L1 tables, private user mappings through `0x10ffffff`,
guarded kernel stacks, and ARMv7 SVC entry/return. Because ARM32 has one
banked user SP per CPU, the SVC path saves and restores each thread's user SP
around blocking syscalls. `tools/test-arm32.sh` verifies root login, shell
commands, external `ls`, child exit/reap, and prompt resumption.

## Native system lifecycle interface

The shared reboot syscall synchronizes filesystem state before invoking an
architecture-appropriate machine lifecycle interface. AArch64 and ARM32 call
the PSCI 0.2 `SYSTEM_RESET` or `SYSTEM_OFF` function through HVC, matching the
QEMU `virt` firmware contract. RISC-V 64 and RISC-V 32 call the OpenSBI SRST
extension with cold-reboot or shutdown reset types. RISC-V retains the QEMU
syscon MMIO values only as a fallback if SBI returns. The automated
`tools/test-nonx86-lifecycle.sh` regression validates reboot, poweroff, and the
shell's shutdown alias on all four ports.

## Host build and run configuration

`build.sh` and `run.sh` retain a flag-driven interface for automation and add
an interactive configuration layer for terminal users. With no arguments on a
TTY, each script sources `tools/interactive-ui.sh` and presents a full-screen
`whiptail` menu; when `whiptail` is unavailable, the same choices are exposed
through a numbered `/dev/tty` fallback. The UI produces the existing
`--arch`, `--firmware`, `--disk-type`, test/verbose, and display arguments, so
there is one validation and backend-routing path rather than a second build
implementation. Non-interactive invocations never wait for menu input.

Before dispatch, both scripts call the shared `tools/dependencies.sh` checker.
It derives requirements from the effective architecture, firmware, disk type,
and test selection rather than demanding the complete toolchain for every run.
Distribution-family mappings produce apt, pacman, dnf, zypper, or apk guidance,
but the checker is deliberately read-only and never elevates privileges.
`tools/host-common.sh` centralizes cross-distribution DejaVu font and GRUB module
directory discovery; `tools/qemu-common.sh` locates OVMF across common package
layouts. Builders consume these discovered paths rather than embedding one
distribution's filesystem layout.

## Firmware handoffs

The x86-64 implementation has two independent firmware paths. UEFI loads
`EFI/BOOT/BOOTX64.EFI`, which loads `XSCORE.ELF` and passes the existing
versioned `akaos_boot_info`. `build/akaos.iso` provides this path through an
El Torito FAT image and provides legacy BIOS through GRUB Multiboot2. Its `xscore_multiboot_entry`
bridge receives the 32-bit protected-mode GRUB state, builds temporary 4 GiB
identity paging, enters long mode, and calls `xscore_bios_entry`. The BIOS
adapter converts Multiboot2 E820 and direct-colour framebuffer tags into the
same plain-data handoff so the normal XSCore path remains shared.

## Boot and handoff

OVMF loads `EFI/BOOT/BOOTX64.EFI` from a FAT32 image. The loader opens `/XSCORE.ELF`, validates ELF64 identity and x86-64 program headers, allocates PT_LOAD segments at requested physical addresses, copies and zero-fills them, discovers the UEFI GOP framebuffer, captures the UEFI memory map, detects optional marker files, exits boot services, and enters XSCore with the System V x86-64 ABI.

The loader-to-kernel boundary is `struct akaos_boot_info` in `sdk/include/akaos/boot_info.h`. It contains a magic/version/size tuple, boot flags, plain framebuffer metadata, and plain memory-map metadata. UEFI protocol objects do not become kernel APIs.

Boot marker files:

- `AKAOS.TEST`: enables test mode and QEMU debug-exit after kernel and Ring 3 checks.
- `VERBOSE` or `AKAOS.VERBOSE`: enables the early framebuffer log. Quiet mode is default.

## XSCore kernel

### CPU

XSCore targets x86-64 long mode and is linked at physical address `0x100000`. It installs its own GDT, including Ring 0 and Ring 3 code/data descriptors, and a TSS. A 256-entry IDT includes original assembly stubs for CPU exceptions, hardware IRQs, and an initial DPL3 `int 0x80` syscall gate.

Exception diagnostics report the vector, error code, RIP, RSP, and CR2 for page faults before halting. A breakpoint self-test verifies the exception path and `iretq` return.

### Physical memory

The PMM is a bitmap allocator covering physical addresses up to 64 GiB. It initially reserves everything and releases only UEFI conventional-memory ranges. It keeps the first MiB, kernel image, boot handoff data, framebuffer, firmware/loader ranges, and MMIO unavailable. It allocates and frees individual 4 KiB pages and tracks free-page count. Bitmap, search-hint, free-count, and frame-reference mutations are protected by one IRQ-safe spinlock.

### Virtual memory

XSCore builds a four-level x86-64 page-table hierarchy and switches CR3. The boot-critical low region is identity mapped; most of that transitional mapping uses 2 MiB writable/executable pages. Address zero is unmapped. The VMM supports 4 KiB map, unmap, flag update, mapping query, and splitting an existing 2 MiB page into 512 4 KiB entries. EFER.NXE and CR0.WP are enabled.

The high-half heap begins at `0xffff800000000000`. Kernel stack regions begin at `0xffff810000000000`; thread stacks begin at `0xffff820000000000`. Process-local user images begin at `0x0000008000000000`.

XSCore retains one kernel page-table root and can create process roots that share only supervisor PML4 branches. Each process builds private user branches beneath its own root. Explicit-root map, unmap, query, activation, and destruction operations support process ownership. Destruction releases private user page-table pages after the process layer releases its user frames.

User stacks are demand-grown within a fixed 256 KiB reservation. The loader
maps the top 4 KiB page; a user-mode translation fault may map only the page
immediately below the process's current stack bottom. The shared process layer
allocates and zeroes the frame, applies user/writable/NX permissions, records
ownership, and advances the bottom. This contiguous-only rule leaves a moving
unmapped guard and rejects unrelated bad pointers. Architecture exception
paths retry handled faults on x86_64, x86_32, AArch64, ARM32, RISC-V64, and
RISC-V32; ARM32 uses a dedicated abort-mode kernel stack.

Anonymous heap pages use the same recoverable fault boundary. `brk()` grows a
reserved range without backing it; the first non-present access allocates and
zeroes one page, records it in the process, and maps it user-writable/NX.
`fork()` shares all tracked user image, stack, and committed heap frames with
read-only mappings and increments PMM reference counts. A write fault on a COW
mapping restores write access for a sole owner or allocates/copies a private
frame when shared. The PMM reference table is keyed by the full physical page
number, which is required by the native ARM/RISC-V RAM base. The current child
copies the architecture-specific user syscall frame and resumes immediately
after `fork()`, with a zero return value; the parent receives the child's PID.
This continuation is verified on all six ports. libc `getppid()` invokes
XSCore syscall 73, which returns the current process record's locked
`parent_pid`; a six-port guest probe verifies that a fork child sees its live
parent. Orphan reparenting after the parent exits and is reaped remains
unfinished. ELF PT_LOAD pages are file-backed reservations loaded on first
instruction or data fault, with segment permissions, BSS zero-fill, and
architecture-specific instruction-fault recovery.

Memory mappings use a dedicated per-process virtual range above the image and
heap. `mmap` reserves page metadata without eagerly allocating frames;
anonymous mappings are zero-filled on first fault, while file-backed
`MAP_PRIVATE` mappings read into a kernel-owned snapshot. Named shared-memory
objects and anonymous `MAP_SHARED` mappings own a common set of physical pages.
Independent processes map those frames directly, and fork bypasses COW for
shared mappings while retaining it for private mappings. `mprotect` remaps
resident pages or temporarily removes `PROT_NONE` pages. Named objects use a
bounded kernel namespace and persist after unlink until descriptor/mapping
references are gone.

### Kernel section permissions

The linker page-aligns section boundaries. XSCore splits large mappings covering its image and applies `.text=RX`, `.rodata=R+NX`, and `.data/.bss=RW+NX`. This is W^X for kernel sections, not for the entire address space; the remaining coarse identity map is still writable and executable.

### Heap

The kernel heap maps pages in minimum 64 KiB growth units. It uses 16-byte-aligned first-fit blocks with splitting and adjacent free-block coalescing. Heap pages are RW+NX. Freed blocks are reused, but unused heap pages are not returned to the PMM. An IRQ-safe lock covers allocation, free, coalescing, statistics, and growth; growth remains locked so two CPUs cannot map the same next heap region.

### Interrupts

On x86_64 XSCore masks the legacy 8259 PIC, maps the Local APIC and IOAPIC,
installs hardware vectors 32-47, uses vector 32 for the timer, routes ISA
keyboard/serial/mouse interrupts from MADT topology, and assigns MSI-X vectors
42/43 to VirtIO block/network completion. ACPI MADT INIT/SIPI brings APs online
with per-CPU TSS/stacks and Local APIC timers. ARM ports use GICv2; RISC-V ports
use PLIC supervisor context plus SBI timer wakeups. Generic MSI allocation and
ordinary user-task scheduling on ARM/RISC-V remain future work; the shared
thread layer now provides online-CPU admission and per-CPU READY masks without
enabling user-task migration. Vector 254 is
an explicit fixed reschedule IPI: a CPU waking a blocked thread owned by
another x86_64 CPU sends it to that CPU's Local APIC so the next scheduling
checkpoint does not wait for a full timer period.

### Timer

The Local APIC timer is calibrated using PIT channel 2 as an approximately 10 ms reference. It runs periodically at 100 Hz and maintains a monotonic tick count. Timer interrupts feed the scheduler accounting hook.

### PCI and block storage

XSCore scans PCI configuration space through mechanism #1 and records a bounded
set of device identities and BARs. It can enable PCI bus mastering for a
selected device. The storage driver supports the legacy transitional
`virtio-blk-pci` interface: it initializes queue 0 with a fixed kernel DMA area,
uses x86_64 MSI-X-assisted completion (synchronous polling on x86_32), and reads
or writes 512-byte sectors. BIOS
x86 runs also expose the writable sidecar through the primary IDE channel; the
x86 kernel's ATA PIO fallback provides the same 512-byte block API when legacy
VirtIO completion polling is unavailable.
The ARM and RISC-V QEMU `virt` ports expose the same block API through
`xscore/kernel/virtio_blk_mmio.c`, which scans the architecture's MMIO
transports, negotiates a modern or legacy VirtIO device, configures split
virtqueue 0, and performs IRQ-aware 512-byte transfers that sleep between
GIC/PLIC or bounded timer wakeups. All architectures feed
the shared buffer cache and AKAFS002 persistence layer. The executable page
cache and persistence snapshot path each have an IRQ-safe lock, so concurrent
AP/BSP ELF reads and disk snapshots cannot race their metadata or dirty-buffer
state.
akaFS uses a bounded checksummed snapshot beginning at sector 8 on the separate
data disk. A bounded VFS mount table and 64-sector LRU buffer cache exist, and
AKAFS002 uses alternating checksummed superblocks with generation/state fields
for write-ahead recovery. The persistence lock serializes complete snapshots;
full general transaction nesting and a broader concurrent-I/O framework remain
future work.

### Scheduler

The scheduler module owns bounded per-CPU accounting records and counts timer
ticks, context switches, and completed preemptions. Every thread carries an
owner CPU; x86_64 APs register an idle TCB and select only READY threads from
their CPU. The shared thread layer also keeps a READY bit mask per logical CPU
and validates explicit owner-CPU admission against the online count reported
by native secondary bring-up. The SMP self-test queues a real kernel worker
and a bounded Ring 3
`/bin/hello` workload on each AP, verifies owner-CPU execution, waits from the
BSP, and reaps the remote TCB while concurrently exercising PMM and heap
allocation/free. Descriptor tables have an IRQ-safe table lock, and each file
description is temporarily referenced for I/O before the table lock is dropped;
close/dup/dup2 and clone cannot recycle a description while another CPU is
using it. Vnode references defer block reclamation after unlink/rename until
the final descriptor or executable-image reference is released. The normal
x86_64 shell descendants may now be AP-owned after boot self-tests. Process
snapshots protect PID, CWD, credentials, and address-space metadata used by
cross-CPU paths; remote wakeups use the reschedule IPI above. x86 context
switches save/restore a per-thread FXSAVE-compatible 512-byte FPU area.
Built-in test mode deliberately keeps its startup workloads BSP-local except
for the explicit AP userspace stress workload. VMM/TLB shootdown and the
tested address-space ownership paths are implemented; concurrent migration of
future mutable VFS/device/socket state remains a separate audit. ARM/RISC-V
remain single-core.

The common blocking path rechecks the current READY set while local interrupts
are masked, so a wake cannot be lost between checking the queue and idling. If
another thread is runnable it context-switches; if the queue is empty, x86
uses `sti; hlt` and ARM/RISC-V use the platform WFI helper, then rescan after
the interrupt. Timer IRQs expire relative timed-futex waiters on all six
ports. This makes blocked waits sleep instead of forcing libc to retry in a
busy loop; it does not enable general preemptive scheduling on ARM/RISC-V.
Timer ticks advance each CPU's current slice and latch a request after five
ticks when another thread is READY.

When a timer interrupt occurs while executing in Ring 3 (user mode), the IRQ return path checks the interrupted CS (`frame[18] & 3 != 0`) and invokes `xscore_thread_preemption_checkpoint()`. This executes a context switch directly on the task's privilege-entry stack, saving RSP and switching CR3 and TSS.RSP0 to the next READY thread. When the preempted task is resumed, the context switch returns to the IRQ handler, which restores all saved user GPRs and executes `iretq` back to user space. Kernel threads switch cooperatively at safe checkpoints or explicit yields; kernel-mode preemption remains disabled.

### Kernel threads

XSCore has a fixed table of 64 kernel threads. Each created thread receives a guarded 16 KiB RW+NX execution stack and a separate guarded 8 KiB RW+NX privilege-entry stack. A hand-written assembly context switch saves/restores callee-saved state and RSP, activates the incoming CR3, and updates TSS.RSP0 to that task's entry stack. Selection is round-robin among READY threads. Threads switch through yield, block, safe deferred-preemption checkpoint, or timer interrupt-return preemption (for user tasks); they can wake a BLOCKED TID and exit. Another thread may reap an EXITED TID, unmap and free both stacks, clear its TCB, and reuse the slot.

### Processes and user mode

XSCore has a fixed 16-entry process table with monotonically increasing PIDs
and explicit UNUSED, ALLOCATING, READY, RUNNING, EXITED, and REAPED states. Each record owns
credentials, CWD, descriptors, address space, mappings, and parent identity.
`xscore/kernel/process.c` owns ELF loading, argument stacks, Ring 3 entry,
credential-aware spawning, child-only waiting, signaling authorization, and
cleanup. A table spinlock reserves a slot and PID before slow initialization;
ALLOCATING records are not visible to process lookup, procfs, signals, or waits.

Process creation maps the initial user stack page and formats it according to
the target ABI with `argc`, `argv`, terminating null pointers, and aligned
argument strings. Additional pages are allocated only when the stack reaches
the mapped lower boundary.

The `brk` syscall reserves anonymous heap pages and the shared architecture
fault policy commits them on first access. `sys_fork` clones descriptors,
credentials, CWD, reservations, and address-space metadata while sharing
tracked frames through read-only COW mappings; `hello --vm-test` verifies
zero-fill and parent/child isolation on all six ports.

#### Syscalls

The provisional ABI is shared in `sdk/include/akaos/syscall.h` and uses `int 0x80` with the syscall number in RAX and up to three arguments in RDI, RSI, and RDX. Implemented calls are:

- `read` (number 0): reads from files or blocking kernel TTY (`stdin`).
- `write` (number 1): writes at most 4096 bytes through the current process descriptor table after verifying every page is user-accessible.
- `exit` (number 2): records an exit status and returns control to the suspended kernel path.
- `close` (number 3): releases one descriptor reference.
- `dup` (number 4): creates the lowest available descriptor referring to the same file description.
- `dup2` (number 5): replaces a selected descriptor with a reference to the source file description.
- `open` (number 6): resolves relative to process CWD, supports `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, and installs a new descriptor.
- `stat` (number 7): returns fixed shared-ABI type, mode, and size metadata for a path resolved relative to process CWD.
- `getdents` (number 8): returns bounded fixed-format entries (including `.` and `..`) from a directory descriptor and advances its shared scan offset.
- `getcwd` (number 9): copies the process's current working directory string to user memory.
- `chdir` (number 10): validates and changes the process's current working directory with path normalization.
- `mkdir` (number 11): creates a new directory in the VFS relative to process CWD.
- `unlink` (number 12): deletes a file or empty directory in the VFS relative to process CWD.
- `spawn` (number 13): launches an executable ELF binary with argument arrays in Ring 3 userspace.
- `waitpid` (number 14): waits for target child process termination and retrieves exit status.
- `brk` (number 15): expands or contracts the calling process's reserved
  anonymous heap; physical pages are committed on first access.
- `pipe` (number 16): creates a unidirectional kernel pipe and returns its two descriptors.
- `kill` (number 17): delivers a supported signal to a process by PID.
- `fork` (number 35): creates a COW-shared child address space and descriptor
  table; the child resumes after the syscall with return value zero while the
  parent receives the child PID.
- `execve` (number 38): validates and stages a new ELF image, atomically swaps
  the current thread to its new address space, rebuilds `argc`/`argv`/`envp`,
  and rewrites the architecture trap return to the new entry point and stack.

Unknown syscalls return `-ENOSYS` (`-38`); invalid user ranges return `-EFAULT` (`-14`), and invalid descriptors return `-EBADF` (`-9`).

### File descriptors

Every process embeds a distinct descriptor table. The process scheduler thread holds a pointer to its owner's table, allowing syscall dispatch to resolve descriptors from the current task without a global current-process variable. Descriptors 0, 1, and 2 refer to permanent console input, output, and error file descriptions. Descriptors 0 routes reads to `xscore_tty_read()`; descriptors 1 and 2 route writes to `g_console_sink` and COM1 serial. Descriptors hold references rather than copying file descriptions, so `dup`/`dup2` share the object and offset. Each descriptor table serializes entry replacement and close, and I/O takes a temporary description reference before releasing the table lock; process reap closes every entry. Open vnode/file descriptions and executable image backing carry references through unlink, close, fork, exec, and reap. On x86_64, the tested mutable address-space paths are serialized by the VMM lock and kept TLB-coherent with root-aware synchronous shootdowns; broader concurrent VFS/device/socket migration remains future stress work.

`poll()` asks the descriptor layer for current readiness without consuming
data. TTYs become readable when a canonical line/EOF is ready; pipes report
buffer data/space plus hangup and broken-reader conditions; ordinary files are
nonblocking; input files compare their per-open cursor with the global event
sequence. Blocking waits cooperatively yield and are interruptible by signals.

`/dev/input/event0` is a root-readable kernel input object backed by a bounded
128-event circular stream. Events use a stable timestamp-free
type/code/value ABI. Per-open sequence cursors allow independent compositor or
session consumers. x86 feeds it from PS/2 scan codes, serial text, and decoded
three-byte PS/2 mouse packets. ARM/RISC-V feed it from serial plus a
multi-device VirtIO MMIO driver; normal runs attach keyboard and mouse devices.

The VFS also exposes a boot-created character-device namespace. `/dev` and
`/dev/input` are directories; `tty1`, `null`, `zero`, `fb0`, and `event0` are
typed character nodes with stable modes. `stat()` and `getdents()` therefore
show the same objects that `open()` can use. Open dispatch resolves through
the caller's CWD before selecting a device, so absolute and relative paths are
equivalent. These nodes are kernel-owned and excluded from akaFS persistence.
Null returns EOF/discards output, zero produces zero-filled reads, tty1 uses
the canonical console, and the framebuffer/input nodes retain root-only
access.

### Signals and foreground jobs

XSCore implements kernel signal actions, user handlers and `sigreturn`, process
groups, background jobs, and stop/continue states. The TTY maps control keys to
the foreground process group; blocked calls can return `EINTR`, and CPU-bound
tasks receive signal frames on interrupt return. SID/PGID and controlling-TTY
foreground ownership are implemented, along with bounded `/dev/ptmx` and
`/dev/pts/N` streams. The bounded termios/ioctl layer implements canonical/raw
input, echo, special signal characters, newline output processing, terminal
names, and window sizes. Full POSIX job control, advanced baud/flow-control
semantics, a general ioctl framework, and all signal flags remain incomplete.

### Kernel TTY and line discipline

`xscore/kernel/tty.c` implements canonical line buffering for keyboard input:
- Receives PS/2 scan codes, maintains shift state, and translates keys into ASCII.
- Visually echoes printable characters to the active framebuffer console sink.
- Handles backspace (`\b`), removing characters from the buffer and erasing character cells from the framebuffer screen.
- Handles Ctrl+D as canonical EOF without echoing a control glyph, allowing stdin-only programs such as `cat` to return to the shell.
- Handles Ctrl+C by displaying `^C`, interrupting the current canonical read, and delivering `SIGINT` to all foreground child processes.
- Spawn transfers foreground state to children and `waitpid` restores it to the
  parent. `akash` installs an ignored SIGINT disposition through syscall 29;
  freshly executed children reset to default. Idle Ctrl+C therefore redraws
  the same shell prompt instead of terminating the authenticated session.
- On Enter (`\n`), marks line ready and awakens any user thread blocking in `sys_read(0, ...)`.

### VFS and storage

XSCore has a dynamic in-memory VFS with a root vnode, directory/regular-file nodes, path resolution (`.`, `..`, relative, absolute), creation (`mkdir`, `open(O_CREAT)`), deletion (`unlink`), dynamic in-memory file buffers, metadata inspection, and directory enumeration.

The build packs the native program suite, `/bin/login`, `/bin/su`,
`/bin/passwd`, account databases, and `/etc/motd` into `build/initramfs.img`.
Archive modes are real octal Unix bits, including setuid. The VFS stores 128
KiB page-backed files and checks path traversal plus owner/group/other access
against the current process effective UID/GID.

`process_create_path_args()` and `xscore_process_spawn_path()` resolve programs
through the VFS. After block-storage initialization,
`xscore_vfs_persistence_mount()` overlays saved dynamic nodes. Files are
page-backed up to 128 KiB. A 64-sector LRU buffer cache and AKAFS002 alternating
checksummed superblocks provide committed generations and fallback recovery;
a bounded mount table exists, but there is no complete transaction engine or
general block-filesystem mounting implementation.
Normal `run.sh` sessions attach `build/akaos-data.img` as the writable sidecar.
UEFI x86 uses `virtio-blk-pci`, BIOS x86 uses IDE/ATA PIO, and ARM/RISC-V use
`virtio-blk-device`; all six architecture ports therefore share the same akaFS
backing image and persistence semantics.

### Pipes and IPC

XSCore provides a unidirectional IPC pipe subsystem (`sys_pipe`, `16`) managed by `xscore/kernel/file.c`. A pipe consists of a 4 KiB ring buffer (`struct xscore_pipe`) with reader/writer reference counts. Reading yields cooperatively on empty buffers and yields EOF once all writer descriptors are closed. Writing yields on full buffers and returns `-EPIPE` if all readers have closed. File descriptor tables are cloned across `spawn()` and `fork()` via `xscore_fd_table_clone()` and remain open across `execve()`.

## Userspace architecture

The native runtime enters C at `_start`, extracts `argc`/`argv` from the stack according to System V ABI, and calls `main(argc, argv)`.

The `userspace/libc` provides:
- **Dynamic Heap Allocator**: `malloc()`, `free()`, `calloc()`, `realloc()` freelist allocator in `malloc.c` with 16-byte alignment, block splitting, and bidirectional coalescing backed by `brk()` and `sbrk()` syscalls.
- **Formatted Standard I/O**: `printf()`, `fprintf()`, `sprintf()`, `snprintf()`, `vsnprintf()`, `vfprintf()`, `puts()`, `putchar()` in `stdio.c` supporting `%s`, `%d`, `%i`, `%u`, `%x`, `%X`, `%p`, `%c`, `%b`, `%%` with width and precision flags.
- **Syscall Wrappers**: `open`, `read`, `write`, `close`, `dup`, `dup2`, `stat`, `getdents`, `getcwd`, `chdir`, `mkdir`, `unlink`, `rmdir`, `spawn`, `fork`, `execve`, `execv`, `waitpid`, `brk`, `sbrk`, `pipe`, `kill`, translating negative kernel errors into process-local `errno`.
- **String & Memory**: `strlen`, `strcmp`, `strncmp`, `strncpy`, `strchr`, `strrchr`, `memcmp`, `memcpy`, `memset`, `atoi`.

Userspace utilities in `/bin`:
- `akash`: Native userspace interactive shell running in unprivileged Ring 3. External commands and pipeline stages use `fork()` + `execv()` + `waitpid()` with inherited descriptor state; built-ins that mutate shell state remain in the parent.
- `clear`: Sends ANSI escape sequence (`\033[2J\033[H`) to clear and reset the framebuffer console.
- `grep`: Pattern matching utility over stdin or files; supports `-i` (case-insensitive) and `-n` (line numbering).
- `head`: Outputs first $N$ lines/bytes (`-n`, `-c`, `-<NUM>`) from stdin or files with multi-file headers.
- `tail`: Outputs last $N$ lines (`-n`, `-<NUM>`) from stdin or files using dynamic circular line buffering.
- `wc`: Counts lines (`-l`), words (`-w`), and bytes (`-c`) from stdin or files with formatted total summary lines.
- `uname`: Prints system/kernel/version/architecture info (`-s`, `-n`, `-r`, `-v`, `-m`, `-o`, `-a`) derived from `sdk/include/akaos/architecture.h`.
- `whoami`: Resolves the effective UID through `/etc/passwd`.
- `login`, `su`, `passwd`: Native account authentication, session switching,
  and password maintenance backed by root-only `/etc/shadow`.
- `hello`: verification of process isolation, preemption, file descriptors, pipe IPC, dynamic heap allocation, `printf`, and error syscalls.
- `ls`: lists current directory or target paths; supports `-a`, `-l`, `-la`, `-h`, multiple arguments, error messages to stderr, and exit status.
- `cat`: streams regular files to stdout; supports multiple files, detects directories (`EISDIR`) and nonexistent paths, error messages to stderr, and exit status.
- `touch`: creates empty files via `O_WRONLY | O_CREAT`.
- `mkdir`: creates directories via `mkdir()`.
- `rm`: removes files and directories via `unlink()`.
- `pwd`: prints current working directory via `getcwd()`.
- `echo`: prints arguments to stdout.

## Graphics and console architecture

### Framebuffer and boot renderer

The loader passes GOP framebuffer metadata. XSCore supports RGBX/BGRX 32-bit framebuffers, pixel fill/blending, an anti-aliased DejaVu Sans atlas for the graphical splash and verbose boot UI, an akaOS wordmark, and an explicit progress spinner advanced by real initialization steps.

Quiet boot keeps diagnostics on serial, clears the framebuffer, renders the clean splash, then clears again before `/bin/akash` runs.

### TTY

The console is a fullscreen framebuffer text renderer using a standard IBM VGA/CP437 8x16 bitmap font generated into `font8x16_data.h`. Its grid is calculated from framebuffer dimensions. Every cell draws all foreground and black background pixels at 1:1 scale, preventing old framebuffer content from bleeding through. Login starts at the top-left; authenticated shells render `<user>@akaOS:<cwd># ` for UID 0 or `$ ` otherwise. Secret input disables echo and history. Full-screen line scrolling is supported.

### Userspace display ownership, Lumen Compositor, and GUI Applications

XSCore registers the active framebuffer behind the root-owned `/dev/fb0`
descriptor. Reading it returns one `akaos_display_info`; writes copy pixel
bytes at the descriptor's current offset and `lseek()` provides bounded byte
positioning for region updates. The first writable descriptor saves and detaches
the framebuffer console sink, and the last writable close restores it. Serial
diagnostics remain independent. The shared public contract is
`sdk/include/akaos/display.h`.

The generated `sdk/include/akaos/ui_font_aa.h` and `ui_font_mono_aa.h` atlases
provide anti-aliased proportional and monospace UI typography. They remain
separate from the full 8x16 CP437 TTY font.

`/bin/lumen` is the native userspace window compositor service. It establishes
a protocol-v3 shared-memory IPC control channel at `/lumen-ipc`
(`struct akaos_compositor_channel`) with a bounded synchronized request queue
for attach, damage, move, focus, and destroy messages, a compositor-to-shell
input queue, and a bounded application notification queue. Each client attach
includes an icon resource identity instead of requiring the taskbar to invent
an icon from a PID. The compositor owns the surface table and maps client
window buffers backed by independent shared-memory objects.

Lumen manages multiple client windows with styled titlebars, process-backed
close, minimize/taskbar restore, drop shadows, active focus highlights,
z-ordering, taskbar focus, and interactive mouse dragging.
It clips, blends, and composites window surfaces and alpha-blended pointer cursors
onto `/dev/fb0`, then writes only the clipped union dirty rectangle when a
damage, move, focus, pointer, or lifecycle event requires repainting.

`/bin/palette` is a standalone client application demonstrating the display protocol,
rendering dynamic color themes, gradient bars, and vector-style geometric shapes to
its shared surface. Its attach, damage, and destroy lifecycle uses the bounded
compositor queue. `tools/test-compositor-clients.sh` runs two palette clients on
all six QEMU architecture targets.

`/bin/display-protocols` also exposes a local AF_UNIX display-transport bridge
at `/run/X11-unix/X0` and `/run/user/0/wayland-0`. Its X11 side implements the
setup reply plus a bounded request subset (`GetInputFocus`, `GetGeometry`,
`InternAtom`, and `QueryExtension`); its Wayland side implements registry
advertisement for `wl_compositor`, `wl_shm`, and `wl_seat`, registry binding, and
`wl_display.sync` callbacks. The bridge is intentionally a protocol boundary
for native akaOS clients, not a complete X server or Wayland compositor.

### Native X11 display server (2026-10-27)

`userspace/bin/xserver` is an original X11 protocol server.  It serves the
connection setup, the core windowing, property, drawing, font, colormap,
cursor and input requests, maintains a per-client event selection for every
window, redirects map/configure/reparent of a window to the window manager
that selected the substructure redirect on its parent, and publishes its
screen to the Lumen compositor as one surface.  `GetImage`, `SendEvent`, the
core text requests and the pointer/keyboard delivery paths are present.  It is
not a complete X server: the RENDER extension and the input extensions are
absent, so Xft falls back to the core font protocol.

The event stream has two per-receiver rules that the protocol requires and
that XCB depends on.  Every event carries the sequence number of the last
request the server processed for the client that receives the event, and the
first window of the event record is the window of the delivery: the window
itself for a selection on the window, the parent for a selection on the
children.  XCB advances its request-completion tracking from the sequence
number of every packet it reads, so an event that carried the sequence number
of the window owner dropped pending replies of another client.  Keyboard,
button, motion and crossing events keep their timestamp as the first field
and the mapping notifications carry no window, so the delivery window is not
stamped for them.

`tools/test-upstream-wm.sh` is the gate for this layer.  It runs the
unmodified upstream Openbox 3.6.1 against the server; the passing transcript
shows the window manager reparenting a client window into a frame
(`0x1000064`), the client verifying its tree parent, drawing a known pixel
into the framed window and reading it back with `XGetImage`.
`QueryTree` and `ConfigureWindow` follow the protocol value semantics that
this check exposed: the query reports the root window and the actual parent,
and a configure request that does not mention the geometry leaves the current
geometry and backing store alone.

The packaged runtime objects export a small akaOS-native event-loop/source
queue, GObject-style reference helpers, GTK-shaped window lifecycle calls, and
a C-facing Qt application/widget bridge. Conventional soname aliases such as
`libglib-2.0.so.0`, `libgtk-3.so.0`, `libgtk-4.so.1`, and the Qt6 `.so.6`
names resolve to these objects so source ports can discover the ABI by normal
dependency names. Full upstream GLib/GTK/Qt symbol and C++ ABI coverage is
still a planned layer.

Both `lumen` and `palette` are verified across all six architecture targets;
Aurora, terminal, Notepad, Task Manager, and WebViewer use the same queued
attach/damage/destroy lifecycle.

Aurora discovers launchable GUI applications from `/etc/aurora-apps`; its
launcher supports mouse activation, search, arrows, Enter, Escape, and the
Super-key shortcut. The tray reads real `/proc/net/dev`, `/proc/net/route`,
`/proc/asound/cards`, and `/proc/realtime` capabilities. `/proc/realtime` is
backed by CMOS RTC on x86, PL031 on ARM virt, and Goldfish RTC on RISC-V virt.
There is no XSCore battery-class interface yet, so Aurora deliberately creates
no battery item. Shell overlays are mapped on demand to preserve the bounded
i386 process mapping budget.

## Important subsystem interfaces

- Loader → kernel: versioned `akaos_boot_info` plain-data ABI.
- PMM → VMM: physical 4 KiB page allocation for page tables and mappings.
- VMM → heap/stacks/threads/user loader/APIC: mapping APIs and permission flags.
- IDT/assembly → C: normalized exception/IRQ frames and syscall dispatch.
- APIC timer → scheduler: `xscore_sched_timer_tick()` accounting hook.
- PCI configuration space → device drivers: mechanism #1 scan, BAR discovery, and bus-master enablement.
- PCI/IOAPIC/MSI-X → x86 devices: MADT-driven ISA routing and bounded VirtIO
  completion vectors, with used-ring work deferred to process context.
- PCI/IDE → block storage: IRQ-assisted legacy VirtIO on x86_64, synchronous
  VirtIO/ATA on x86_32, plus IRQ-aware MMIO VirtIO waits on ARM/RISC-V.
- Persistent data disk → akaFS overlay: checksummed dynamic VFS snapshot restored after initramfs mount.
- Scheduler/IRQ return → threads: safe-checkpoint Ring 0 switching and saved-frame Ring 3 preemption.
- Threads → VMM: every cooperative task switch activates the incoming thread's address-space root.
- Threads → TSS: every task switch selects the incoming guarded privilege-entry stack through RSP0.
- Internal process API → VMM/threads: caller-supplied ELF creation, private-root ownership, CR3-bound task creation, separate wait, and reap.

- Process layer → descriptor core: per-process table initialization, scheduler-task binding, and close-all during reap.
- Syscall entry → descriptor core: current-TCB table lookup followed by descriptor operations.
- VFS → descriptor core: path-resolved regular files become pooled read-only file descriptions.
- Initramfs → VFS: boot-time construction of the initial root directory tree.
- Syscall entry → current TCB: task-owned Ring 3 kernel continuation and exit status.
- User code → kernel: temporary `int 0x80` ABI.
- Native libc → shared SDK ABI: syscall wrappers (`spawn`, `waitpid`, `getcwd`, `chdir`, etc.) and errno translation without Linux runtime code.
- Kernel logging → serial: always available; framebuffer output is mode-controlled and decoupled from background logs.
- Main → PID 1 → login → shell: kernel launches `/bin/init`; init supervises
  `/bin/login`; login authenticates and drops credentials before `akash`.
- Shell → utilities: `akash` parses pipelines/redirections/variables and
  expands unquoted final-component `*`/`?` patterns through VFS `getdents`
  before `fork`/`execv`; utilities receive concrete argv entries and validate
  their own supported options and operands.

Update this file whenever any of these boundaries changes.

### x86_64 multicore VMM/TLB ownership (2026-09-07)

The x86_64 page-table implementation uses one global VMM lock for all table
walks and mutations.  Map, unmap, permission-update, and large-page-split
operations release that lock before synchronously issuing TLB invalidations.
This ordering is required because a remote CPU can be inside a masked page
fault while waiting for the same lock.  The lock's contended wait path enables
interrupt delivery only while the lock is held by another CPU, then disables
interrupts for the atomic acquisition and restores the caller's original
state on release.

TLB shootdowns use x86_64 fixed vector 253.  Each CPU publishes the root it has
loaded; private-root changes target only CPUs using that root, while shared
kernel-root changes broadcast to all online CPUs.  The shootdown mailbox has a
separate IRQ-aware lock so a CPU waiting for the mailbox can still acknowledge
an incoming invalidation.  Context switches and AP startup update the active
root publication.  Reschedule IPIs (vector 254) may target CPU 0, which lets a
BSP waiter resume when an AP-owned process exits.

The implementation is verified by the 2/4-CPU `tools/test-smp.sh` suite,
including AP Ring 3 stack-growth page faults, remote wait/reap, and concurrent
PMM/heap activity plus bounded per-AP VFS/descriptor, `/dev/null`/`/dev/zero`,
and UDP socket lifecycle stress. Other architecture ports now have a bounded
secondary heartbeat, timer-accounting, and READY-admission boundary, but
continue to use their existing single-core VMM and process paths until
trap-frame and address-space migration are fully audited.

### Persistent akaFS and external VirtIO-Net SMP extension (2026-09-07)

The scheduler self-test is ordered after PCI and optional device initialization
so AP workers can exercise the writable akaFS overlay and real network driver.
Each AP leaves one unique `/root/xsmpN/persist` record after an explicit
dual-superblock sync; the SMP harness inspects the disk image and requires all
records. Snapshot construction holds the VFS tree lock only while copying a
coherent metadata/data image and performs block I/O afterward under the
persistence lock. Four UDP datagrams per AP target QEMU's `10.0.2.2` gateway;
VirtIO-Net's single TX descriptor and DMA buffer are protected by an
IRQ-safe lock. This verifies bounded persistent commits and concurrent external
transmit, not remote replies, arbitrary disk transactions, or full socket I/O.

### Native non-x86 secondary-core entry (2026-09-08)

`xscore/arch/virt/secondary.c` is linked into all four native ARM/RISC-V
builders. The AArch64 and ARM32 entry stubs inspect MPIDR, while the RISC-V
stubs inspect the OpenSBI hart ID in `a0`; non-zero units select a private
16 KiB identity-mapped stack and wait on `xscore_secondary_release[]`. SBI
uses a dedicated secondary entry symbol, so an arbitrary OpenSBI boot-hart ID
cannot be mistaken for a secondary. The
primary requests units with PSCI HVC (`CPU_ON`) or SBI HSM (`hart_start`), then
publishes the release flag. `xscore_secondary_cpu_entry()` registers a
per-CPU idle TCB, records an online flag, and performs bounded heartbeat/tick
accounting so firmware bring-up is observable without entering the still-
BSP-owned process scheduler. QEMU `-smp 2` and
`-smp 4` boots report the heartbeat pass on AArch64, ARM32, RISC-V64, and
RISC-V32.

After idle-TCB registration, each secondary configures its local timer IRQ
before publishing `online`. ARM ports enable the banked GICv2 CPU interface and
physical-timer PPI 30; ARM32 gives each secondary a private banked IRQ stack.
RISC-V ports derive the supervisor PLIC enable/claim window from the raw hart
ID (`0x0c002080 + hart*0x100` and `0x0c201000 + hart*0x2000`) and program the
SBI timer locally. `xscore_device_irq_dispatch()` rearms and acknowledges the
timer, then calls `xscore_sched_timer_tick()` so each logical CPU records its
own hardware ticks. The non-x86 scheduler deliberately stops at accounting;
user-task context/address-space migration remains BSP-only. The SMP harness
requires one observed timer IRQ on every online secondary.

The shared thread layer exposes the online secondary count through
`xscore_smp_cpu_count()`. `xscore_thread_create_on_cpu()` rejects an owner
CPU that has not reached that firmware-published count, and
`per_cpu_ready_mask[]` records each READY TCB by owner. This makes queue
admission and CPU-local runnable selection explicit on all four native ports
without enabling `multicore_userspace_enabled` or attempting a non-x86
address-space/trap-frame switch.

All six targets compile static context-layout assertions for the C TCB and
assembly offsets. The native ARM/RISC-V builders link
`xscore/kernel/native_context.c`, whose BSP-only probe loads two private
roots and restores the kernel root before booting init. Combined with the
native parity boots, this validates the current single-CPU trap-return and
address-space activation boundary; cross-CPU user-task migration remains
future work.

### User TP and static TLS ABI (2026-09-21)

`struct xscore_user_transition` ends with a 64-bit `user_tp` at byte offset
48. Syscalls 69/70 update/read it for any current userspace process, including
the kernel-created initial process trampoline. x86_64 restores it through
IA32_FS_BASE, x86_32 through a dedicated user TLS GDT descriptor, AArch64 via
TPIDR_EL0, and RISC-V via `tp`. ARM32 keeps the transition value authoritative,
mirrors it from privileged returns when appropriate, and builds userspace with
soft TLS; its `__aeabi_read_tp` preserves r1-r3/r12 around GET_TP.

The loader allocates static TLS offsets from a bounded area (positive TP-relative
for AArch64/RISC-V, negative TPOFF for x86/ARM32), copies PT_TLS templates per
thread, and resolves static relocations while an object is loading. General
dynamic `__tls_get_addr`, TLSDESC, and STT_TLS `dlsym` access reuse the static
instance when present, avoiding two copies of one module's data. This is a
bounded ABI foundation. Static PT_TLS ranges are aligned and first-fit allocated
among live DSOs; final object reset reclaims the range, and a generation change
forces existing threads to copy a newly loaded module's template into reused
space. The six-port `dltest` verifies a 64-byte-aligned TLS address and 12
open/close reloads. Slot growth, fork/exec inheritance, and complete TLS model
coverage remain intentionally outside the current architecture contract.

### Desktop main-context ABI (2026-09-21)

The common desktop runtime carries a bounded `runtime_context` table with a
stable default context. `g_main_context_default()` never returns NULL;
`g_main_loop_new()` records the resolved context, and context iteration and
pending checks safely accept handles crossing the separately packaged GLib,
GTK, and GIO-shaped DSOs. Ref/unref are exported for clients that follow the
usual ownership contract. Source dispatch is still process-wide and bounded,
so this is a compatibility foundation rather than full upstream GLib polling.

The same source table now supports descriptor entries (`fd`, requested events,
and callback condition) in addition to timers. `g_unix_fd_add*()` composes a
bounded native POLL request through `akaos_desktop_poll()`; the loop computes a
timer deadline, polls in at most 100 ms slices when descriptors are present,
and dispatches both kinds fairly. Per-context source isolation remains a
follow-up ABI boundary.

GIOChannel is represented by a private bounded channel table (`fd` plus
reference count); each `g_io_add_watch*()` entry owns an extra channel
reference until the source is removed or its callback returns false. This
keeps the public pointer opaque while making the common GLib ownership pattern
safe across the separately packaged compatibility DSOs.
 
### Reusable asynchronous D-Bus queue (2026-09-21)

The transport now includes a caller-owned `akaos_dbus_async_client` with four
bounded pending slots and a fixed reply workspace per slot. Queue admission
builds a standard little-endian METHOD_CALL with a new serial; the pump reads
complete frames incrementally, validates bounded lengths/alignment, discards
signals, and stores METHOD_RETURN/ERROR replies for serial-matched retrieval.
Cancellation clears a pending slot without changing the shared event loop.

This keeps ownership and memory limits explicit while proving concurrent
requests through the existing `g_unix_fd_add()` path. Reconnect, source
ownership tied to individual `GMainContext` instances, and arbitrary D-Bus
signature/message lifetimes are intentionally still outside this bounded ABI.

### Per-context source ownership (2026-09-21)

`runtime_source` now stores a retained `runtime_context`. Explicit context
creation and thread-default push/pop select the owner for timer, descriptor,
and GIOChannel registration, while loop run, polling, iteration, and pending
checks filter by that owner. Source destruction and loop unref release the
retained context reference. This makes separate toolkit contexts observable
without allocating or spawning a dispatcher; broader GLib source attachment
and cross-thread ownership semantics remain future work.

### D-Bus async reconnect boundary (2026-09-21)

`akaos_dbus_async_client_reconnect()` is an explicit caller-driven lifecycle
operation. It closes the prior authenticated socket, clears pending reply slots
and partial frame state, performs the bounded binary handshake again, and
returns an active client with serials restarted for the new connection. The
descriptor watch is deliberately not retained across reconnect; the caller
must attach the replacement descriptor to its context.

This makes connection loss recoverable without a hidden worker or global
retry loop. Cancellation result states, close/reset ownership, retry budgets,
and full upstream libdbus lifecycle semantics remain separate work.

### D-Bus cancellation and close ownership (2026-09-21)

Cancellation is represented in the bounded reply table instead of silently
freeing a serial. `take_reply()` consumes the cancelled slot once and reports
`ECANCELED`; unknown serials remain `ENOENT`. The client close operation owns
socket teardown and clears every pending/reply/partial-frame field, leaving a
closed state that cannot accidentally be polled or queued.

This keeps descriptor and serial lifetimes explicit for event-loop clients.
Automatic retry/backoff and complete upstream D-Bus transaction semantics are
still outside the fixed-size ABI.

### Asynchronous native D-Bus boundary (2026-09-21)

`akaos_dbus_async_ping` is a caller-owned fixed-size transaction object in the
public D-Bus header. Initialization performs the five-byte native request while
the connection is in its normal blocking mode; the event loop then calls
`akaos_dbus_async_ping_pump()` on POLLIN/G_IO_IN. The pump rechecks readiness
with zero-time `poll()`, reads only currently available bytes, preserves
partial responses, and transitions to DONE only for exactly `PONG\n`.

This deliberately composes with the existing GLib-shaped descriptor sources
without adding a D-Bus worker thread, hidden allocator, or global callback
registry. It is a transport proof for native clients. A full desktop-compatible
implementation still needs binary message queues/serial matching,
cancellation, reconnect/address parsing, and source ownership tied to each
`GMainContext`.

The standard-frame extension keeps the same ownership rule. A
`akaos_dbus_async_binary_ping` carries a 512-byte request/response workspace;
the caller supplies an authenticated descriptor, registers POLLIN, and pumps
until a bounded METHOD_RETURN frame is complete. Header/body alignment and
length arithmetic are checked before the expected tail is accepted. This is
deliberately one transaction, not a global queue: serial correlation,
cancellation, reconnect, and arbitrary message lifetime belong in the next
transport layer.

### Bounded asynchronous D-Bus retry boundary (2026-09-21)

The reusable async queue now has an explicit `akaos_dbus_async_client_retry()`
operation. Its `max_attempts` argument is constrained to one through
`AKAOS_DBUS_ASYNC_CLIENT_MAX_RETRIES` (three). A retry always invalidates the
old descriptor and serial epoch, clears pending/cancelled slots and partial
frames, and re-runs the bounded authenticated binary connection. On success
the client is active with serials restarting at one; the caller must attach the
new descriptor to its own GLib source. This keeps connection recovery bounded
and observable without a hidden thread or automatic source mutation. Address
parsing, backoff, and the complete upstream D-Bus message lifecycle remain
outside this fixed-size ABI.

### Pthread cleanup and exit boundary (2026-09-21)

The libc pthread layer stores up to 16 cleanup entries per worker and a
separate stack for the initial thread. `pthread_cleanup_push/pop()` preserve
the POSIX lexical macro shape; `pthread_exit()` and cancellation pop entries
in reverse order before pthread-key destructors and static-TLS release. The
initial TID is recorded before `pthread_create()` can schedule a worker, which
keeps main-thread handlers distinct from not-yet-published worker records.

This is the lifecycle primitive needed by common GLib/Qt worker wrappers, not a
claim of complete pthreads. Clone flags, true asynchronous interruption,
unbounded cleanup storage, and full process teardown accounting remain outside
the bounded ABI.

### POSIX `pthread_atfork` and child repair (2026-09-24)

The libc stores a fixed maximum of 32 atfork handler sets. `fork()` takes a
stable snapshot, runs prepare handlers in reverse registration order, and
holds the pthread/TSD-record lock across the kernel's address-space snapshot.
The parent releases that lock and invokes parent handlers in registration
order. The child retains and renumbers the calling thread, clears records and
TSD values for vanished workers, unmaps copied worker-stack mappings, resets
the copied lock and main-thread cleanup/cancellation state, then invokes child
handlers in registration order. The live probe forks while a worker exists,
checks both callback sequences, and creates/joins another worker in the child.

The test exposed an i386-specific user transition requirement: the entry shim
writes the first cdecl argument at `user_sp + 4` while executing in CPL0.
`xscore_process_user_thread_entry()` now validates and prepares that user range
before entering assembly so an absent demand-paged stack slot cannot trigger an
unrecoverable kernel-mode page fault. The complete six-architecture
compositor/client matrix passes with the atfork marker. These fixed-table
semantics are not full POSIX/NPTL behavior; TLS inheritance, async-signal-safe
library behavior, and broader fork/exec edge cases remain open.

### Pthread-specific key destructor iterations (2026-09-23)

The libc pthread-key table has bounded per-key values for each thread. Setting
a value to null and deleting a key now release slots. On thread exit, a
non-null value is cleared before its destructor runs, allowing the callback to
publish a new value that is processed in a later
`PTHREAD_DESTRUCTOR_ITERATIONS` pass. Values left after the final allowed pass
are reclaimed. The live probe re-arms a key through all four iterations and
then creates 65 sequential workers to exercise reclamation beyond the
64-thread-per-key slot bound; the six-port guest matrix passes. Broader TLS
module lifecycle, full pthread/NPTL behavior, and the upstream GLib/Qt ABI
remain separate work.

### Typed desktop D-Bus property boundary (2026-09-22)

Resident desktop roles answer `org.freedesktop.DBus.Properties` at
`/org/akaos/Desktop`. The implementation is deliberately bounded: a fixed
property set is encoded as real D-Bus variants and an `a{sv}` dictionary, with
all nested values aligned against the complete message body rather than a
temporary sub-buffer. Property names are returned through a switch-based
relocation-safe helper; static absolute pointer tables are not used in service
images. This boundary is sufficient for early GLib/GTK/Qt-style capability
discovery while property-change signals and general-purpose marshalling remain
outside the current ABI.

### Cross-architecture verification (2026-09-22)

The same typed-properties ABI and compositor boundary are now exercised by
the rebuilt x86_64, x86_32, AArch64, ARM32, RISC-V64, and RISC-V32 images.
The six-port client matrix passes Lumen input, Openbox D-Bus activation/name
ownership, desktop Properties routing, XSDisplay, toolkit display bridging,
and palette lifecycle. This proves parity of the bounded native boundary, not
binary compatibility with unmodified upstream desktop stacks.

### PropertiesChanged signal boundary (2026-09-22)

Native desktop roles publish readiness transitions as a standard
`org.freedesktop.DBus.Properties.PropertiesChanged` signal at
`/org/akaos/Desktop`. The current bounded payload uses `sa{sv}as`, carries the
complete six-property dictionary (`Status`, `Ready`, `Capabilities`,
`DisplayServer`, `Toolkit`, and `SessionType`) with string and boolean variants,
and emits an empty invalidated-property array. The bus forwards the frame to
clients with a matching `AddMatch` rule; the signal is decoded and verified on
all six architectures. Additional signatures, invalidation semantics, and
general-purpose marshalling remain outside this boundary.

### Standard broker and packet-sized stream boundary (2026-09-22)

The resident D-Bus broker has a bounded standard-method surface beyond the
desktop Properties interface: process-ID and credential queries, match-rule
and queued-owner arrays, activation-environment updates, and configuration
reload. Each method validates its request signature and returns a bounded
wire-compatible value; the methods are included in the broker's introspection
document and in the live standard-method regression.

The kernel's IPv4/TCP sender treats a stream write as a sequence of ordered
segments no larger than the packet buffer's safe payload. This is required by
the native loopback and VirtIO paths because the old single-packet path dropped
oversized writes. It is transport framing, not an application-level D-Bus
limit, so X11, Wayland, and toolkit traffic receive the same behavior.

### Shared libc ABI and architecture-specific dynamic startup (2026-09-22)

The userspace runtime has a shared `libc.so.6` ET_DYN assembled from the same
freestanding libc sources used by static programs. The loader maps it through
PT_INTERP/DT_NEEDED, exports the signal-return trampoline, and keeps the demo
DSO dependency path available. x86_32 uses a dedicated GOT-base dynamic CRT;
its static CRT remains direct-call based. RISC-V32 recognizes the `R_RISCV_32`
data relocation generated by the shared libc in addition to the existing
PC-relative forms.

This layer is validated with the six-port loader and compositor/client suites.
It is a native akaOS ABI foundation, not an assertion that unmodified KDE,
GNOME, LXDE, or LXQt binaries already run; their complete ABI and protocol
requirements remain separate work.

### D-Bus codec boundary (2026-09-22)

The shared libc layer now contains a standalone D-Bus codec that is independent
of broker policy. Its parser enforces the D-Bus type grammar and alignment, its
body validator rejects malformed/truncated/trailing data, and its marshaller
walks caller-owned typed value trees for primitive, container, and variant
values. This is deliberately reusable by GLib/GIO, Qt, and native services on
all six ports. It is not yet a complete libdbus replacement: decoding into a
value tree, strict object-path rules, arbitrary broker method dispatch, and
the complete upstream toolkit ABI remain separate layers.

The codec's array length is measured from the first aligned element, excluding
the padding after the uint32 length word, as required by the D-Bus wire format.
Object paths and embedded signatures are checked before marshalling and while
validating received bodies. The same validator is called by libc transport
helpers and resident service dispatch, while authorization policy remains in
the broker.

### D-Bus AF_UNIX transport and activation lifetime (2026-09-23)

The standard binary client path prefers the conventional Unix stream
endpoints `/run/dbus/system_bus_socket` and `/run/user/0/bus`; libc retains a
loopback TCP fallback for older native callers, but the broker does not accept
that unauthenticated transport as a D-Bus session. The broker publishes the
corresponding address files and accepts standard `AUTH EXTERNAL` over AF_UNIX,
verifying the claimed UID against `SO_PEERCRED`. This provides kernel-backed
connection identity but not a general multi-user system-bus policy. Activated children use
`posix_spawn_file_actions_addclosefrom_np(3)` to avoid inheriting daemon and
listener descriptors.

Broker route cleanup preserves a reply once it has been forwarded, even if a
short-lived activation target disconnects before the caller wakes. Persistent
desktop roles keep their D-Bus name/service registration while compositor
surfaces recover independently. The fresh six-port guest matrix and x86_64
desktop-readiness run verify these bounded paths; they do not establish full
libdbus or unmodified upstream desktop support.

## Futex wait/wake path

The libc synchronization layer uses XSCore syscall 71 for blocking waits on
aligned 32-bit words. The default key is the physical backing word so a shared
mapping can be signaled from another address space; `AKAOS_FUTEX_PRIVATE_FLAG`
uses the process root and virtual address instead. Futex waiters are bounded by
the current scheduler TCB capacity. `xscore_process_page_info()` resolves
executable/stack records, heap pages, and mmap-backed pages so the syscall can
derive the physical futex key when the same shared object is mapped at
different virtual addresses in different processes. A parent/child shared-
mapping WAIT/WAKE regression passes on all six guest architectures. The
scheduler stores a pending wake for the narrow race where a waiter has
registered but is not yet BLOCKED. Mutex, condition, rwlock, once, barrier,
and semaphore waits use this mechanism.

The implementation does not yet include futex timeouts, bitsets, robust owner
recovery, or priority inheritance. Pthread mutex, condition, rwlock, barrier,
and semaphore objects retain their process-sharing attributes and select
physical/shared versus address-space/private futex keys accordingly. A
separate-mapping parent/child regression for all five primitives passes on all
six ports. Timed pthread condition/semaphore operations currently use
`nanosleep` polling.

## Scheduler and bounded pthread capacity

XSCore uses a fixed 64-entry TCB table. Each CPU's runnable-thread set is a
64-bit bitmap, with one bit per TCB index; scheduler bit operations use
`uint64_t` masks so indices 32 through 63 remain addressable. Libc's bounded
pthread-record pool also contains 64 slots. The live pthread regression holds
24 workers concurrently behind a gate, checks per-thread TLS and join values,
and passes on all six architecture guests.

The x86_32 process mmap page-record budget is deliberately capped at 128 pages
(512 KiB). Increasing it above that value has caused a boot-time supervisor
page fault, so the 24-worker test uses 16 KiB stacks and fits the current
budget. Replacing the i386 fixed per-process page metadata safely is required
before raising the cap. These fixed bounds are not full POSIX resource
management, and passing the stress probe does not mean upstream desktop
packages are runnable.
