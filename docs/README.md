# akaOS Technical Documentation Index

Welcome to the technical documentation for **akaOS** and the **XSCore** kernel.

---

## Documentation Categories

### 1. Contributor & AI Guidelines
- **[Contributor & AI Guidelines](rules.md)**: **Mandatory reading for all human developers and anyone using AI tools.** Details the universal MIT License requirement, upstream desktop purity rule, and code quality standards.

### 2. Architecture & Kernel (XSCore)
- **[System Architecture](architecture.md)**: Overall operating system layout and design philosophy.
- **[Kernel Syscalls & ABI](kernel/syscalls.md)**: Syscall calling conventions across all 6 architectures.
- **[Kernel Memory Architecture](kernel/memory.md)**: Physical bitmap allocator, 4-level paging, COW, and demand paging.
- **[Boot Sequence](boot.md)**: Boot sequence from OVMF UEFI / SeaBIOS to user session.
- **[Architecture Ports](ports.md)**: Porting matrix for x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32.

### 3. Display, Desktop & Window Managers
- **[X11 Server & Compositor Architecture](desktop/x11-and-compositor.md)**: Native X11 server (`xserver`), Lumen compositor, and Wayland bridge.

### 4. Userspace & C Runtime
- **[Native libc Overview](userspace/libc.md)**: Freestanding C runtime, POSIX emulation layer, and dynamic linker (`ld-akaos`).

### 5. Device Drivers & Hardware
- **[GPU Hardware Architecture](drivers/gpu-architecture.md)**: Framebuffer drivers, UEFI GOP, VirtIO-GPU, and discrete GPU drivers.
- **[GPU Hardware Driver Matrix](../PROGRESS.md#gpu-hardware-drivers--target-support-matrix)**: Live readiness tracking for NVIDIA, AMD, and Intel GPUs.

### 6. Development & Verification
- **[Building & Running](BUILDING.md)**: Host packages, toolchains, and interactive configuration menus.
- **[Developer Workflow](development/workflow.md)**: Testing guidelines, automated QEMU verification, and porting checklists.
