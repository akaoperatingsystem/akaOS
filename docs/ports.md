# akaOS architecture ports

akaOS keeps the XSCore kernel and userspace independent of Linux. The current
complete boot paths include x86_64 and x86_32 UEFI FAT32 images plus
dual-firmware ISOs.
The shared SDK now identifies the
requested CPU family and the Ubuntu WSL toolchain can compile and link a
freestanding entry-point smoke image for all of these targets:

| Target | Build status | Boot status |
| --- | --- | --- |
| x86_64 | SDK, kernel, userspace, UEFI loader build and QEMU/OVMF tested | Full image boot-tested |
| x86_32 | Native kernel, userspace, UEFI FAT32 disk, dual UEFI/BIOS Multiboot ISO, and BIOS ATA/IDE persistence fallback | QEMU OVMF and i386 BIOS boot-tested |
| aarch64 | Native kernel, userspace, MMU, exceptions, and direct QEMU build/test | QEMU `virt` AArch64 boot-tested |
| ARM 32-bit | Native kernel, userspace, MMU, exceptions, and direct QEMU build/test | QEMU `virt` ARM32 boot-tested |
| RISC-V 64-bit | Native kernel, userspace, and direct QEMU build/test | QEMU `virt` RISC-V64 boot-tested |
| RISC-V 32-bit | Native kernel, userspace, and direct QEMU build/test | QEMU `virt` RISC-V32 boot-tested |

## x86_64 firmware paths

The x86_64 kernel now has two separate firmware handoffs:

- UEFI: `build/akaos.img`, loaded by `EFI/BOOT/BOOTX64.EFI` and tested with
  OVMF.
- Optical ISO: `build/akaos.iso`, with a native UEFI El Torito entry and GRUB's
  Multiboot2 BIOS stage.
  The BIOS bridge starts in 32-bit protected mode, builds a temporary 4 GiB
  identity map, enters long mode, converts Multiboot2 framebuffer/E820 data to
  the shared `akaos_boot_info`, and then calls the same XSCore entry path.

The FAT32 disk and ISO are separate outputs. The ISO itself boots through UEFI
or BIOS and does not overwrite the UEFI FAT image.

Run the matrix explicitly in Ubuntu WSL:

```sh
wsl.exe -d Ubuntu -- bash -lc "cd /path/to/akaOS && ./tools/build-arch-smoke.sh --all"
```

Individual targets can also be selected through the main build entry point:

```sh
./build.sh --arch aarch64
./build.sh --arch riscv64
```

Build and test the BIOS path with Ubuntu WSL packages `grub-pc-bin`,
`grub-common`, `grub2-common`, and `xorriso`:

```sh
./build.sh --arch x86_64 --firmware bios --disk-type iso
./tools/test-bios.sh
```

The main build entry point keeps firmware and disk format explicit.
`--firmware uefi --disk-type fat32` creates `build/akaos.img`. Any accepted ISO
selection creates the same dual-firmware `build/akaos.iso`, including UEFI,
BIOS, and `both`. `--firmware both --disk-type fat32` creates both outputs from
one common build. Only `fat32` and `iso` are accepted; the old `--uefi` and
`--bios` aliases are not accepted.

For x86_64, `run.sh` accepts `--firmware uefi|bios` and
`--disk-type fat32|iso`. It does not accept firmware `both`, because one QEMU
instance selects one firmware. With `--disk-type iso`, that choice boots the
same `akaos.iso` through OVMF or SeaBIOS. A UEFI/FAT32 selection boots
`build/akaos.img`; BIOS/FAT32 selects the BIOS-capable ISO fallback.
For BIOS runs, `run.sh` attaches the persistent sidecar through the primary
IDE/ATA PIO backend; UEFI runs use the transitional VirtIO PCI backend.

When attached to an interactive terminal, running `./build.sh` or `./run.sh`
without options opens the shared raspi-config-style terminal UI. The build menu
selects architecture, firmware, disk type, test mode, and verbose logging; the
run menu selects architecture, firmware, disk type, and display mode. Explicit
flags remain the stable interface for scripts and automated tests.

For x86_32, the architecture-aware build default is a dual-firmware ISO, while
the run default selects BIOS. One `build/akaos32.iso` boots through either
firmware without touching the x86_64 `build/akaos.iso`:

```sh
./build.sh --arch x86_32
./build.sh --arch x86_32 --firmware uefi --disk-type fat32
./build.sh --arch x86_32 --firmware both --disk-type iso
./run.sh --arch x86_32 --firmware uefi --disk-type iso
./run.sh --arch x86_32 --firmware uefi --disk-type fat32
./build.sh --arch x86_32 --firmware bios --disk-type iso
./run.sh --arch x86_32
./run.sh --arch x86_32 --firmware bios --disk-type iso
```

The FAT32 image and ISO contain `BOOTX64.EFI` and `BOOTIA32.EFI` when the GRUB
EFI modules are installed. GRUB performs the
Multiboot2 handoff to the native 32-bit kernel, so ordinary 64-bit UEFI PCs and
IA32 UEFI implementations have matching removable-media entries. On Ubuntu,
install `grub-efi-amd64-bin` and `grub-efi-ia32-bin` to build these entries.
`tools/test-x86_32-iso.sh` verifies the same ISO through OVMF and SeaBIOS.
`tools/test-x86_32-persistence.sh` verifies `echo > file`, automatic shell
logout commit, and a second BIOS boot through the ATA/IDE-backed akaFS sidecar.

ARM32 and AArch64 are native direct-kernel QEMU targets. They accept the common
firmware and disk selectors for CLI parity; QEMU still loads their ELF kernels
directly:

```sh
./build.sh --arch arm32
./run.sh --arch arm32 --headless
./tools/test-arm32.sh
```

Both direct paths use the PL011 serial console (`-serial stdio`) and disable
QEMU's separate monitor. The optional `--gui` flag opens a RAMFB framebuffer
window with the same bootsplash and fullscreen TTY console; VirtIO keyboard
events are mirrored into the TTY while serial input remains available.

RISC-V `--arch` targets use the same direct-kernel QEMU model as ARM. Their
common firmware and disk selectors are accepted for CLI parity while the
native OpenSBI/QEMU `virt` loader boots the architecture ELF directly.
