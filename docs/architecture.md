# akaOS architecture

## Portability boundary

The complete UEFI/BIOS XSCore image targets x86-64. Native x86-32, AArch64,
ARM32, RISC-V64, and RISC-V32 XSCore kernels also boot through their tested
Multiboot or direct QEMU `virt` paths with matching userspace/initramfs builds.
See [ports.md](ports.md) for the exact matrix.

## Firmware handoffs

The x86-64 system supports both UEFI and legacy BIOS without sharing firmware
state. UEFI uses the existing PE/COFF loader and passes the versioned plain-data
handoff directly. `build/akaos.iso` includes that loader in a UEFI El Torito
image and also includes GRUB Multiboot2 for BIOS; `xscore_multiboot_entry` is a
small 32-bit protected-mode bridge that creates temporary identity paging,
enters long mode, and calls `xscore_bios_entry`. The BIOS parser translates
Multiboot2 memory-map and direct-colour framebuffer tags into the same
`akaos_boot_info` contract, so the kernel, splash, console, drivers, and
userspace self-tests remain shared.

## Phase 1 boundary

The firmware loader and XSCore are separate programs with a small, versioned
handoff ABI in `sdk/include/akaos/boot_info.h`. Firmware-owned protocols remain
inside the UEFI or BIOS adapter. XSCore receives only plain data: the
framebuffer description, a UEFI or Multiboot2/E820 memory map, and boot flags.
This prevents firmware structures from becoming kernel APIs and keeps both boot
methods on the same kernel entry contract.

XSCore is an ELF64 executable linked at physical address `0x100000`. The loader
validates its ELF identity, machine type, program-header bounds, file bounds,
and segment sizes before allocating and copying each loadable segment. It then
exits UEFI boot services before entering XSCore with the x86-64 System V
calling convention.

## Phase 2 kernel foundation

XSCore installs its own GDT immediately after validating the boot handoff, then
loads a 256-entry IDT. The first 32 entries point to original x86-64 assembly
stubs that normalize hardware error codes, preserve general-purpose registers,
and dispatch to a C exception handler. The automated test executes a breakpoint
instruction and requires control to return through `iretq` before boot
continues.

The physical memory manager uses a page bitmap covering physical addresses up
to 64 GiB. Only UEFI conventional-memory ranges become allocatable; firmware,
loader, XSCore, framebuffer, MMIO, and the first MiB remain reserved.

XSCore then creates a new four-level paging hierarchy and switches CR3 while
executing. The boot-critical physical range is identity mapped, including the
framebuffer, but address zero is left unmapped using a 4 KiB page table for the
first 2 MiB. Remaining boot mappings use 2 MiB pages.

The initial heap begins at virtual address `0xffff800000000000`, maps physical
pages in 64 KiB growth units, and uses first-fit blocks with splitting and
adjacent-block coalescing. NX is applied to heap pages because EFER.NXE is now
enabled during VMM initialization.

User processes reserve anonymous heap space with `brk()` and commit a
zero-filled 4 KiB frame on the first non-present access. A shared process fault
policy handles normalized write/present flags from all six architecture trap
paths. `fork()` maps tracked image, stack, and committed heap frames read-only,
increments PMM reference counts, and copies a frame only when a COW write
fault occurs. The PMM reference table uses absolute physical page numbers so
the native ports' `0x40000000` RAM base is handled correctly. Forked children
restore their complete syscall frame and resume after `fork()` with return
value zero. Syscall buffers cross a shared fault-aware copy boundary that
rejects invalid ranges, commits valid lazy pages, and resolves COW copy-out.
ELF PT_LOAD pages are reserved without frames and loaded on the first
instruction or data fault, copying file bytes, zero-filling BSS, and applying
segment permissions on all six ports.

## Phase 3 kernel hardening

### Section permissions

EFER.NXE is enabled inside `xscore_vmm_initialize`, before any page-table entry
uses bit 63. The linker script page-aligns every section boundary, and
`apply_section_permissions` first splits every 2 MiB large page that
intersects the kernel image (the kernel crosses the 2 MiB boundary at
0x200000 because the 64 GiB PMM bitmap in `.bss` alone is ~2 MiB), then sets:

- `.text` — present, execute, not writable, not NX
- `.rodata` — present, readable, not writable, NX
- `.data` — present, read/write, NX
- `.bss` — present, read/write, NX

