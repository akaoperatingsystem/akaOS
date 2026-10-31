# akaOS Development Progress

[![akaOS Version](https://img.shields.io/badge/akaOS-v0.1.3-blue.svg)](sdk/include/akaos/version.h)
[![XSCore Version](https://img.shields.io/badge/XSCore-v0.1.3-green.svg)](sdk/include/akaos/version.h)

This document tracks the live implementation progress, hardware readiness, subsystem maturity, and desktop environment compatibility for **akaOS** and the **XSCore** kernel.

---

## Supported CPU Architectures

| Architecture | Port Status & Kernel Target | Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **x86-64 (AMD64)** | Full Image Boot-Tested (UEFI GOP & BIOS Multiboot2, SMP, PCI, VirtIO) | 98% | `[██████████]` 98% |
| **x86-32 (i386)** | Full Image Boot-Tested (UEFI FAT32 & BIOS ATA/IDE Persistence) | 92% | `[█████████░]` 92% |
| **AArch64 (ARM 64-bit)** | Native Kernel & Userspace Boot-Tested (MMU, Exceptions, VirtIO-MMIO) | 88% | `[████████░░]` 88% |
| **ARM32 (ARMv7-A)** | Native Kernel & Userspace Boot-Tested (MMU, Direct Kernel QEMU `virt`) | 85% | `[████████░░]` 85% |
| **RISC-V64 (RV64GC)** | Native Kernel & Userspace Boot-Tested (Sv39 Paging, VirtIO-MMIO) | 85% | `[████████░░]` 85% |
| **RISC-V32 (RV32GC)** | Native Kernel & Userspace Boot-Tested (Sv32 Paging, Direct Kernel QEMU `virt`) | 82% | `[████████░░]` 82% |

---

## System Subsystems & Hardware Drivers

| Target Subsystem | Scope / Implementation | Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **Memory Management** | 4-Level Paging, Physical Page Allocator, NX, Guard Stacks, COW, Demand Paging | 95% | `[█████████░]` 95% |
| **Process & Scheduling** | Preemptive Multitasking, Local APIC Timer, POSIX Fork/Execve, Signals | 88% | `[████████░░]` 88% |
| **GPU & Display Drivers** | Framebuffer (`/dev/fb0`), Lumen Compositor, X11 Display Server, Mode Query | 85% | `[████████░░]` 85% |
| **Storage & akaFS Overlay** | VirtIO-Block, IDE/ATA PIO Fallback, Persistent Flash Overlay VFS | 85% | `[████████░░]` 85% |
| **Native libc & Linker** | Freestanding C Runtime, Dynamic Linker (`ld-akaos`), POSIX ABI Subset | 82% | `[████████░░]` 82% |
| **Networking Stack** | VirtIO-Net, IPv4/ARP/UDP/TCP Sockets, DNS Resolver, HTTP Server | 80% | `[████████░░]` 80% |
| **Audio Subsystem** | AC97 / Intel HDA Sound Driver Foundation, `/dev/audio` Device Stream | 70% | `[███████░░░]` 70% |

---

## Desktop Environments & Window Managers

All desktop environments and window managers run as **completely unmodified upstream releases**; all required compatibility is implemented directly within akaOS subsystems.

| Desktop / Window Manager | Upstream Version | Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **Openbox WM** | Unmodified Upstream 3.6.1 | 100% | `[██████████]` 100% |
| **LXDE (Openbox + LXPanel + PCManFM)** | Unmodified Upstream | 90% | `[█████████░]` 90% |
| **LXQt Desktop & Session** | Unmodified Upstream | 65% | `[██████░░░░]` 65% |
| **XFCE Desktop & Xfwm4** | Unmodified Upstream | 35% | `[███░░░░░░░]` 35% |
| **MATE Desktop & Marco** | Unmodified Upstream | 30% | `[███░░░░░░░]` 30% |
| **KDE Plasma & KWin** | Unmodified Upstream | 25% | `[██░░░░░░░░]` 25% |
| **GNOME Desktop & Mutter** | Unmodified Upstream | 20% | `[██░░░░░░░░]` 20% |
| **Cinnamon Desktop & Muffin** | Unmodified Upstream | 20% | `[██░░░░░░░░]` 20% |
| **Overall System Completion** | Full Native Operating System | 65% | `[██████░░░░]` 65% |
