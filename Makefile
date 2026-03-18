# ============================================================
# akaOS Makefile — Multi-Architecture (x86_64, x86_32, aarch64)
# ============================================================
# Usage:
#   make                    — Build for x86_64 (default)
#   make ARCH=x86_64        — Build for x86_64
#   make ARCH=x86_32        — Build for x86_32
#   make ARCH=aarch64       — Build for aarch64
#   make iso                — Build bootable ISO (x86_64 default)
#   make run                — Build + run in QEMU
#   make clean              — Clean build artifacts

ARCH ?= x86_64

# ============================================================
# Per-architecture toolchain and flags
# ============================================================

ifeq ($(ARCH),x86_64)
  CC      = gcc
  AS      = nasm
  LD      = ld
  CFLAGS  = -m64 -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
            -Iarch/x86_64 -Ikernel -Ikernel/libc -Iroot/games/doomgeneric \
            -DARCH_X86_64 \
            -fno-pie -fno-pic -mno-red-zone -fno-stack-protector \
            -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel \
            -Wno-unused-parameter
  DOOM_CFLAGS = -m64 -std=gnu99 -ffreestanding -O2 \
                -Iarch/x86_64 -Ikernel -Ikernel/libc -Iroot/games/doomgeneric \
                -DARCH_X86_64 \
                -fno-pie -fno-pic -mno-red-zone -fno-stack-protector \
                -mcmodel=kernel -Wno-unused-parameter
  ASFLAGS  = -f elf64
  LDFLAGS  = -m elf_x86_64 -T arch/x86_64/linker.ld -nostdlib -z noexecstack
  QEMU     = qemu-system-x86_64
  INCLUDE_DOOM = yes

else ifeq ($(ARCH),x86_32)
  CC      = gcc
  AS      = nasm
  LD      = ld
  CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
            -Iarch/x86_32 -Ikernel -Ikernel/libc -Iroot/games/doomgeneric \
            -DARCH_X86_32 \
            -fno-pie -fno-pic -fno-stack-protector \
            -Wno-unused-parameter
  DOOM_CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 \
                -Iarch/x86_32 -Ikernel -Ikernel/libc -Iroot/games/doomgeneric \
                -DARCH_X86_32 \
                -fno-pie -fno-pic -fno-stack-protector \
                -Wno-unused-parameter
  ASFLAGS  = -f elf32
  LDFLAGS  = -m elf_i386 -T arch/x86_32/linker.ld -nostdlib -z noexecstack
  LIBGCC   = $(shell gcc -m32 -print-libgcc-file-name)
  QEMU     = qemu-system-i386
  INCLUDE_DOOM = yes

else ifeq ($(ARCH),aarch64)
  CC      = aarch64-linux-gnu-gcc
  AS      = aarch64-linux-gnu-as
  LD      = aarch64-linux-gnu-ld
  CFLAGS  = -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
            -Iarch/aarch64 -Ikernel -Ikernel/libc -Iroot/games/doomgeneric \
            -DARCH_AARCH64 \
            -fno-pie -fno-pic -fno-stack-protector \
            -Wno-unused-parameter
  ASFLAGS  =
  LDFLAGS  = -T arch/aarch64/linker.ld -nostdlib
  QEMU     = qemu-system-aarch64
  INCLUDE_DOOM = no

else
  $(error Unknown ARCH=$(ARCH). Supported: x86_64, x86_32, aarch64)
endif

# ============================================================
# Architecture-specific source files
# ============================================================

ifeq ($(ARCH),x86_64)
  ARCH_ASM_SRCS = arch/x86_64/boot.asm arch/x86_64/idt_asm.asm
  ARCH_C_SRCS   = arch/x86_64/idt.c
else ifeq ($(ARCH),x86_32)
  ARCH_ASM_SRCS = arch/x86_32/boot.asm arch/x86_32/idt_asm.asm
  ARCH_C_SRCS   = arch/x86_32/idt.c
else ifeq ($(ARCH),aarch64)
  ARCH_ASM_SRCS = arch/aarch64/boot.S arch/aarch64/interrupts_asm.S
  ARCH_C_SRCS   = arch/aarch64/interrupts.c
endif

