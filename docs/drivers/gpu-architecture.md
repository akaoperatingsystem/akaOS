# GPU Hardware Architecture & Driver Model for XSCore

XSCore features a modular display and GPU acceleration framework designed to scale from early legacy graphics adapters up to modern accelerators.

## Driver Abstraction Layers

1. **Boot Framebuffer & Generic Display**:
   - **UEFI GOP (Graphics Output Protocol)**: Linear 32-bit BGRX/RGBA framebuffers handed off during boot.
   - **VESA BIOS Extensions (VBE 2.0/3.0)**: Legacy BIOS protected-mode linear framebuffer switching.
   - **VirtIO-GPU**: Bounded 2D virtualized scanout and display surfaces for QEMU / cloud guests.

2. **Hardware Register & MMIO Direct Access**:
   - Memory-mapped I/O (MMIO) BAR mapping through XSCore PCI discovery.
   - 2D blitter engines, hardware cursor overlays, and page-flipping interrupts.
   - Dedicated driver matrices for NVIDIA, AMD/ATI, and Intel hardware tracked in detail in [PROGRESS.md](../../PROGRESS.md).
