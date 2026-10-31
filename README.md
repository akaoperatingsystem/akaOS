# akaOS

[![akaOS Version](https://img.shields.io/badge/akaOS-v0.1.18-blue.svg)](sdk/include/akaos/version.h)
[![XSCore Version](https://img.shields.io/badge/XSCore-v0.1.10-green.svg)](sdk/include/akaos/version.h)

akaOS is an independent Unix-like operating system built from scratch around the **XSCore** kernel. It runs entirely on its own kernel and userspace — it does not use the Linux kernel or Linux userspace at runtime. Linux is used solely as a cross-compilation host.

---

For detailed live progress across CPU architectures, device drivers, and desktop environments, see **[PROGRESS.md](PROGRESS.md)**.

akaOS provides verified native boot paths across six CPU architectures: **x86-64**, **x86-32**, **AArch64**, **ARM32**, **RISC-V64**, and **RISC-V32**.

---

## Highlights

- **Custom Kernel (XSCore)**: Written from scratch with virtual memory (four-level paging), cooperative Ring 0 kernel threads, preemptive Ring 3 multitasking, task-owned address spaces, and Local APIC / timer interrupts.
- **6 Hardware Architectures**: Verified boot across x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32 under QEMU.
- **Dual Firmware Support**: Boots via UEFI (GOP framebuffer, memory map handoff) and legacy BIOS (Multiboot2) on x86 platforms.
- **Native Userspace & C Runtime**: Independent C library (libc), dynamic linker (`ld-akaos`), POSIX syscall ABI subset, and standard Unix shell (`akash`).
- **Complete CLI Toolkit**: Over 100 native command-line utilities including `ls`, `cat`, `grep`, `sed`, `tar`, `ps`, `top`/`sysmon`, `ping`, `curl`, `ssh`, `sshd`, `httpd`, and more.
- **Display Server & GUI Ecosystem**:
  - Native compositor **Lumen** and resident shell **Aurora**.
  - Custom X11 server (`xserver`) and Wayland bridge.
  - Upstream porting layers for Cairo, Pango, HarfBuzz, FreeType, Fontconfig, GLib, GTK3/GTK4, Qt6, and LXQt.
- **akaFS Persistent Storage**: Dynamic VFS with an overlay filesystem backing persistent storage on VirtIO or ATA/IDE disks across reboots.

---

## Architecture & Boot Support

| Architecture | Kernel Image | Boot Firmware | Disk / Media | Test Environment |
| :--- | :--- | :--- | :--- | :--- |
| **x86-64** | `XSCORE.ELF` | UEFI / SeaBIOS | FAT32 (`.img`) / ISO (`.iso`) | QEMU `q35` / OVMF |
| **x86-32** | `XSCORE32.ELF` | UEFI / SeaBIOS | FAT32 (`.img`) / ISO (`.iso`) | QEMU `pc` / SeaBIOS |
| **AArch64** | `XSCORE_AARCH64.ELF` | Direct kernel | VirtIO MMIO | QEMU `virt` |
| **ARM32** | `XSCORE_ARM32.ELF` | Direct kernel | VirtIO MMIO | QEMU `virt` |
| **RISC-V64** | `XSCORE_RISCV64.ELF` | Direct kernel | VirtIO MMIO | QEMU `virt` |
| **RISC-V32** | `XSCORE_RISCV32.ELF` | Direct kernel | VirtIO MMIO | QEMU `virt` |

For technical specifications, memory layout, and port status, see:
- [docs/architecture.md](docs/architecture.md) — System architecture, syscall ABI, and kernel subsystems.
- [docs/boot.md](docs/boot.md) — Boot flow from firmware to user session.
- [docs/memory.md](docs/memory.md) — Physical and virtual memory management.
- [docs/ports.md](docs/ports.md) — Architecture portability matrix.

---

## Quick Start

### 1. Prerequisites

On Windows, use **WSL (Ubuntu)**:

```powershell
wsl.exe -d Ubuntu
```

On Linux or WSL, check missing toolchain dependencies:

```sh
./build.sh --check-deps
```

To install the full toolchain across distributions:

- **Ubuntu / Debian**:
  ```sh
  sudo apt update && sudo apt install -y clang lld llvm binutils python3 python3-pil \
    fonts-dejavu-core dosfstools mtools grub-pc-bin grub-common grub2-common \
    grub-efi-amd64-bin grub-efi-ia32-bin xorriso qemu-system-x86 qemu-system-arm \
    qemu-system-misc ovmf whiptail
  ```
- **Arch Linux**:
  ```sh
  sudo pacman -S --needed clang lld llvm binutils python python-pillow \
    ttf-dejavu dosfstools mtools grub xorriso qemu-full edk2-ovmf libnewt
  ```
- **Fedora**:
  ```sh
  sudo dnf install -y clang lld llvm binutils python3 python3-pillow \
    dejavu-sans-fonts dosfstools mtools grub2-tools grub2-tools-extra \
    grub2-pc-modules grub2-efi-x64-modules grub2-efi-ia32-modules xorriso \
    qemu-system-x86 qemu-system-arm qemu-system-aarch64 qemu-system-riscv \
    edk2-ovmf newt
  ```

See [docs/BUILDING.md](docs/BUILDING.md) for openSUSE and Alpine instructions.

---

### 2. Build & Run (Interactive Menu)

Running without arguments opens a full-screen terminal configuration menu:

```sh
# 1. Build kernel and image
./build.sh

# 2. Boot inside QEMU
./run.sh
```

*Navigation:* Use Arrow keys to navigate, Tab to switch buttons, Enter to select, and Escape to cancel. If `whiptail` is not available, a clean text-based menu is shown automatically.

---

### 3. Non-Interactive CLI Commands

You can build and launch target configurations directly without opening menus:

```sh
# x86-64 UEFI (Default)
./build.sh --arch x86_64 --firmware uefi --disk-type fat32
./run.sh --arch x86_64 --firmware uefi --disk-type fat32 --gui

# x86-64 Dual UEFI/BIOS Hybrid ISO
./build.sh --arch x86_64 --firmware both --disk-type iso
./run.sh --arch x86_64 --firmware bios --disk-type iso

# x86-32 BIOS (ATA/IDE persistent storage)
./build.sh --arch x86_32 --firmware bios --disk-type iso
./run.sh --arch x86_32 --firmware bios --disk-type iso

# AArch64 (ARM 64-bit)
./build.sh --arch aarch64
./run.sh --arch aarch64 --headless

# RISC-V 64-bit
./build.sh --arch riscv64
./run.sh --arch riscv64 --headless
```

#### Key Options for `run.sh`
- `--gui`: Launch with graphical display output (QEMU GTK/SDL window).
- `--headless`: Run purely over serial console in the terminal.
- `./debug.sh`: Boot paused with a GDB server listening on `localhost:1234`.

---

## Default Credentials

The generated development image includes two pre-configured accounts:

| Username | Password | Privileges | Initial Shell Prompt |
| :--- | :--- | :--- | :--- |
| **`root`** | `root` | Superuser / Administrator | `#` |
| **`user`** | `akaos` | Standard User | `$` |

*Security notice: Change these passwords with `passwd` before deploying images outside local development.*

---

## Operating System Structure

```text
akaOS/
├── boot/              # UEFI bootloader (BOOTX64.EFI) and handoff protocol
├── xscore/            # Core operating system kernel
│   ├── arch/          # Architecture HAL (x86_64, x86_32, aarch64, arm32, riscv64, riscv32)
│   ├── kernel/        # Scheduler, processes, threads, VFS, syscalls, IPC, TTY, APIC
│   └── memory/        # Physical bitmap allocator, 4-level paging, kernel heap
├── userspace/         # Native userspace ecosystem
│   ├── bin/           # 100+ native user applications, shells, servers, and GUIs
│   ├── libc/          # Freestanding C library, math, pthreads, POSIX wrapper
│   ├── linker/        # Dynamic ELF linker (ld-akaos)
│   └── etc/           # System configuration, passwd, services
├── sdk/               # Shared kernel-to-userspace ABI and boot headers
├── tools/             # Cross-compilation scripts, QEMU test harnesses, patch toolchain
└── docs/              # In-depth architectural and developer documentation
```

---

## Verification & Testing

akaOS maintains an extensive automated QEMU test suite:

```sh
# Run full automated boot & regression test
./test.sh

# Verify all 6 architecture builds (smoke test)
./tools/build-arch-smoke.sh --all

# Test filesystem persistence across reboots
./tools/test-persistence.sh

# Test ISO booting under both UEFI and SeaBIOS
./tools/test-iso.sh

# Test user authentication and privilege separation
./tools/test-auth.sh
```

---

## Contributing, Bug Reports & Pull Requests

We welcome issues, feature requests, bug reports, and pull requests!

### Reporting Bugs
If you find a bug, crash, or hardware incompatibility:
1. Open an issue on our [GitHub Issues](https://github.com/akaoperatingsystem/akaOS/issues) tracker.
2. Include the targeted architecture (`x86-64`, `x86-32`, `AArch64`, `ARM32`, `RISC-V64`, or `RISC-V32`) and firmware (`UEFI` or `BIOS`).
3. Attach relevant reproduction steps and the serial boot/panic output (or output from `./test.sh`).

### Submitting Pull Requests
1. **Fork** the repository and create your feature branch:
   ```sh
   git checkout -b feature/your-feature-name
   ```
2. **Follow Upstream Purity**: If adding support for window managers or desktop environments, do **not** patch or modify upstream code. All compatibility shims must be implemented inside akaOS's own subsystems (kernel, libc, display server, or Wayland bridge).
3. **Verify Builds & Tests**:
   ```sh
   # Verify all 6 architecture ports build cleanly
   ./tools/build-arch-smoke.sh --all

   # Run automated QEMU regression tests
   ./test.sh
   ```
4. **Submit a Pull Request** against the `main` branch on [GitHub Pull Requests](https://github.com/akaoperatingsystem/akaOS/pulls).

---

## License

akaOS is open-source software licensed under the [MIT License](LICENSE).  
You are free to use, modify, distribute, and fork this project for personal, educational, or commercial purposes.