# Common kernel C sources (shared across all architectures)
C_SOURCES   = kernel/kernel.c kernel/vga.c kernel/string.c \
              kernel/keyboard.c kernel/shell.c \
              kernel/fs.c kernel/time.c kernel/fb.c \
              kernel/mouse.c kernel/gui.c kernel/net.c kernel/sysmon.c \
              kernel/doomgeneric_akaos.c kernel/libc/all_libc.c

# DOOM Generic Engine (x86 only)
DOOM_DIR    = root/games/doomgeneric
DOOM_OBJS   = dummy.o am_map.o doomdef.o doomstat.o dstrings.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o d_net.o f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o info.o i_cdmus.o i_endoom.o i_joystick.o i_scale.o i_sound.o i_system.o i_timer.o memio.o m_argv.o m_bbox.o m_cheat.o m_config.o m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o r_segs.o r_sky.o r_things.o sha1.o sounds.o statdump.o st_lib.o st_stuff.o s_sound.o tables.o v_video.o wi_stuff.o w_checksum.o w_file.o w_main.o w_wad.o z_zone.o w_file_stdc.o i_input.o i_video.o doomgeneric.o
DOOM_OBJECTS = $(addprefix $(DOOM_DIR)/, $(DOOM_OBJS))

# ============================================================
# Object file lists
# ============================================================

# Architecture assembly objects
ifeq ($(ARCH),aarch64)
  ARCH_ASM_OBJECTS = $(ARCH_ASM_SRCS:.S=.o)
else
  ARCH_ASM_OBJECTS = $(ARCH_ASM_SRCS:.asm=.o)
endif

ARCH_C_OBJECTS = $(ARCH_C_SRCS:.c=.o)
C_OBJECTS      = $(C_SOURCES:.c=.o)

ifeq ($(INCLUDE_DOOM),yes)
  ALL_OBJECTS = $(ARCH_ASM_OBJECTS) $(ARCH_C_OBJECTS) $(C_OBJECTS) $(DOOM_OBJECTS)
else
  ALL_OBJECTS = $(ARCH_ASM_OBJECTS) $(ARCH_C_OBJECTS) $(C_OBJECTS)
endif

# Output
KERNEL_BIN = build/boot/akaos.bin
ISO        = akaOS.iso

.PHONY: all clean run run-uefi iso

# Limine bootloader version
LIMINE_BRANCH = v10.x-binary
LIMINE_VER = 10.x

all: $(KERNEL_BIN)

$(KERNEL_BIN): $(ALL_OBJECTS)
	@mkdir -p build/boot
	$(LD) $(LDFLAGS) $(ALL_OBJECTS) $(LIBGCC) -o $(KERNEL_BIN)

# ============================================================
# Compilation rules
# ============================================================

# Kernel C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# DOOM C files (with SSE enabled, x86 only)
ifeq ($(INCLUDE_DOOM),yes)
$(DOOM_DIR)/%.o: $(DOOM_DIR)/%.c
	$(CC) $(DOOM_CFLAGS) -w -c $< -o $@
endif

# x86 assembly (NASM)
ifneq ($(ARCH),aarch64)
arch/$(ARCH)/boot.o: arch/$(ARCH)/boot.asm
	nasm $(ASFLAGS) $< -o $@

arch/$(ARCH)/idt_asm.o: arch/$(ARCH)/idt_asm.asm
	nasm $(ASFLAGS) $< -o $@
endif

# ARM64 assembly (GAS)
ifeq ($(ARCH),aarch64)
arch/aarch64/boot.o: arch/aarch64/boot.S
	$(CC) $(CFLAGS) -c $< -o $@

arch/aarch64/interrupts_asm.o: arch/aarch64/interrupts_asm.S
	$(CC) $(CFLAGS) -c $< -o $@
endif

# ============================================================
# Limine bootloader
# ============================================================
limine:
	@if [ ! -d "limine" ]; then \
		git clone https://github.com/limine-bootloader/limine.git --branch $(LIMINE_BRANCH) --depth 1; \
		make -C limine; \
	fi

# ============================================================
# DOOM WAD download
# ============================================================
build/boot/doom1.wad:
	@echo "Downloading DOOM Shareware WAD..."
	@mkdir -p build/boot
	@wget -qO build/boot/doom1.wad https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad || curl -Lso build/boot/doom1.wad https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad

