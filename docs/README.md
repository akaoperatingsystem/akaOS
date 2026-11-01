# akaOS Technical Documentation Index

Welcome to the comprehensive technical documentation for **akaOS** and the **XSCore** kernel.

---

## Documentation Categories

### 1. AI Rules & Contributor Standards
- **[AI Rules & Directives](ai/rules.md)**: **Mandatory reading for all AI agents and contributors using AI tooling.** Covers universal MIT licensing, zero-patching desktop purity, dual versioning, and real-time Git requirements.

### 2. Architecture & Kernel (XSCore)
- **[System Architecture](architecture.md)**: Overall operating system layout, kernel design, and subsystem communication.
- **[Kernel Syscalls & ABI](kernel/syscalls.md)**: Register calling conventions, syscall tables, and libc dispatch across all 6 architectures.
- **[Memory Foundation](memory.md)**: Physical page allocation, 4-level paging, NX, CR0.WP, and virtual memory layout.
- **[Boot Flow](boot.md)**: Boot sequence from OVMF UEFI / SeaBIOS to kernel initialization and userspace handoff.
- **[Architecture Ports](ports.md)**: Porting matrix for x86-64, x86-32, AArch64, ARM32, RISC-V64, and RISC-V32.

### 3. Display, Desktop & Window Managers
- **[X11 Server & Compositor Architecture](desktop/x11-and-compositor.md)**: Architecture of the native X11 server (`xserver`), Lumen compositor, and Wayland bridge.

### 4. Device Drivers & Hardware
- **[GPU Hardware Architecture](drivers/gpu-architecture.md)**: Overview of display adapters, UEFI GOP, VirtIO-GPU, and discrete GPU drivers.
- **[Full GPU Driver Matrix](../PROGRESS.md#gpu-hardware-drivers--target-support-matrix)**: Live readiness tracking for NVIDIA, AMD, and Intel GPUs.

### 5. Development & Verification
- **[Building & Running](BUILDING.md)**: Toolchain requirements, package commands, interactive menus, and QEMU options.
