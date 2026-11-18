
<p align="center">
  <h1 align="center">akaOS</h1>
  <p align="center"><strong>Independent Unix-like Operating System</strong></p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/akaOS-v0.9.11-blue.svg" alt="akaOS Version">
  <img src="https://img.shields.io/badge/XSCore-v0.8.11-green.svg" alt="XSCore Version">
  <img src="https://img.shields.io/badge/License-BSD--3--Clause-blue.svg" alt="License">
  <img src="https://img.shields.io/github/stars/akaoperatingsystem/akaOS?style=social" alt="GitHub Stars">
</p>

---

## Table of Contents
1. [Overview](#-overview)
2. [Key Subsystems](#-key-subsystems)
3. [Architecture Matrix](#-architecture-matrix)
4. [Footprint Metrics](#-footprint-metrics)
5. [Quick Start](#-quick-start)
6. [Contributing](#-contributing)

---

## Overview
**akaOS** is an independent, original Unix-like operating system engineered from the hardware up around the **XSCore** kernel.

akaOS runs entirely on its own microkernel, freestanding C runtime library (libc), dynamic ELF linker (`ld-akaos`), native display server, compositor (`lumen`), and userspace utilities. It is **not** Linux; it does not embed the Linux kernel, and does not depend on Linux at runtime.

---

## Key Subsystems

| Subsystem | Core Features |
| :--- | :--- |
| **XSCore Kernel** | Modular preemptive microkernel, 4-level paging, kernel threads, COW demand paging. |
| **Security** | Boot-time KASLR, Seccomp-BPF syscall filtering, fine-grained POSIX Capabilities. |
| **Storage** | Native drivers for NVMe, AHCI/SATA, ATA, FDC; persistent akaFS overlay. |
| **Graphics** | DRM/KMS engine, VirtIO-GPU acceleration, Lumen compositor. |
| **Network** | VirtIO-Net driver, TCP/UDP BSD sockets, ARP/IPv4 packet routing. |
| **Self-Hosting** | Native `make`, GNU Binutils, and native C compiler drivers (`cc`, `gcc`). |

---

## Architecture Matrix

| Architecture | Kernel Image | Firmware Hand-off | Storage Media |
| :--- | :--- | :--- | :--- |
| **x86-64** | `XSCORE.ELF` | UEFI / BIOS | FAT32 / ISO |
| **x86-32** | `XSCORE32.ELF` | UEFI / BIOS | FAT32 / ISO |
| **AArch64** | `XSCORE_AARCH64.ELF` | Direct | VirtIO |
| **ARM32** | `XSCORE_ARM32.ELF` | Direct | VirtIO |
| **RISC-V64**| `XSCORE_RISCV64.ELF`| Direct | VirtIO |
| **RISC-V32**| `XSCORE_RISCV32.ELF`| Direct | VirtIO |


---

## Quick Start

```sh
# 1. Automated Build (includes optimization & strip)
./build.sh

# 2. Launch interactive menu
./run.sh
```

---

## Contributing
All contributors must adhere to the **[docs/rules.md](docs/rules.md)**.
- **BSD 3-Clause License** required for all contributions.
- **Verification**: Ensure `./test.sh` and `./tools/build-arch-smoke.sh --all` pass before submitting.

---

## Star History
[![Star History Chart](https://api.star-history.com/svg?repos=akaoperatingsystem/akaOS&type=Date)](https://star-history.com/#akaoperatingsystem/akaOS&Date)
