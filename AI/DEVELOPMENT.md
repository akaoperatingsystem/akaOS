# Developing akaOS

## Required host

The supported host is the WSL distribution named `Ubuntu`. Do not use the machine's default WSL distribution because it may be Arch Linux. From Windows, enter the required environment explicitly:

```powershell
wsl.exe -d Ubuntu
```

Then use the working copy at:

```sh
cd "/mnt/c/Users/iniko/Documents/Projects/akaOS"
```

Linux is a development host only; it is not part of the akaOS runtime.

## Required tools

Canonical packages on Ubuntu/WSL:

```sh
sudo apt install clang lld binutils qemu-system-x86 ovmf mtools dosfstools python3-pil fonts-dejavu-core
```

The build expects `clang`, `ld.lld`, `lld-link`, `objcopy`, `mkfs.fat`, `mmd`, `mcopy`, `truncate`, Python 3, Pillow, and a TrueType font (default `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`). QEMU and OVMF are required for run/test/debug.
The x86 ISO/FAT32 firmware images additionally require `grub-pc-bin`,
`grub-common`, `grub2-common`, `grub-efi-amd64-bin`, `grub-efi-ia32-bin`, and
`xorriso`.

Verified host note (2026-08-28): Ubuntu WSL has matching Clang/LLD 21.1.8, Pillow 12.1.1, QEMU 10.2.1, OVMF, and the expected DejaVu font path. The canonical build and test scripts pass there. Arch WSL is explicitly unsupported for this working copy.

## Build

```sh
./build.sh --arch x86_64 --firmware uefi --disk-type fat32
```

`build.sh` is the single build entry point. It defaults to the x86-64 UEFI
FAT32 image, but firmware and disk type can be made explicit:

```sh
./build.sh                                      # UEFI + FAT32 (default)
./build.sh --arch x86_64 --firmware uefi --disk-type fat32
./build.sh --arch x86_64 --firmware uefi --disk-type iso
./build.sh --arch x86_64 --firmware bios --disk-type iso
./build.sh --arch x86_64 --firmware both --disk-type iso
./build.sh --arch x86_64 --firmware bios --disk-type iso --test
```

`--firmware uefi|bios|both` is the only build firmware selector. Disk type is
inferred when omitted. Every documented architecture, firmware, and disk-type
combination is accepted. When legacy BIOS or x86_32 is paired with FAT32, the
script automatically selects the architecture's bootable hybrid-ISO backend
instead of failing the command. Every x86_64 ISO selection creates one
`build/akaos.iso` with UEFI and BIOS boot entries. Firmware `both` with FAT32
creates both the separate FAT32 disk and the dual-firmware ISO.

The `--test` and `--verbose` markers apply to both ISO boot paths. AArch64,
ARM32, RISC-V64, and RISC-V32 also accept the shared firmware, disk-type, test,
and verbose selectors. Never reject those options merely because a port uses
its native QEMU backend; `--test` runs that port's parity suite.

Build and test the dual-firmware ISO:

```sh
./build.sh --arch x86_64 --firmware bios --disk-type iso
./tools/test-bios.sh
./tools/test-iso.sh
```

`./run-bios.sh --headless` or `./run-bios.sh --gui` selects the BIOS entry of
`akaos.iso`. The ISO is separate from `build/akaos.img`.

The main runner uses the same target selectors:

```sh
./run.sh --arch x86_64 --firmware uefi --disk-type fat32 --gui
./run.sh --arch x86_64 --firmware uefi --disk-type iso --gui
./run.sh --arch x86_64 --firmware bios --disk-type iso --headless
```

One QEMU instance selects one firmware, so the x86-64 `run.sh` path accepts only
`uefi` or `bios`. For ISO, both selections use the same file. The same selector
syntax is accepted by all four ARM/RISC-V QEMU `virt` runners.

The complete firmware image is currently x86-64. Native boot-tested paths
also exist for x86-32, AArch64, ARM32, RISC-V64, and RISC-V32. Check a selected port with
`./build.sh --arch aarch64` (or `x86_32`, `arm32`, `riscv64`, `riscv32`), or
run every portability probe with:

```sh
./tools/build-arch-smoke.sh --all
```

The smoke outputs validate SDK headers, target C compilation, assembly entry,
linking, and ELF machine identity. They do not replace a native XSCore boot
port or a board-specific emulator test.

The script:

1. Generates the anti-aliased splash font header.
2. Generates the self-contained IBM VGA/CP437 8x16 TTY font header.
3. Builds the native C utility suite and `/bin/akash` with the shared minimal libc/runtime as standalone relocation-free ELF files and packages their bytes into the initial initramfs.
4. Builds all XSCore C and assembly sources as freestanding x86-64 code with warnings as errors.
5. Links `build/XSCORE.ELF` with `xscore/arch/x86_64/linker.ld`.
6. Builds the PE/COFF UEFI loader.
7. Creates the selected media: a 64 MiB FAT32 `build/akaos.img`, a dual-boot
   `build/akaos.iso`, or both.

