# Building and Running akaOS

**akaOS** is powered by the original **XSCore** kernel. Linux is used exclusively as a development cross-compilation host; it is not used at runtime.

---

## 1. Quick Start Guide

From the akaOS repository root directory:

```sh
# 1. Verify that your host system has all required tools
./build.sh --check-deps

# 2. Build the operating system (opens the interactive configuration menu)
./build.sh

# 3. Boot akaOS inside QEMU
./run.sh
```

- The first command inspects your host toolchain and prints the exact package installation command for your distribution if any dependencies are missing.
- When run without arguments in an interactive terminal, both `build.sh` and `run.sh` open full-screen configuration menus where you can select target architectures, firmware, desktop environments, software bundles, and hardware parameters.
- **TUI Navigation:** Use Arrow keys to highlight items, Tab to cycle between buttons, Enter to select, and Escape to cancel. If `whiptail` is not installed, a numbered text-mode menu is automatically presented instead.

### Default Login Accounts

The generated image contains two pre-configured accounts:
- **Root Administrator:** username `root`, password `root` (shell prompt: `#`)
- **Standard User:** username `user`, password `akaos` (shell prompt: `$`)

---

## 2. Dependency Checking & Package Installation

You can check dependencies for any specific architecture target without installing anything:

```sh
./build.sh --arch x86_64 --firmware uefi --check-deps
./run.sh --arch aarch64 --check-deps
```

The checker validates compilers, linkers, emulators, Python libraries, fonts, and firmware. It never runs `sudo` or makes unapproved host changes.

### Complete Host Package Sets

#### Ubuntu and Debian
```sh
sudo apt update
sudo apt install -y clang lld llvm binutils python3 python3-pil \
  fonts-dejavu-core dosfstools mtools grub-pc-bin grub-common grub2-common \
  grub-efi-amd64-bin grub-efi-ia32-bin xorriso qemu-system-x86 \
  qemu-system-arm qemu-system-misc ovmf whiptail
```

On Windows, use the Ubuntu WSL distribution explicitly:
```powershell
wsl.exe -d Ubuntu
```

#### Arch Linux
```sh
sudo pacman -S --needed clang lld llvm binutils python python-pillow \
  ttf-dejavu dosfstools mtools grub xorriso qemu-full edk2-ovmf libnewt
```

#### Fedora
```sh
sudo dnf install -y clang lld llvm binutils python3 python3-pillow \
  dejavu-sans-fonts dosfstools mtools grub2-tools grub2-tools-extra \
  grub2-pc-modules grub2-efi-x64-modules grub2-efi-ia32-modules xorriso \
  qemu-system-x86 qemu-system-arm qemu-system-aarch64 qemu-system-riscv \
  edk2-ovmf newt
```

#### openSUSE
```sh
sudo zypper install clang lld llvm binutils python3 python3-Pillow \
  dejavu-fonts dosfstools mtools grub2 grub2-i386-pc grub2-x86_64-efi \
  grub2-i386-efi xorriso qemu-x86 qemu-arm qemu-extra \
  qemu-ovmf-x86_64 newt
```

#### Alpine Linux
```sh
sudo apk add clang lld llvm binutils python3 py3-pillow font-dejavu \
  dosfstools mtools grub xorriso qemu-system-x86_64 qemu-system-i386 \
  qemu-system-aarch64 qemu-system-arm qemu-system-riscv64 \
  qemu-system-riscv32 ovmf newt
```

---

## 3. Non-Interactive CLI Automation

Both scripts accept direct command-line arguments for automated scripting and CI/CD pipelines:

```sh
# Build x86-64 with XFCE desktop autostarting
./build.sh --arch x86_64 --firmware uefi --disk-type fat32 --desktop xfce --session-mode desktop
./run.sh --arch x86_64 --firmware uefi --gui

# Build x86-64 pure console headless server (no GUI installed)
./build.sh --arch x86_64 --firmware bios --disk-type iso --desktop none --session-mode console
./run.sh --arch x86_64 --firmware bios --headless

# Build and run ARM 64-bit (AArch64)
./build.sh --arch aarch64
./run.sh --arch aarch64 --gui

# Build and run RISC-V 64-bit
./build.sh --arch riscv64
./run.sh --arch riscv64 --headless
```

---

## 4. Persistent Storage (akaFS)

Persistent user files and configuration changes are preserved on a dedicated secondary disk image (`build/akaos-data.img`).
- This disk is automatically created on first boot and attached across QEMU sessions.
- To reset user data and start with a fresh environment, delete `build/akaos-data.img`.
- To specify an alternative storage disk, set `AKAOS_STORAGE_IMAGE=/path/to/disk.img ./run.sh`.
