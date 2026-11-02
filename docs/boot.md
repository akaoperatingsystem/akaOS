# UEFI & BIOS Boot Sequence

```text
OVMF UEFI
  -> EFI/BOOT/BOOTX64.EFI
  -> open and validate XSCORE.ELF
  -> allocate and copy ELF PT_LOAD segments
  -> capture GOP framebuffer information
  -> capture UEFI memory map
  -> ExitBootServices
  -> xscore_entry(akaos_boot_info)
  -> install XSCore GDT and exception IDT
  -> initialize and verify physical page allocator
  -> create XSCore page tables, switch CR3, enable NX + WP
  -> initialize and verify kernel heap
  -> apply kernel section permissions (split 2 MiB pages first)
  -> allocate boot/IST stacks, enter the boot chain on the kernel stack
  -> install TSS + IST, mask 8259 PIC, map + enable Local APIC
  -> calibrate APIC timer against PIT, start 100 Hz periodic ticks
  -> verify scheduler tick counter, complete splash progress, halt
```

The loader writes early failures and milestones to COM1 at 115200 baud. XSCore
continues using that port after firmware services are gone.

The automated test places an `AKAOS.TEST` marker in a copy of the disk image.
The loader converts that marker to a handoff flag. After all kernel checks and
rendering finish, XSCore signals QEMU's debug-exit device. Normal boot images
do not contain the marker and remain halted safely on the diagnostics screen.

## Two-phase screen transition

The UEFI loader gathers graphics metadata but does not render a graphical boot
screen or introduce an artificial delay. Serial diagnostics remain active for
the entire boot.

Stage 1 is the early kernel console. Quiet mode is the default, so this stage
keeps the framebuffer black and writes diagnostics only to serial. If the boot
volume contains `VERBOSE` or `AKAOS.VERBOSE`, XSCore renders the real `[ OK ]`,
`[ .. ]`, hardware, and memory messages through its early framebuffer console.

Stage 2 begins only after paging and the kernel heap are verified.
`splash_init()` clears the complete framebuffer to `0x000000` before setting the
splash-active state. It then renders only a centered group containing the akaOS
wordmark, `Starting XSCore` subtitle, and 8-dot progress indicator. Logging
functions never render or animate the splash; later subsystem code advances the
spinner through explicit splash-progress calls while logs continue on serial.

Text uses an anti-aliased DejaVu Sans atlas generated at build time by
`tools/generate_font.py`. `tools/capture.sh` waits for the real kernel-completion
serial milestone and saves the final splash to `build/boot-screen.png`.