Remaining identity mappings (loader data, framebuffer, MMIO, memory outside
the kernel image) stay on coarse 2 MiB writable+executable entries — the
transitional layout documented in `docs/memory.md`. Full-system W^X needs a
per-page identity map for the low 4 GiB or a proper kernel remap; that is
future work and is not claimed.

### Kernel stacks

Stack region: `0xffff810000000000` upward. Each stack is preceded by an
unmapped guard page (a fault instead of silent corruption) and mapped
read/write + NX. Allocation creates the 16 KiB boot stack and the 8 KiB IST1
(double-fault) and IST2 (NMI) stacks, whose tops are installed in the TSS.

The transition onto the dedicated stack happens exactly once through
`xscore_stack_enter` (assembly): it switches RSP to the new top, clears RBP,
and jumps into `xscore_boot_on_kernel_stack`, which never returns. This is the
safe pattern — switching RSP *inside* a normal C function is unsafe because the
compiler-generated epilogue and the caller's frame remain on the old stack; the
previous implementation did exactly that and faulted in the function epilogue
with a corrupted frame. The UEFI loader stack is abandoned for good; nothing
references it after interrupts are disabled at entry.

The first SMP milestone will turn the single stack set into a per-CPU array
with per-CPU GDT/TSS and per-CPU `kernel_stack_top`, all of which the current
structures already anticipate (`struct xscore_per_cpu`).

### Interrupt controller

The 8259 PICs are fully masked. The Local APIC base is read from
IA32_APIC_BASE; the APIC MMIO page at `0xFEE00000` is outside the current
identity map, so it is mapped explicitly (writable, NX) before any register is
touched. The APIC is enabled through the spurious-interrupt vector register
(vector 255), TPR is cleared, and hardware IRQ vectors 32-47 are installed in
the IDT with assembly stubs that dispatch to `xscore_irq_dispatch` and EOI.

### Timer

The APIC timer is calibrated against PIT channel 2 (a ~10 ms one-shot
reference that needs no IRQ), producing ticks-per-second; the periodic LVT
entry then fires at `XSCORE_TIMER_DEFAULT_HZ` (100 Hz, compile-time
configurable). Each interrupt increments the monotonic tick counter and calls
`xscore_sched_timer_tick`.

### PCI and block storage

XSCore scans PCI configuration space through mechanism #1 and records bounded
device and BAR metadata. x86 uses the optional legacy transitional
`virtio-blk-pci` path, while ARM and RISC-V discover VirtIO-MMIO block devices
on their QEMU `virt` buses. Both initialize queue 0 in a fixed kernel DMA area
and poll completion for 512-byte sector reads and writes. A bounded akaFS snapshot starts at sector 8 of a separate data disk,
leaving boot/test metadata untouched. It restores dynamic VFS directories and
files at boot and synchronizes mutations through a 64-sector LRU buffer cache.
AKAFS002 alternates two checksummed committed superblocks with generations and
fallback recovery. There is not yet a general mount table or full filesystem
transaction engine.

### Scheduler foundation

`xscore/kernel/sched.c` owns the BSP `struct xscore_per_cpu` and the
timer-tick hook. The scheduler runs cooperative Ring 0 threads and uses timer
interrupt-return preemption for Ring 3 tasks. User switches preserve the full
saved frame on each task's guarded privilege-entry stack, while kernel threads
switch only at safe checkpoints or yields. The current table and run queue are
fixed-size and BSP-only; generalized wait queues and SMP remain future work.

## Userspace process and syscall layer

XSCore's GDT now includes Ring 3 code and data descriptors. The TSS `RSP0`
points to a dedicated guarded privilege-transition stack, so interrupts and
system calls arriving from user mode do not overwrite the boot stack. A DPL3
interrupt gate at vector `0x80` provides the initial system-call ABI.

The build produces separate freestanding ELF binaries for the native utility
suite, packs them into a generated initramfs archive, and validates each ELF64
header and loadable program header at runtime. XSCore maps each segment into a
user-only address range beginning at `0x0000008000000000`, applying
writable/NX flags from the ELF segment flags. Each process receives a guarded
user stack initialized with System V x86-64 `argc`/`argv` pointers. Syscall
pointer ranges are checked for user-accessible page mappings before the kernel
reads or writes them.

