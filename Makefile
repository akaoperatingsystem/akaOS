# ============================================================
# akaOS Makefile (64-bit, GUI + Networking)
# ============================================================

CC      = gcc
AS      = nasm

# 64-bit freestanding kernel flags
CFLAGS  = -m64 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Ikernel -Ikernel/libc -Iroot/games/doomgeneric \
          -fno-pie -fno-pic -mno-red-zone -fno-stack-protector \
          -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel \
          -Wno-unused-parameter

# DOOM needs SSE for float operations
DOOM_CFLAGS = -m64 -std=gnu99 -ffreestanding -O2 -Ikernel -Ikernel/libc -Iroot/games/doomgeneric/doomgeneric \
              -fno-pie -fno-pic -mno-red-zone -fno-stack-protector \
              -mcmodel=kernel -Wno-unused-parameter

ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib -z noexecstack

# Source files
ASM_SOURCES = kernel/boot.asm kernel/idt_asm.asm
C_SOURCES   = kernel/kernel.c kernel/vga.c kernel/string.c \
              kernel/idt.c kernel/keyboard.c kernel/shell.c \
              kernel/fs.c kernel/time.c kernel/fb.c \
              kernel/mouse.c kernel/gui.c kernel/net.c kernel/sysmon.c \
              kernel/doomgeneric_akaos.c kernel/libc/all_libc.c

# DOOM Generic Engine
DOOM_DIR    = root/games/doomgeneric/doomgeneric
DOOM_OBJS   = dummy.o am_map.o doomdef.o doomstat.o dstrings.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o d_net.o f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o info.o i_cdmus.o i_endoom.o i_joystick.o i_scale.o i_sound.o i_system.o i_timer.o memio.o m_argv.o m_bbox.o m_cheat.o m_config.o m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o r_segs.o r_sky.o r_things.o sha1.o sounds.o statdump.o st_lib.o st_stuff.o s_sound.o tables.o v_video.o wi_stuff.o w_checksum.o w_file.o w_main.o w_wad.o z_zone.o w_file_stdc.o i_input.o i_video.o doomgeneric.o
DOOM_OBJECTS = $(addprefix $(DOOM_DIR)/, $(DOOM_OBJS))

# Object files
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
C_OBJECTS   = $(C_SOURCES:.c=.o)
OBJECTS     = $(ASM_OBJECTS) $(C_OBJECTS) $(DOOM_OBJECTS)

# Output
KERNEL_BIN = build/boot/akaos.bin
ISO        = akaOS.iso

.PHONY: all clean run run-uefi iso
# Limine bootloader version
LIMINE_BRANCH = v10.x-binary
LIMINE_VER = 10.x

all: $(KERNEL_BIN)

$(KERNEL_BIN): kernel/boot.o kernel/idt_asm.o $(C_OBJECTS) $(DOOM_OBJECTS)
	@mkdir -p build/boot
	ld $(LDFLAGS) kernel/boot.o kernel/idt_asm.o $(C_OBJECTS) $(DOOM_OBJECTS) -o $(KERNEL_BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(DOOM_DIR)/%.o: $(DOOM_DIR)/%.c
	$(CC) $(DOOM_CFLAGS) -w -c $< -o $@

kernel/boot.o: kernel/boot.asm
	nasm -f elf64 kernel/boot.asm -o kernel/boot.o

kernel/idt_asm.o: kernel/idt_asm.asm
	nasm -f elf64 kernel/idt_asm.asm -o kernel/idt_asm.o

limine:
	@if [ ! -d "limine" ]; then \
		git clone https://github.com/limine-bootloader/limine.git --branch $(LIMINE_BRANCH) --depth 1; \
		make -C limine; \
	fi

build/boot/doom1.wad:
	@echo "Downloading DOOM Shareware WAD..."
	@mkdir -p build/boot
	@wget -qO build/boot/doom1.wad https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad || curl -Lso build/boot/doom1.wad https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad

iso: $(KERNEL_BIN) limine build/boot/doom1.wad
	@# 1. Prepare ISO folder structure
	@mkdir -p build/boot/limine
	@mkdir -p build/limine
	@mkdir -p build/EFI/BOOT
	@# 2. Hyper-redundant configuration and stage 2 mirroring
	@cp limine.conf build/
	@cp limine.conf build/limine/
	@cp limine.conf build/boot/
	@cp limine.conf build/boot/limine/
	@cp limine/limine-bios.sys build/
	@cp limine/limine-bios.sys build/limine/
	@cp limine/limine-bios.sys build/boot/
	@cp limine/limine-bios.sys build/boot/limine/
	@# 3. Copy boot images and WADs
	@cp limine/limine-bios-cd.bin limine/limine-uefi-cd.bin build/
	@cp limine/BOOTX64.EFI build/EFI/BOOT/
	@# 4. Build the ISO with xorriso (Hyper-compatible)
	xorriso -as mkisofs -V "akaOS" -R -J -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build -o $(ISO)
	@# 5. Install Limine BIOS boot sector
	./limine/limine bios-install $(ISO)

# Run with BIOS (legacy)
run: iso
	qemu-system-x86_64 -cdrom $(ISO) \
		-m 128M \
		-device virtio-mouse-pci \
		-nic user,model=e1000

# Run with UEFI (requires: sudo apt install ovmf)
# Tries common OVMF paths
OVMF := $(shell test -f /usr/share/ovmf/OVMF.fd && echo /usr/share/ovmf/OVMF.fd || \
               (test -f /usr/share/OVMF/OVMF_CODE.fd && echo /usr/share/OVMF/OVMF_CODE.fd || \
                echo /usr/share/qemu/OVMF.fd))

run-uefi: iso
	qemu-system-x86_64 -cdrom $(ISO) \
		-m 128M \
		-bios $(OVMF) \
		-device virtio-mouse-pci \
		-nic user,model=e1000

clean:
	rm -f $(OBJECTS)
	rm -f $(KERNEL_BIN)
	rm -f $(ISO)
	rm -rf build
	rm -rf limine
