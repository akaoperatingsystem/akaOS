# Building and Running akaOS

akaOS uses its own XSCore kernel. Linux is only the development host used to
compile the project and run QEMU.

## New-user quick start

From the akaOS repository directory:

```sh
./build.sh --check-deps
./build.sh
./run.sh
```

The first command checks the default target and prints a distribution-specific
package command if anything is missing. The next two commands open full-screen
terminal menus. Select the architecture, firmware, and disk type, confirm the
build, then select the same values in the run menu.

Use the arrow keys to move, Tab to switch buttons, Enter to select, and Escape
to cancel. If `whiptail` is not installed, akaOS automatically presents a
numbered text menu instead.

The development image login is `root` with initial password `root`, or `user`
with initial password `akaos`.

## Checking a specific target

The dependency checker understands every supported architecture:

```sh
./build.sh --arch x86_32 --firmware both --disk-type fat32 --check-deps
./run.sh --arch aarch64 --firmware uefi --disk-type fat32 --check-deps
```

It reports missing commands, Python Pillow, fonts, GRUB platform modules, QEMU,
and OVMF firmware. It never runs `sudo` or changes the host automatically.

## Complete package sets

The checker normally suggests only the package groups relevant to the selected
target. These commands install the broad toolset for building and running all
six QEMU ports. Package names can change between distribution releases; use
the command printed by `--check-deps` as the current repository guidance.

### Ubuntu and Debian

```sh
sudo apt update
sudo apt install -y clang lld llvm binutils python3 python3-pil \
  fonts-dejavu-core dosfstools mtools grub-pc-bin grub-common grub2-common \
  grub-efi-amd64-bin grub-efi-ia32-bin xorriso qemu-system-x86 \
  qemu-system-arm qemu-system-misc ovmf whiptail
```

On this project's Windows development machine, use the Ubuntu WSL distribution
explicitly:

```powershell
wsl.exe -d Ubuntu
```

### Arch Linux

```sh
sudo pacman -S --needed clang lld llvm binutils python python-pillow \
  ttf-dejavu dosfstools mtools grub xorriso qemu-full edk2-ovmf libnewt
```

### Fedora

```sh
sudo dnf install -y clang lld llvm binutils python3 python3-pillow \
  dejavu-sans-fonts dosfstools mtools grub2-tools grub2-tools-extra \
  grub2-pc-modules grub2-efi-x64-modules grub2-efi-ia32-modules xorriso \
  qemu-system-x86 qemu-system-arm qemu-system-aarch64 qemu-system-riscv \
  edk2-ovmf newt
```

### openSUSE

```sh
sudo zypper install clang lld llvm binutils python3 python3-Pillow \
  dejavu-fonts dosfstools mtools grub2 grub2-i386-pc grub2-x86_64-efi \
  grub2-i386-efi xorriso qemu-x86 qemu-arm qemu-extra \
  qemu-ovmf-x86_64 newt
```

### Alpine Linux

```sh
sudo apk add clang lld llvm binutils python3 py3-pillow font-dejavu \
  dosfstools mtools grub xorriso qemu-system-x86_64 qemu-system-i386 \
  qemu-system-aarch64 qemu-system-arm qemu-system-riscv64 \
  qemu-system-riscv32 ovmf newt
```

Ubuntu WSL is the repository's verified host. The other package commands and
resource paths are supported by the dependency assistant but should be treated
as distribution guidance until their full QEMU matrices are continuously
tested.

## Non-interactive commands

Menus are optional. Scripts and CI can continue using explicit flags:

```sh
./build.sh --arch x86_64 --firmware uefi --disk-type fat32
./run.sh --arch x86_64 --firmware uefi --disk-type fat32 --gui

./build.sh --arch x86_32 --firmware both --disk-type iso
./run.sh --arch x86_32 --firmware bios --disk-type iso --headless

./build.sh --arch riscv64 --firmware uefi --disk-type fat32
./run.sh --arch riscv64 --firmware uefi --disk-type fat32 --headless
```

The persistent shell filesystem is stored separately in
`build/akaos-data.img`. Do not delete that file if you want created files to
survive later QEMU sessions.