XSCore now creates a private PML4 root for each process, binds that CR3 to a
scheduler thread, and tracks create/run/exit/wait/reap states in a fixed
process table. The boot self-test creates two READY instances at the same
virtual address and proves their code resolves to different physical pages
before scheduling and reaping both. Ring 3 continuation and exit state is now
owned by each TCB, process wait uses BLOCKED/READY wakeup, and subsequent
processes prove stack/page/slot reclamation, argument passing, CWD inheritance,
VFS mutations, pipe inheritance, the initial foreground SIGINT path, and the
PCI/virtio-blk storage self-test. Every
task owns a guarded privilege-entry stack selected through TSS.RSP0. This is
still an early foundation: complete wait-queue semantics, full termios/ioctl
line discipline, and supplementary groups remain future work. Each
process owns real/effective/saved UID and GID credentials. Children inherit
them, setuid/setgid executable bits update effective identity, and identity
changes are mediated by syscalls 22-27.

After the boot self-tests, normal boot launches root PID 1 (`/bin/init`), which
supervises `/bin/login`. Login verifies the root-only shadow database, sets the
account GID/UID and home directory, then launches `/bin/akash`. The kernel TTY
provides canonical input, switchable secret echo, backspace, Ctrl+C/SIGINT
foreground interruption, and ANSI clear handling;
the shell uses `fork`/`execv`/`waitpid`, descriptor inheritance, pipes,
redirection, and CWD-aware VFS utilities. Sessions, process groups, and a
controlling-terminal foreground PGID are kernel-owned. Foreground ownership
transfers to executed child groups and
returns to the waiting parent; the shell ignores SIGINT while executed children
start with the default disposition. Thus idle Ctrl+C cancels input without
ending login, while a running foreground command is terminated. Test-mode boots still terminate automatically
after the user-mode checks so CI remains non-interactive.

## Pollable input

XSCore syscall 45 provides a bounded `poll()` implementation for TTY, file,
pipe, and input descriptors. Pipes expose readable data, writable capacity,
hangup, and broken-reader conditions; invalid descriptors report `POLLNVAL`.
Blocking waits yield to the scheduler and return `EINTR` for pending signals.

Root userspace can open `/dev/input/event0` read-only. It carries fixed-size
type/code/value events through independent per-open cursors over a 128-event
ring. x86 feeds keyboard and three-byte PS/2 mouse events. ARM and RISC-V QEMU
runs use a multi-device VirtIO MMIO driver for simultaneous keyboard and mouse
input. This is the input foundation used by the native compositor; it is not a
Linux input subsystem implementation.

## Device namespace

XSCore installs a small kernel-backed `/dev` tree after mounting the
initramfs. Its VFS nodes carry character-device type and permission metadata,
so `stat`, `getdents`, and `ls -l` report devices instead of pretending they
are regular files. The initial namespace contains `/dev/tty1`, `/dev/null`,
`/dev/zero`, `/dev/fb0`, `/dev/ptmx`, `/dev/pts`, and
`/dev/input/event0`. Opening `ptmx` allocates one of four bounded PTY pairs and
publishes its `/dev/pts/N` slave. Master and slave descriptors provide
bidirectional blocking streams, poll readiness, canonical/raw modes, echo,
special signal characters, output newline processing, and shared window-size
state. Syscall 49 supplies the bounded `termios`/TTY `ioctl` ABI. Baud rates,
flow control, parity, and a general ioctl registration model are deferred.

Open requests first resolve the path through the caller's current working
directory and then dispatch by device identity. Consequently both
`/dev/null` and paths such as `null` after `chdir("/dev")` reach the same
device. Null reads return EOF and writes are discarded; zero reads fill the
requested buffer with zeroes; tty1 uses the canonical console; fb0 and event0
use the framebuffer and input descriptor implementations. The device nodes
are recreated at boot and are not serialized into the akaFS data volume.

XSCore also installs a generated `/proc` foundation. `/proc/self/status`
reports the caller's PID, PPID, PGID, SID, credentials, state, and TTY
foreground group. `/proc/processes` reports a stable per-open snapshot of the
live bounded process table. Absolute `/proc/<pid>/status` opens resolve live
process records, while dynamic PID directory enumeration and a general procfs
provider model are not implemented yet.

## Userspace display and compositor foundation

`/dev/fb0` is a root-owned XSCore display object rather than a regular VFS
file. A read-only open returns one versioned `akaos_display_info` record. A
writable open acquires graphics ownership and accepts framebuffer bytes at the
current file offset; `lseek()` positions a write at a byte within the exposed
pixel buffer, allowing clipped region updates. While an owner exists, normal
TTY rendering is disconnected from the framebuffer so console output cannot
overwrite the graphical scene. Closing
the final owner restores the prior console sink.

