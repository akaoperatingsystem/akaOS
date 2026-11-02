# XSCore Memory Architecture & Protection

## Physical pages

The physical memory manager begins with every page reserved, then releases only
ranges that UEFI classifies as conventional memory. The first MiB, loader data,
firmware regions, XSCore ELF segments, framebuffer, and MMIO remain unavailable
to callers. A static bitmap currently addresses up to 64 GiB of physical space
(2 MiB of `.bss`), which is why the kernel image spans the 2 MiB boundary at
`0x200000`.

## Virtual memory

XSCore allocates its own PML4, PDPT, page directories, and page tables from the
physical page allocator. The initial address space identity maps enough
physical space to retain the running kernel, boot stack, loader handoff,
allocator bitmap, page tables, and framebuffer. Page zero is absent. Loading
the new CR3 is part of the automated boot path.

EFER.NXE is enabled during VMM initialization so NX page-table bits are legal
for every later mapping (heap, stacks, MMIO).

The mapping API accepts aligned physical and virtual addresses, rejects address
zero and non-canonical virtual addresses, refuses to overwrite present entries,
and invalidates the affected TLB entry after mapping changes. Two helpers
support the Phase 3 hardening work: `xscore_vmm_split_large_page` converts a
2 MiB large page into 512 4 KiB pages preserving flags, and
`xscore_vmm_is_mapped` reports whether a 4 KiB page is present.

### Kernel section permissions

The linker script page-aligns every section boundary, so each section starts on
a 4 KiB page. Before permissions are applied, every 2 MiB large page that
intersects the kernel image is split (per-page flags would otherwise be
impossible). Final state:

- `.text` → RX
- `.rodata` → R + NX
- `.data` → RW + NX
- `.bss` → RW + NX

What remains transitional, and is documented rather than hidden: physical
memory outside the kernel image (loader data, framebuffer, MMIO, and all
remaining RAM) is mapped with coarse 2 MiB entries that are writable and
executable. This is the same transitional policy that lets the kernel leave
firmware page tables safely; converting the whole low identity map to per-page
permissions (and trimming execute rights) is future work, so full-system W^X
is not claimed yet.

### Kernel stacks

The stack region starts at `0xffff810000000000`. For each stack the guard page
(below) stays unmapped, stack pages are mapped read/write + NX, and RSP starts
at the top. Current layout: boot stack 16 KiB, IST1 8 KiB, IST2 8 KiB. The
TSS's IST1/IST2 entries are installed. Stacks are single-CPU until SMP arrives;
per-CPU arrays are the planned structure.

## Kernel heap

The heap is mapped in the high half starting at `0xffff800000000000`, grows in
at least 16-page units, and uses first-fit allocation with splitting and
coalescing. Blocks are 16-byte aligned; heap pages are never executable. Heap
pages stay mapped after blocks are freed; returning unused heap pages to the
physical allocator is future work.

This allocator is single-core only at this milestone. Locks must be added
before SMP scheduling can permit concurrent heap access.

## User heap demand paging and COW

Each process reserves anonymous heap virtual space with `brk()` (up to 1 MiB)
without immediately allocating frames. A recoverable non-present user fault in
the reserved range allocates and zero-fills one 4 KiB page, maps it writable
and non-executable, and records it for process cleanup. x86, ARM, and RISC-V
trap handlers normalize write/present information before calling the shared
process fault policy.

`fork()` shares tracked image, stack, and committed heap frames as read-only
pages with physical reference counts. A write fault either restores write
access for a sole owner or allocates and copies a private frame when the page
is shared. The reference table uses complete physical page numbers, including
the `0x40000000` RAM base used by native QEMU ARM/RISC-V ports. Each port
captures its complete user syscall frame before cloning; the child restores
that frame on its own kernel stack and returns from `fork()` with zero while
the parent receives the child PID. ELF PT_LOAD pages are file-backed
reservations: the first instruction or data fault allocates a frame, copies the
page's file intersections, zero-fills BSS, and maps recorded permissions. Fork
preserves unloaded reservations and COW for loaded pages; cleanup skips
reservations never faulted in.

## Safe userspace copying

Syscalls validate and prepare every page before moving data across the
user/kernel boundary. `xscore_copy_from_user` and `xscore_copy_to_user` reject
unmapped, privileged, or incorrectly protected ranges with `EFAULT`. A valid
reserved heap page is committed on demand, and copy-out to a COW mapping first
creates or restores a writable private page. Read, write, and directory I/O use
kernel-heap bounce buffers so VFS, TTY, and pipe implementations never receive
raw user pointers.

`hello --usercopy-test` verifies invalid pointers do not consume file input, a
single transfer can span two untouched heap pages, and a child can receive file
data into a COW page without modifying the parent. The test passes on x86-64,
x86-32, AArch64, ARM32, RISC-V64, and RISC-V32.

## Private and shared memory mappings

The kernel implements syscall 39 (`mmap`) and syscall 40 (`munmap`) on all six
ports. Anonymous private mappings reserve virtual pages and allocate
zero-filled physical pages on first access. Private file mappings copy the
requested range into a kernel-owned snapshot, then fault in pages on demand;
closing the source descriptor does not invalidate the mapping. `fork()` shares
resident writable mapping pages read-only and resolves writes with Copy-on-
Write. `munmap()` releases resident frames and snapshot references, including
when a process exits.

`MAP_SHARED` is available for anonymous memory and named objects created by
`shm_open()`. `ftruncate()` sizes a new object, and independent processes that
map it observe the same physical pages. `shm_unlink()` removes the name while
open descriptors and mappings remain valid. `mprotect()` changes permissions
for mmap ranges, including temporary `PROT_NONE` access removal.

The current namespace supports 16 objects with names up to 31 characters and
sizes up to 256 KiB. Objects may be resized only while unmapped. Regular VFS
files do not yet provide write-through `MAP_SHARED` semantics.
