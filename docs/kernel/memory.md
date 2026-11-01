# XSCore Memory Subsystem Architecture

The XSCore kernel provides a robust virtual and physical memory management architecture across 64-bit and 32-bit CPU architectures.

## Subsystems

1. **Physical Page Allocator**:
   - Bitmap-tracked page allocator managing conventional physical RAM reported by firmware (UEFI GOP/E820/DeviceTree).
   - Guarded pages for kernel bootstrap, interrupt stacks, and DMA buffers.
2. **Virtual Memory Management (VMM)**:
   - 4-level paging on 64-bit architectures (PML4, PDPT, PD, PT) with NX (No-Execute) and CR0.WP supervisor write protection.
   - Demand paging and Copy-On-Write (COW) support for `fork()` address spaces.
   - Anonymous and file-backed memory mappings via `mmap` and `mprotect`.
