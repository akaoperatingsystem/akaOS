# akaOS

[![akaOS Version](https://img.shields.io/badge/akaOS-v0.3.12-blue.svg)](sdk/include/akaos/version.h)
[![XSCore Version](https://img.shields.io/badge/XSCore-v0.2.15-green.svg)](sdk/include/akaos/version.h)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)
[![Build & Test](https://img.shields.io/badge/QEMU%20Tests-Passing-brightgreen.svg)](docs/development/workflow.md)

---

> **akaOS** is an independent, Unix-like operating system built from the ground up, centering on the custom-engineered **XSCore** kernel.

---

## 🧭 Project Philosophy
akaOS aims to provide a complete, natively-built experience. It is **not** Linux, does **not** embed the Linux kernel, and relies on Linux only as a cross-compilation host for development. Everything from the kernel and dynamic loader to the userspace and window manager stack is developed to ensure high modularity and performance.

## 🛠️ Key Architecture Components
*   **XSCore Kernel**: Modular preemptive microkernel with demand paging, COW isolation, and fine-grained process control.
*   **Native Userspace**: Custom freestanding libc, dynamic linker (`ld-akaos`), and POSIX-compliant shell.
*   **Graphical Stack**: High-performance compositor **Lumen** paired with the **Aurora** desktop shell.
*   **Portability**: Boots across 6 architectures (x86-64, x86-32, AArch64, ARM32, RISC-V64, RISC-V32).

## 📑 Getting Started
For detailed guides, refer to the [Documentation Index](docs/README.md).

- **Quick Setup**: 
  ```bash
  ./build.sh --check-deps
  ./build.sh
  ./run.sh
  ```
- **Development**: See [docs/development/workflow.md](docs/development/workflow.md).

---
## 📊 Status Dashboards
*   **[Live Progress & Readiness](PROGRESS.md)**

---

## ⚖️ License
Licensed under the [BSD 3-Clause License](LICENSE).
