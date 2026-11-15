# akaOS

[![akaOS Version](https://img.shields.io/badge/akaOS-v0.6.5-blue.svg)](sdk/include/akaos/version.h)
[![XSCore Version](https://img.shields.io/badge/XSCore-v0.5.5-green.svg)](sdk/include/akaos/version.h)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)
[![Build & Test](https://img.shields.io/badge/QEMU%20Tests-Passing-brightgreen.svg)](docs/development/workflow.md)

**akaOS** is an independent, original Unix-like operating system engineered from the hardware up around the **XSCore** kernel.

akaOS runs entirely on its own microkernel, freestanding C runtime library (libc), dynamic ELF linker (`ld-akaos`), native display server, compositor (`lumen`), and userspace utilities. It is **not** Linux, does **not** embed the Linux kernel, and does **not** depend on Linux at runtime (Linux is used solely as a cross-compilation build host).

---

## Live Progress & Status

Track real-time engineering milestones across all subsystems, hardware drivers, and size metrics in **[PROGRESS.md](PROGRESS.md)**.

---

## Key Subsystems & Architecture

- **XSCore Microkernel**: Modular, preemptive kernel featuring 4-level hierarchical paging, guarded kernel stacks with ISTs, Local APIC timer preemption, kernel threads, real UID/GID process credentials, and demand paging with Copy-on-Write (COW).
- **Kernel Security Hardening**:
  - **KASLR**: Boot-time physical and virtual address slide using hardware entropy (RDRAND, RDTSC, APIC timer, and memory map).
  - **Seccomp-BPF**: In-kernel classical Berkeley Packet Filter (cBPF) execution engine filtering system calls with `prctl(PR_SET_SECCOMP)` and `seccomp(SECCOMP_SET_MODE_FILTER)`.
  - **POSIX Capabilities**: Fine-grained capability bitmask (`akaos_cap_t`) replacing monolithic root checks across process management, raw I/O, network administration, and VFS permissions.
- **6 Hardware Architectures**: Native boot and execution paths across **x86-64**, **x86-32**, **AArch64**, **ARM32**, **RISC-V64**, and **RISC-V32**.
- **Dual Firmware Integration**:
  - **UEFI Runtime Services**: Preserved runtime tables (`GetTime`, `SetTime`, `GetVariable`, `SetVariable`, `ResetSystem`), native NVRAM variable store accessed via `/dev/efivar`, Secure Boot key validation chain (`PK`, `KEK`, `db`, `dbx`), and Capsule Update protocol.
  - **Legacy BIOS Fallback**: BIOS Data Area (BDA at `0x400`), Extended BIOS Data Area (EBDA), and protected-to-real-mode thunking interface for BIOS interrupts (INT 10h VBE, INT 13h Disk, INT 15h E820).
- **Storage Subsystems & Hardware Drivers**:
  - **NVMe**: PCIe BAR MMIO discovery, Admin/IO queue management, 64-bit DMA, and SMART telemetry (`smartctl`).
  - **AHCI / SATA**: HBA reset, Global Host Control (GHC), BOHC handoff, FIS/PRDT scatter-gather DMA, LBA48 read/write, SMART diagnostics, and TRIM.
  - **ATA / IDE**: Direct PIO multi-sector transfer engine for legacy storage.
  - **Floppy Disk Controller (FDC)**: Standard PC 82078 FDC driver with MSR status polling, cylinder seek calibration, and disk formatting ioctls.
  - **Buffer Cache (bufcache)**: In-memory LRU block cache with hash-based sector lookup and dirty-page synchronization.
  - **Persistent Filesystem (akaFS)**: VFS overlay filesystem preserving changes across reboots on VirtIO and ATA/IDE storage.
- **Graphics & KMS**:
  - **DRM/KMS Engine**: Direct Rendering Manager with atomic mode setting (`crtc`, `connector`, `plane`), GEM memory buffer allocation, and fence-based command synchronization.
  - **Hardware Drivers**: VirtIO-GPU hardware accelerator and Bochs VBE display adapter.
  - **Compositor Integration**: Native **Lumen** compositor operating with atomic page flipping and hardware cursor overlay planes.
- **Hardware-Compliant Network Stack**:
  - **VirtIO-Net Driver**: DMA-based transmit and receive descriptor rings with interrupt completion handling.
  - **L2 / L3 / L4 Protocols**: Ethernet II framing, ARP cache resolution, IPv4 packet routing and header checksumming, ICMP Echo request/reply engine (real `ping`), and BSD socket transport (UDP and TCP).
- **Power & Platform Telemetry**:
  - **ACPI**: Dual-mode RSDP discovery (UEFI configuration tables and BIOS memory scanning), XSDT/RSDT traversal, FADT, MADT, and DSDT parsing.
  - **Platform Management**: Thermal zone temperature reporting, battery state monitoring, and system power transitions (`pmctl poweroff`, `pmctl reboot`).
- **Input Subsystem**:
  - Interrupt-driven drivers for PS/2 keyboard and mouse (standard and Intellimouse scroll wheel packets).
  - VirtIO input device driver (keyboard, mouse, tablet).
  - Serial mouse and USB HID device drivers.
- **Color-Coded Console & Diagnostics**:
  - Built-in ANSI/VT100 SGR color escape sequence parser supporting full 8/16-color palettes in framebuffer console and serial output.
  - Standardized diagnostic status tags (`[ OK ]`, `[INFO]`, `[WARN]`, `[FAIL]`) for verbose boot mode.
- **Native Self-Hosting Toolchain**:
  - **Make**: Native POSIX and GNU Make-compatible build engine supporting variable expansion, pattern rules, and dependency resolution.
  - **Binutils**: Native archiver (`ar`), symbol lister (`nm`), object dumper (`objdump`), assembler driver (`as`), and linker driver (`ld`).
  - **Compiler Drivers**: Native `cc` and `gcc` drivers for C source compilation on akaOS.
- **Complete CLI Toolkit**: Over 124 native command-line utilities including `akash`, `bash`, `ls`, `cat`, `grep`, `sed`, `awk`, `tar`, `patch`, `diff`, `curl`, `ssh`, `sshd`, `httpd`, `efivar`, and `smartctl`.

---

## Architecture Portability Matrix

| Architecture | Target Triple | Kernel Image | Firmware Hand-off | Storage Media | Default QEMU Target |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **x86-64** | `x86_64-unknown-none-elf` | `XSCORE.ELF` | UEFI (GOP) / BIOS (Multiboot2) | FAT32 (`.img`) / Dual Hybrid ISO (`.iso`) | `q35` / OVMF |
| **x86-32** | `i386-unknown-none-elf` | `XSCORE32.ELF` | UEFI / SeaBIOS (Multiboot2) | FAT32 (`.img`) / Hybrid ISO (`.iso`) | `pc` / SeaBIOS |
| **AArch64** | `aarch64-unknown-none-elf` | `XSCORE_AARCH64.ELF` | Direct kernel boot | VirtIO MMIO / RAMFB | `virt` / Cortex-A57 |
| **ARM32** | `armv7-unknown-none-eabi` | `XSCORE_ARM32.ELF` | Direct kernel boot | VirtIO MMIO / RAMFB | `virt` / Cortex-A7 |
| **RISC-V64** | `riscv64-unknown-none-elf` | `XSCORE_RISCV64.ELF` | Direct kernel boot | VirtIO MMIO / RAMFB | `virt` / RV64GC |
| **RISC-V32** | `riscv32-unknown-none-elf` | `XSCORE_RISCV32.ELF` | Direct kernel boot | VirtIO MMIO / RAMFB | `virt` / RV32GC |

---

## Quick Start

### 1. Host Setup & Dependencies

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
    qemu-system-misc ovmf whiptail zstd
  ```
- **Arch Linux**:
  ```sh
  sudo pacman -S --needed clang lld llvm binutils python python-pillow \
    ttf-dejavu dosfstools mtools grub xorriso qemu-full edk2-ovmf libnewt zstd
  ```
- **Fedora**:
  ```sh
  sudo dnf install -y clang lld llvm binutils python3 python3-pillow \
    dejavu-sans-fonts dosfstools mtools grub2-tools grub2-tools-extra \
    grub2-pc-modules grub2-efi-x64-modules grub2-efi-ia32-modules xorriso \
    qemu-system-x86 qemu-system-arm qemu-system-aarch64 qemu-system-riscv \
    edk2-ovmf newt zstd
  ```

---

### 2. Build & Launch

Run without arguments to launch the interactive configuration menu:

```sh
# 1. Build kernel, toolchain, and system images
./build.sh

# 2. Boot inside QEMU
./run.sh
```

#### Direct Command Line Targets

```sh
# x86-64 UEFI (Default)
./build.sh --arch x86_64 --firmware uefi --disk-type fat32
./run.sh --arch x86_64 --firmware uefi --disk-type fat32 --gui

# x86-64 Dual UEFI/BIOS Hybrid ISO (Optimized <30MB)
./build.sh --arch x86_64 --firmware both --disk-type iso
./run.sh --arch x86_64 --firmware bios --disk-type iso

# x86-32 BIOS (Optimized ~12MB)
./build.sh --arch x86_32 --firmware bios --disk-type iso
./run.sh --arch x86_32 --firmware bios --disk-type iso

# AArch64 (ARM 64-bit)
./build.sh --arch aarch64
./run.sh --arch aarch64 --headless

# RISC-V 64-bit
./build.sh --arch riscv64
./run.sh --arch riscv64 --headless
```

---

## Verification & Automated Testing

akaOS maintains an extensive automated regression test harness:

```sh
# Verify all 6 architecture compiler targets (smoke build)
./tools/build-arch-smoke.sh --all

# Run full automated boot and regression suite
./test.sh

# Test ISO booting under both UEFI and SeaBIOS
./tools/test-iso.sh

# Test user authentication and privilege separation
./tools/test-auth.sh

# Test filesystem persistence across reboots
./tools/test-persistence.sh
```

---

## Default Credentials

| Username | Password | Privileges | Initial Shell Prompt |
| :--- | :--- | :--- | :--- |
| **`root`** | `root` | Superuser / Administrator | `#` |
| **`user`** | `akaos` | Standard User | `$` |

---

## License

akaOS is open-source software licensed under the **[BSD 3-Clause License](LICENSE)**.

---

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=akaoperatingsystem/akaOS&type=Date)](https://star-history.com/#akaoperatingsystem/akaOS&Date)
