# akaOS Architecture Portability Matrix

akaOS keeps the **XSCore** kernel, native C library (libc), and userspace runtime independent of Linux. The system provides tested, bootable native implementations across six CPU architectures:

| Target Architecture | Triplet | Kernel Binary | Boot Firmware | Storage Device | Test Environment |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **x86-64 (AMD64)** | `x86_64-unknown-none-elf` | `XSCORE.ELF` | UEFI / SeaBIOS | VirtIO-Block / IDE | QEMU `q35` / OVMF |
| **x86-32 (i386)** | `i386-unknown-none-elf` | `XSCORE32.ELF` | UEFI / SeaBIOS | VirtIO / ATA-PIO | QEMU `pc` / SeaBIOS |
| **AArch64 (ARM 64-bit)** | `aarch64-unknown-none-elf` | `XSCORE_AARCH64.ELF`| Direct Kernel | VirtIO-MMIO | QEMU `virt` |
| **ARM32 (ARMv7-A)** | `armv7-unknown-none-eabi` | `XSCORE_ARM32.ELF` | Direct Kernel | VirtIO-MMIO | QEMU `virt` |
| **RISC-V 64-bit** | `riscv64-unknown-none-elf` | `XSCORE_RISCV64.ELF`| Direct Kernel | VirtIO-MMIO | QEMU `virt` |
| **RISC-V 32-bit** | `riscv32-unknown-none-elf` | `XSCORE_RISCV32.ELF`| Direct Kernel | VirtIO-MMIO | QEMU `virt` |

---

## 1. x86-64 Firmware Pathways

The x86-64 port provides two distinct firmware hand-off mechanisms:

1. **UEFI Boot Image (`build/akaos.img`):**
   - Built as a standard FAT32 EFI System Partition (ESP) containing `EFI/BOOT/BOOTX64.EFI`.
   - Boot-tested using OVMF firmware.
   - Discovers hardware via ACPI and Local APIC, captures GOP framebuffer modes, and executes XSCore with 4-level paging.
2. **Dual-Firmware Optical ISO (`build/akaos.iso`):**
   - Features both a native UEFI El Torito boot entry and a legacy BIOS GRUB Multiboot2 boot stage.
   - When booting under BIOS, the 32-bit protected mode bridge establishes a 4 GiB identity page table, activates 64-bit long mode, converts Multiboot2 E820 and VBE framebuffer structures to `akaos_boot_info`, and calls the unified XSCore kernel entry point.

---

## 2. x86-32 Native Port

The 32-bit x86 port produces a dual-firmware ISO (`build/akaos32.iso`) and a native FAT32 disk (`build/akaos32.img`):
- **BIOS Compatibility:** Employs SeaBIOS with primary IDE channel fallback for reliable ATA PIO sector reads/writes.
- **UEFI Compatibility:** Compatible with 32-bit and 64-bit UEFI firmware handoffs.

---

## 3. ARM (AArch64 & ARM32) Direct Kernel Ports

Both ARM targets execute as direct-kernel payloads under QEMU's `virt` machine:
- PL011 UART serial console communication mapped to `stdio`.
- RAMFB linear framebuffer window support with native mouse and keyboard input via VirtIO devices.
- VirtIO-MMIO sector-level disk storage for persistent akaFS storage.

---

## 4. RISC-V (RV64GC & RV32GC) Direct Kernel Ports

The RISC-V ports leverage QEMU's native `virt` machine with OpenSBI:
- **Paging:** Sv39 virtual memory on RV64; Sv32 virtual memory on RV32.
- **Hardware Integration:** VirtIO-MMIO block storage, UART serial terminal, and timer interrupts.

---

## 5. Automated Multi-Architecture Smoke Validation

To verify that the complete freestanding toolchain can cross-compile and link valid ELF binaries for all six architectures:

```sh
./tools/build-arch-smoke.sh --all
```