# ============================================================
# ISO creation (x86_64 with Limine, x86_32 with GRUB)
# ============================================================
ifeq ($(ARCH),x86_64)
iso: $(KERNEL_BIN) limine build/boot/doom1.wad
	@mkdir -p build/boot/limine build/limine build/EFI/BOOT
	@cp limine.conf build/ && cp limine.conf build/limine/
	@cp limine.conf build/boot/ && cp limine.conf build/boot/limine/
	@cp limine/limine-bios.sys build/ && cp limine/limine-bios.sys build/limine/
	@cp limine/limine-bios.sys build/boot/ && cp limine/limine-bios.sys build/boot/limine/
	@cp limine/limine-bios-cd.bin limine/limine-uefi-cd.bin build/
	@cp limine/BOOTX64.EFI build/EFI/BOOT/
	xorriso -as mkisofs -V "akaOS" -R -J -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build -o $(ISO)
	./limine/limine bios-install $(ISO)

else ifeq ($(ARCH),x86_32)
iso: $(KERNEL_BIN) build/boot/doom1.wad
	@mkdir -p build/boot/grub
	@echo 'set timeout=3'                          > build/boot/grub/grub.cfg
	@echo 'menuentry "akaOS (i386)" {'             >> build/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/akaos.bin'         >> build/boot/grub/grub.cfg
	@echo '    module2 /boot/doom1.wad'            >> build/boot/grub/grub.cfg
	@echo '    boot'                               >> build/boot/grub/grub.cfg
	@echo '}'                                      >> build/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) build

else ifeq ($(ARCH),aarch64)
iso: $(KERNEL_BIN) limine
	@mkdir -p build/boot/limine build/limine build/EFI/BOOT
	@echo "timeout: 5" > build/limine.conf
	@echo "verbose: yes" >> build/limine.conf
	@echo "" >> build/limine.conf
	@echo "/akaOS" >> build/limine.conf
	@echo "    protocol: limine" >> build/limine.conf
	@echo "    kernel_path: boot():/boot/akaos.bin" >> build/limine.conf
	@cp build/limine.conf build/limine/
	@cp build/limine.conf build/boot/ && cp build/limine.conf build/boot/limine/
	@cp limine/BOOTAA64.EFI build/EFI/BOOT/
	@cp limine/limine-uefi-cd.bin build/
	xorriso -as mkisofs -V "akaOS" -R -J \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build -o $(ISO)
endif

# ============================================================
# Run targets
# ============================================================
ifeq ($(ARCH),x86_64)
run: iso
	$(QEMU) -cdrom $(ISO) \
		-m 128M \
		-device virtio-mouse-pci \
		-nic user,model=e1000

OVMF := $(shell test -f /usr/share/ovmf/OVMF.fd && echo /usr/share/ovmf/OVMF.fd || \
               (test -f /usr/share/OVMF/OVMF_CODE.fd && echo /usr/share/OVMF/OVMF_CODE.fd || \
                echo /usr/share/qemu/OVMF.fd))

run-uefi: iso
	$(QEMU) -cdrom $(ISO) \
		-m 128M \
		-bios $(OVMF) \
		-device virtio-mouse-pci \
		-nic user,model=e1000

else ifeq ($(ARCH),x86_32)
run: iso
	$(QEMU) -cdrom $(ISO) \
		-m 128M \
		-device virtio-mouse-pci \
		-nic user,model=e1000

else ifeq ($(ARCH),aarch64)
run: iso
	$(QEMU) -M virt -cpu cortex-a72 \
		-cdrom $(ISO) \
		-m 128M \
		-bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
		-device virtio-gpu-pci \
		-device qemu-xhci -device usb-kbd -device usb-mouse
endif

# ============================================================
# Clean
# ============================================================
clean:
	rm -f $(C_OBJECTS) $(ARCH_ASM_OBJECTS) $(ARCH_C_OBJECTS)
	rm -f $(DOOM_OBJECTS)
	rm -f $(KERNEL_BIN)
	rm -f $(ISO)
	rm -rf build
	rm -rf limine