The ISO selection invokes `tools/build-bios.sh` to package GRUB BIOS and native
UEFI El Torito entries into `build/akaos.iso` without overwriting `akaos.img`.

Override the splash font only when needed:

```sh
AKAOS_FONT_FILE=/path/to/font.ttf ./build.sh
```

Do not replace the generated 8x16 TTY font with a proportionally spaced font.

## Run

```sh
./run.sh
./run.sh --gui
./run.sh --headless
```

The default selects GUI when WSLg/X11/Wayland is available and otherwise falls back to headless serial output. QEMU uses q35, 256 MiB RAM, OVMF pflash, and the selected boot image.

Normal `run.sh` sessions permit hardware reset: the akaOS `reboot` command
restarts the guest and keeps the QEMU window/session open. Automated tests,
capture helpers, and `debug.sh` intentionally retain QEMU's stop-on-reset mode.
Verify the interactive lifecycle with `./tools/test-reboot.sh`.

## Automated testing

```sh
./test.sh
AKAOS_TEST_VERBOSE=1 ./test.sh
```

Run the complete kernel/architecture/persistence/lifecycle/network regression
set serially with:

```sh
./tools/test-kernel-regressions.sh
```

Its stable summary and individual logs are written under
`.test-results/kernel-regressions/`, outside the recreated `build/` directory.

`test.sh` always rebuilds, copies the boot disk to `build/akaos-test.img`, creates a separate deterministic 4 MiB virtio disk, adds `AKAOS.TEST`, optionally adds `VERBOSE`, boots headlessly, expects QEMU debug-exit status 33, and checks serial milestones for boot handoff, CPU tables, memory, permissions, stacks, APIC, timer, PCI discovery, virtio-blk sector reads, threads, Ring 3, syscalls, exact native utility behavior, process exits, and resource restoration.

For persistence across boots, run:

```sh
./tools/test-persistence.sh
./tools/test-nonx86-persistence.sh
```

It creates `/saved.txt` through the real shell, validates its akaFS payload on
the separate disk, boots again with that same disk, and requires the akaFS load
milestone. Normal `run.sh` boots create `build/akaos-data.img` only if missing;
delete that generated image only when intentionally resetting saved files.
The non-x86 suite repeats the same real `echo > /saved.txt`, poweroff, and
second-boot verification on AArch64, ARM32, RISC-V64, and RISC-V32.

Do not treat printed UI text as proof by itself. A new subsystem should have a state-changing self-test or externally observable QEMU result.

## Debugging

```sh
./debug.sh
```

This rebuilds, starts QEMU paused, and opens a GDB server on TCP port 1234. Attach an x86-64-capable GDB to `localhost:1234` and load symbols from `build/XSCORE.ELF`.

Serial COM1 is the authoritative early diagnostic channel. Fatal exception output includes vector, error code, RIP/RSP, and CR2 for page faults.

## Screenshots

```sh
./tools/capture.sh
./tools/capture-tty.sh
./tools/capture-tty-help.sh
./tools/capture-tty-info.sh
```

The helpers use QEMU framebuffer capture and host-side image conversion. `capture-tty-help.sh` enters `help` and writes `build/tty-help-screen.png`, making command submission, cursor cleanup, spacing, and framebuffer artifacts visually testable. `capture-tty-info.sh` enters `about` and `version` and writes `build/tty-info-screen.png`.

## Boot modes

The loader recognizes empty files at the FAT root:

- `VERBOSE` or `AKAOS.VERBOSE`: render early framebuffer diagnostics.
- `AKAOS.TEST`: exit through QEMU's debug-exit device after automated milestones.

Normal `build/akaos.img` should not contain test or verbose markers unless deliberately prepared.

## Code conventions

- C17, freestanding, no host libc in kernel/loader code.
- Compile with `-Wall -Wextra -Werror` and the existing x86-64 freestanding flags.
- Preserve `-mno-red-zone`; interrupt and kernel code cannot rely on a userspace red zone.
- Avoid floating-point/SIMD until context management exists.
- Keep functions and subsystem interfaces small and explicit.
- Use fixed-width integer types at ABI/hardware boundaries.
- Validate lengths, overflows, alignment, canonical addresses, privilege flags, and ownership.
- Keep architecture-specific assembly and hardware code under `xscore/arch/x86_64`.
- Record incomplete behavior honestly in serial output and `AI/CURRENT_STATE.md`.

## Kernel development