The public ABI in `sdk/include/akaos/display.h` defines display metadata,
pixel formats, shared-memory surface headers, and protocol-v2 attach, damage,
move, focus, and destroy messages in a bounded synchronized request queue.
`/bin/lumen` creates and composes named shared-memory surfaces in userspace,
owns the active surface table, maps/unmaps client buffers, and tracks damage
sequence numbers. `/bin/palette` is a separate client that submits its full
attach/damage/destroy lifecycle through `/lumen-ipc`. Lumen paints the
background, surfaces, and pointer, and uses `poll()` on `/dev/input/event0` to
redraw after pointer movement or client messages; Escape returns to the TTY.
Window titles and Aurora panel labels use the dependency-free 5x7 bitmap atlas
in `sdk/include/akaos/ui_font.h`; taskbar badges focus their corresponding
window and close actions promote the next topmost window. Lumen adds clipped
window shadows, an active-client focus ring, and compact traffic-light title
controls; Aurora keeps its panel and wallpaper surfaces alive for the complete
session rather than a bounded demo interval.
At login, `AUTOSTART_DESKTOP=1` is parsed from the persistent configuration and
`/bin/desktop --apps` is launched through the direct kernel spawn path. The
launcher uses that same path for Lumen, Aurora, and default applications and
waits for a cold-boot compositor handshake before reporting success.
`lumen --once` provides a finite graphical presentation test, while
`lumen --self-test` verifies queue ordering and cross-process shared-surface
behavior even on a test boot without an active framebuffer.

This remains an original akaOS compositor, not X11 or Wayland. Lumen composes
complete rows in memory but streams only the clipped union dirty rectangle via
`lseek()` plus `write()`; direct legacy slot changes still trigger a safe full
redraw. Richer window policy, fonts, widgets, and session security can build on
the stable queue later.

## Crash handling

Exceptions other than the breakpoint self-test print the vector, error code,
CR2 (page faults), RIP, and RSP, then halt with interrupts disabled. During
Phase 3 development this diagnostics output identified two real faults: an
unaligned-section mapping error and the unsafe mid-function stack switch.

## Private and shared memory mappings

The shared process layer provides demand-paged `MAP_PRIVATE` anonymous and
file-backed mappings through `mmap`/`munmap` on x86, ARM, and RISC-V. Resident
pages are tracked per process, shared read-only across `fork()` and split on
write faults. File-backed mappings use a kernel-owned private snapshot so
closing the descriptor is safe. Anonymous and named-object `MAP_SHARED`
mappings instead reference common backing frames and bypass fork COW.
`mprotect()` changes permissions on mapping ranges, including `PROT_NONE`.

## What is deliberately deferred

XSCore does not yet reclaim loader memory, provide advanced terminal device
controls or complete POSIX job control, synchronize allocators for SMP,
provide write-through shared
VFS mappings, a general mount table, or complete POSIX behavior. Account hardening
still needs random salts, a memory-hard KDF, supplementary groups, rate
limiting, and password aging/locking. Anonymous heap demand paging and COW are
implemented. Forked children resume immediately after the syscall with return
value zero on all six ports; executable/file-backed demand paging is now
implemented with first-touch page loading and recoverable instruction faults.

## Aurora and Lumen desktop composition

Lumen owns the framebuffer, procedural full-screen wallpaper, pointer, window
stack, shadows, titlebars, focus, drag, close, and final dirty-region writes.
Aurora is a resident shell client that owns the 48-pixel bottom dock and reads
the shared compositor channel to show focused application pills. Keeping the
wallpaper procedural avoids a multi-megabyte shared surface and preserves the
same renderer on x86, ARM, and RISC-V. GUI shared-memory objects are capped at
1 MiB; i386 retains its compact fixed mapping table because its Multiboot
kernel image has a tighter low-memory layout.

Desktop text uses pre-generated 8-bit alpha atlases derived from DejaVu Sans
and DejaVu Sans Mono. Lumen and Aurora alpha-blend the proportional atlas for
chrome and dock labels; applications use the monospaced atlas where aligned
data matters. The font generator is a development tool only, and XSCore needs
no TrueType parser or Linux service at runtime.

Aurora derives icon identity from each compositor surface title and renders
icon-only running-window buttons. Lumen uses the same task geometry for mouse
focus. The system tray is shell-owned and contains network, power, clock, and
date presentation; clock/date begin at the compiled XSCore build timestamp and
advance in the resident panel loop.
