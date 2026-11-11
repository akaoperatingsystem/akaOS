# akaOS

[![akaOS Version](https://img.shields.io/badge/akaOS-v0.3.53-blue.svg)](sdk/include/akaos/version.h)
[![XSCore Version](https://img.shields.io/badge/XSCore-v0.2.38-green.svg)](sdk/include/akaos/version.h)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)
[![Build & Test](https://img.shields.io/badge/QEMU%20Tests-Passing-brightgreen.svg)](docs/development/workflow.md)

**akaOS** is an original, independent Unix-like operating system engineered from scratch around the **XSCore** kernel.

akaOS runs entirely on its own kernel, freestanding C library (libc), dynamic loader (`ld-akaos`), native display server, and userspace applications. It is **not** Linux, does **not** embed the Linux kernel, and does **not** rely on Linux at runtime (Linux is used solely as a development cross-compilation host).

---

## Live Progress & Status

Track real-time progress across hardware architectures, graphics drivers, desktop environments and more in **[PROGRESS.md](PROGRESS.md)**.

---

## Key Features

- **XSCore Kernel**: Modular preemptive microkernel with 4-level paging, guarded stacks, Local APIC timer preemption, kernel threads, real UID/GID credentials, and demand paging with Copy-on-Write (COW).
- **6 Hardware Architectures**: Bootable native paths across x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32 under QEMU.
- **Dual Firmware Support**: Boots via UEFI (GOP framebuffer & memory handoff) and legacy BIOS (Multiboot2) on x86 platforms.
- **Native Userspace & C Runtime**: Freestanding C runtime (libc), dynamic ELF linker (`ld-akaos`), POSIX syscall ABI subset, and standard Unix shell (`akash`).
- **Complete CLI Toolkit**: Over 100 native command-line utilities including `ls`, `cat`, `grep`, `sed`, `tar`, `ps`, `sysmon`, `curl`, `ssh`, `sshd`, `httpd`, and network diagnostics.
- **Native Graphical Stack**:
  - Native compositor **Lumen** and resident desktop shell **Aurora**.
  - Custom X11 display server (`xserver`) supporting core X11 protocols, `SHAPE`, `X-Resource`, and `BIG-REQUESTS`.
  - Upstream porting layer for unmodified desktop software (Cairo, Pango, HarfBuzz, FreeType, Fontconfig, GLib, GTK3/GTK4, Qt6, LXQt, and LXDE).
- **Persistent Storage (akaFS)**: Dynamic VFS with an overlay filesystem backing persistent storage on VirtIO or ATA/IDE disks across reboots.

---

## Architecture Portability Matrix

| Architecture | Kernel Binary | Firmware Hand-off | Storage Media | QEMU Target |
| :--- | :--- | :--- | :--- | :--- |
| **x86-64** | `XSCORE.ELF` | UEFI / SeaBIOS | FAT32 (`.img`) / ISO (`.iso`) | `q35` / OVMF |
| **x86-32** | `XSCORE32.ELF` | UEFI / SeaBIOS | FAT32 (`.img`) / ISO (`.iso`) | `pc` / SeaBIOS |
| **AArch64** | `XSCORE_AARCH64.ELF` | Direct kernel | VirtIO MMIO | `virt` |
| **ARM32** | `XSCORE_ARM32.ELF` | Direct kernel | VirtIO MMIO | `virt` |
| **RISC-V64** | `XSCORE_RISCV64.ELF` | Direct kernel | VirtIO MMIO | `virt` |
| **RISC-V32** | `XSCORE_RISCV32.ELF` | Direct kernel | VirtIO MMIO | `virt` |

For detailed technical specifications, explore the [Documentation Index](docs/README.md).

---

## Quick Start

### 1. Host Setup & Toolchain Check

On Windows, use **Ubuntu WSL**:
```powershell
wsl.exe -d Ubuntu
```

Verify build requirements using the automated dependency assistant:
```sh
./build.sh --check-deps
```

#### Installing Dependencies

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

### 2. Build & Boot (Interactive Menu)

Running without arguments opens a full-screen terminal menu:

```sh
# 1. Build kernel and disk image
./build.sh

# 2. Boot inside QEMU
./run.sh
```

*Navigation:* Arrow keys to move, Tab to switch buttons, Enter to select, and Escape to cancel. (Falls back to a clean text-based menu if `whiptail` is not installed).

---

### 3. Non-Interactive CLI Commands

You can build and launch target configurations directly:

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
<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:5207c3c0abb4ef4237a4fb7972952ccc05acbc6e20b7a45678d8e2c6823762e8 -->

- `./debug.sh`: Boot paused with a GDB server on a Unix socket in a private temporary directory. Use the printed attach command (GDB 9 or newer).

<!-- /coderabbit-ai-generated:v1 -->

---

## Default Credentials

The generated development image includes two pre-configured accounts:

| Username | Password | Privileges | Initial Shell Prompt |
| :--- | :--- | :--- | :--- |
| **`root`** | `root` | Superuser / Administrator | `#` |
| **`user`** | `akaos` | Standard User | `$` |

*Security notice: Change these passwords with `passwd` before deploying images outside local development.*

---

## Repository Structure

```text
akaOS/
├── boot/              # UEFI bootloader (BOOTX64.EFI) and handoff protocol
├── xscore/            # XSCore kernel (core operating system)
│   ├── arch/          # Architecture HAL (x86_64, x86_32, aarch64, arm32, riscv64, riscv32)
│   ├── kernel/        # Scheduler, processes, threads, VFS, syscalls, IPC, TTY, APIC
│   └── memory/        # Physical bitmap allocator, 4-level paging, kernel heap
├── userspace/         # Native userspace ecosystem
│   ├── bin/           # 100+ native applications, shells, display servers, and GUIs
│   ├── libc/          # Freestanding C library, math, pthreads, POSIX wrapper
│   ├── linker/        # Dynamic ELF linker (ld-akaos)
│   └── etc/           # System configuration, passwd, services
├── sdk/               # Shared kernel-to-userspace ABI and boot headers
├── tools/             # Cross-compilation scripts, QEMU test harnesses, patch toolchain
└── docs/              # In-depth technical documentation categorized by subsystem
```

---

## Verification & Automated Testing

akaOS maintains an extensive automated QEMU regression suite:

```sh
# Run full automated boot & regression test
./test.sh

# Verify all 6 architecture compiler targets (smoke test)
./tools/build-arch-smoke.sh --all

# Test filesystem persistence across reboots
./tools/test-persistence.sh

# Test ISO booting under both UEFI and SeaBIOS
./tools/test-iso.sh

# Test user authentication and privilege separation
./tools/test-auth.sh
```

---

## Contributor & AI Guidelines

All contributors — **both human developers and anyone utilizing AI coding assistants** (Cursor, Copilot, DeepSeek, Claude, ChatGPT, etc.) — **must read and follow the mandatory guidelines** in **[docs/rules.md](docs/rules.md)**:

1. **Universal BSD 3-Clause License**: Every single line of code, header, script, and documentation contributed to akaOS must be licensed under the [BSD 3-Clause License](LICENSE). Incompatible or copyleft code must never be introduced into core subsystems.
2. **Upstream Desktop Purity**: All desktop environments (GNOME, KDE, XFCE, MATE, Cinnamon, LXQt, LXDE) and window managers must run as **completely unmodified upstream software**. Compatibility belongs inside akaOS subsystems (kernel, libc, display server), never by patching desktop code.
3. **Code Quality & Verification**: All code (human-written or AI-assisted) must be verified against automated QEMU regression scripts (`./test.sh`) and architecture targets (`./tools/build-arch-smoke.sh --all`).
4. **Independent Dual Versioning**: Follow the semantic versioning contract for akaOS and the XSCore kernel defined in [`sdk/include/akaos/version.h`](sdk/include/akaos/version.h).

---

## Contributing, Bug Reports & Pull Requests

We welcome issues, feature requests, bug reports, and pull requests!

### Reporting Bugs
If you find a bug, crash, or hardware incompatibility:
1. Open an issue on our [GitHub Issues](https://github.com/akaoperatingsystem/akaOS/issues) tracker.
2. Include the targeted architecture (`x86-64`, `x86-32`, `AArch64`, `ARM32`, `RISC-V64`, or `RISC-V32`) and firmware (`UEFI` or `BIOS`).
3. Attach relevant reproduction steps and serial boot logs.

### Submitting Pull Requests
1. **Fork** the repository and create your feature branch:
   ```sh
   git checkout -b feature/your-feature-name
   ```
2. **Adhere to [docs/rules.md](docs/rules.md)** (BSD 3-Clause License and zero-patching upstream rule).
3. **Verify Builds & Tests**:
   ```sh
   ./tools/build-arch-smoke.sh --all
   ./test.sh
   ```
4. **Submit a Pull Request** against the `main` branch on [GitHub Pull Requests](https://github.com/akaoperatingsystem/akaOS/pulls).

---

## License

akaOS is open-source software licensed under the **[BSD 3-Clause License](LICENSE)**.
You are completely free to use, modify, distribute, and fork this project for personal, educational, or commercial purposes.