Every kernel change should be checked for boot ordering, interrupt state, stack ownership, page permissions, and whether it executes before or after the dedicated-stack transition. Never switch stacks in the middle of a returning C function. Ring 3 timer preemption is permitted only through the implemented saved-frame interrupt-return path on the task-owned privilege stack; Ring 0 code must continue to switch only at explicit safe checkpoints or yields.

## Driver development

Add discovery and resource ownership before device-specific policy. Current direct port/MMIO accesses are early foundations, not a driver framework. Prefer QEMU-testable devices, define interrupt/DMA rules, and add negative-path tests.

## Graphics and GUI development

Keep the boot splash, TTY, and future display subsystem separate. The current framebuffer renderer in `main.c` is boot infrastructure. A future graphics module should own surfaces, formats, clipping, and synchronization; a compositor must live above IPC and process isolation. Preserve the fullscreen TTY as a fallback.

## Userspace and application development

User programs must be independent freestanding akaOS binaries. Do not import Linux userspace as the OS. Build libc, `aka-init`, `akash`, services, and utilities only on real XSCore syscalls and VFS/process primitives. Applications should not be embedded into the kernel once filesystem-backed loading exists.

## Git workflow

The current akaOS directory has no `.git` metadata. Do not claim commits, branches, or a clean worktree until a repository is initialized or restored. Once Git exists:

- Inspect status before editing.
- Preserve unrelated user changes.
- Use focused commits that pair implementation, tests, and `AI/` updates.
- Never rewrite history or discard changes without explicit authorization.

## Persistent-memory workflow

Before work, follow the takeover protocol in `AI/CONTEXT.md`. After significant work, build/test and update `CURRENT_STATE`, `TODO`, `CHANGELOG`, `HANDOFF`, and any architecture/decision documents affected.

For shared-runtime work, also record the exact loader and architecture matrix:
dynamic binaries must use PT_INTERP with the packaged akaOS `libc.so.6`, and
the x86_64/x86_32/AArch64/ARM32/RISC-V64/RISC-V32 loader plus compositor
regressions must be rerun before describing desktop compatibility. Keep
bounded akaOS-native compatibility clearly distinguished from running
unmodified upstream KDE/GNOME/LXDE/LXQt packages.

### 2026-09-22 verification note

When a shared libc source is added, regenerate the userspace builder scripts,
rebuild all six ports, refresh both x86 dual-firmware ISOs, and run the
compositor/client matrix. The generic D-Bus codec follows this workflow and
currently passes it. Record bounded compatibility honestly; do not turn a
self-test or a native bridge into a claim that stock upstream desktop packages
are fully supported.

After changing a wire codec, include at least one nontrivial aligned array
(`a{ss}` or `a{sv}`) in the self-test. Rebuild every architecture and rerun
both x86 ISO paths plus the four native guests; a compile-only result is not
enough for transport changes.

For pthread synchronization changes, exercise mutex contention, condition
signal/broadcast, rwlock wakeups, `pthread_once`, barriers, and semaphore
wait/post in `hello --pthread-test` on x86_64, x86_32, AArch64, ARM32,
RISC-V64, and RISC-V32. A green marker is only evidence for those bounded
paths; it does not establish full POSIX behavior or unmodified desktop support.
Futex process-shared semantics now have a parent-WAIT/child-WAKE test: the
processes map the same shared-memory object separately, and it passes on all
six guest architectures. Rerun it after changes to process-page metadata, mmap,
the scheduler, or futex handling. Process-shared pthread mutex/condition/
rwlock/barrier/semaphore tests also run on every guest. Rerun those after
changes to synchronization-object layouts or process-sharing futex selection.
The QEMU monitor harness uses a 250 ms inter-key gap because faster key-event
delivery can drop characters under guest load.

### 2026-09-23 scheduler scaling verification

The TCB table, ready bitmap, and pthread-record pool now have capacity 64.
The 24-worker TLS/join stress marker passes on all six ports in
`AKAOS_TEST_SKIP_BUILD=1 tools/test-compositor-clients.sh`; the live x86_64
`AKAOS_TEST_SKIP_BUILD=1 tools/test-desktop-readiness.sh` gate passes too.
Builds were refreshed for x86_64 UEFI/FAT32 and BIOS ISO, x86_32 dual-firmware
ISO, AArch64, ARM32, RISC-V64, and RISC-V32. The x86_32 mmap-page cap remains
128 (512 KiB) because larger values reproduce an early supervisor page fault;
do not raise it until the fixed metadata storage is safely redesigned. The
final x86_32 single ISO passed UEFI and BIOS login checks via
`tools/test-x86_32-iso.sh --no-build`; its UEFI QEMU instance needs 256 MiB
(128 MiB timed out before handoff).
