# akaOS Documentation

Welcome to the **akaOS** technical documentation library. This directory contains detailed architectural specifications, subsystem designs, hardware porting guidelines, and contributor rules.

---

## Quick Navigation

| Topic | Document | Description |
| :--- | :--- | :--- |
| **Guidelines** | **[Contributor & AI Rules](rules.md)** | **Mandatory reading** for humans and AI coding assistants (MIT license, upstream purity). |
| **Quick Start** | **[Building & Running](BUILDING.md)** | Prerequisites, toolchain setup, interactive TUI menus, and CLI options. |
| **Architecture** | **[System Architecture](architecture.md)** | Core system design, loader-to-kernel handoffs, and subsystem layout. |
| **Boot Flow** | **[Boot Sequence](boot.md)** | From UEFI/BIOS firmware entry to userspace PID 1 initialization. |
| **Hardware Ports** | **[Portability Matrix](ports.md)** | Hardware support matrix across all 6 supported CPU architectures. |
| **Memory** | **[Memory Subsystem](memory.md)** | Physical page allocation, 4-level paging, NX, and kernel heap. |
| **Live Progress** | **[Progress Dashboard](../PROGRESS.md)** | Real-time progress bars for CPU ports, drivers, GPU matrix, and desktop environments. |

---

## Subsystem Documentation

### 1. Kernel Architecture (`docs/kernel/`)
- **[Syscall ABI & Conventions](kernel/syscalls.md)**: Calling conventions across x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32.
- **[Kernel Memory Architecture](kernel/memory.md)**: Physical bitmap allocator, 4-level paging, COW, and demand paging.

### 2. Desktop & Graphics Stack (`docs/desktop/`)
- **[X11 Server & Compositor Architecture](desktop/x11-and-compositor.md)**: Native X11 protocol server (`xserver`), Lumen compositor engine, and Wayland bridge.

### 3. Userspace & C Runtime (`docs/userspace/`)
- **[Native libc Overview](userspace/libc.md)**: Freestanding C library, dynamic ELF loader (`ld-akaos`), and POSIX compatibility layer.

### 4. Device Drivers (`docs/drivers/`)
- **[CPU Architecture & Hardware Identification](drivers/cpu-architecture.md)**: CPUID leaves, ARM MIDR_EL1, and RISC-V device-tree CSR detection.
- **[GPU Hardware Architecture](drivers/gpu-architecture.md)**: Framebuffer abstraction, UEFI GOP, VirtIO-GPU, and discrete hardware driver models.
- **[Comprehensive Hardware Matrices](../PROGRESS.md#supported-cpu-architectures)**: Live readiness tracking for CPUs, GPUs, and Network hardware.

### 5. Development & Testing (`docs/development/`)
- **[Software Bundles & Library Architecture](development/software-bundles.md)**: Detailed breakdown of each bundle profile (`all`, `desktop`, `dev`, `net`, `manual`, `minimal`, `none`) and which libraries are included.
- **[Developer Workflow](development/workflow.md)**: Automated regression scripts, QEMU validation, and contribution checklists.
