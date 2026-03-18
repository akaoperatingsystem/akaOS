# akaOS

**akaOS** is a modern, open-source operating system designed for **x86-64**, **x86-32**, and **aarch64** CPUs. It uses the **Limine Bootloader** for **x86-64** and **aarch64**, and **GRUB** for **x86-32**. Developed with a focus on visual excellence and "vibe coding," akaOS features a custom GUI, a POSIX-like shell, and even runs DOOM.

![akaOS Screenshot](https://raw.githubusercontent.com/akaoperatingsystem/akaOS/refs/heads/main/akaOS.png)

---

## ✨ Features

- **Multi-Architecture Support**: Native support for **x86-64**, **x86-32**, and **aarch64**.
- **Modern Bootloading**:
  - **Limine Bootloader**: Powers **x86-64** and **aarch64** (BIOS & UEFI).
  - **GRUB**: Used for **x86-32** legacy support.
- **Custom GUI**: A sleek graphical desktop environment with window management and micro-animations.
- **POSIX-like Shell**: Functional terminal with standard Unix-like commands.
- **DOOM Support**: Runs `doomgeneric` natively with shareware WAD support.
- **System Monitor**: Real-time CPU, memory, and process management.
- **Networking**: Integrated network stack with `ping` support (e1000).
- **In-Memory Filesystem**: Fast and efficient storage management.

## 🛠 Prerequisites

To build and run **akaOS**, you will need:

- **Compiler**: `gcc` (with cross-compilers for aarch64: `aarch64-linux-gnu-gcc`)
- **Assembler**: `nasm` (for x86) and `aarch64-linux-gnu-as` (for ARM64)
- **Linker**: `ld`
- **ISO Tools**: `xorriso`, `mtools`
- **Emulation**: `qemu-system-x86_64`, `qemu-system-i386`, or `qemu-system-aarch64`
- **Utilities**: `git`, `wget` or `curl`, `make`

## 🚀 Building and Running

akaOS uses a versatile `Makefile` to manage builds across architectures.

### x86-64 (BIOS & UEFI)
```bash
make            # Build the kernel
make iso        # Build the bootable ISO
make run        # Build and run akaOS (BIOS)
make run-uefi   # Build and run akaOS (UEFI)
```

### Other Architectures
You can specify the target architecture using the `ARCH` variable:

| Architecture | Build Command | ISO Command | Run Command |
| :--- | :--- | :--- | :--- |
| **x86-64** | `make` | `make iso` | `make run` |
| **x86-32** | `make ARCH=x86_32` | `make ARCH=x86_32 iso` | `make ARCH=x86_32 run` |
| **ARM64** | `make ARCH=aarch64` | `make ARCH=aarch64 iso` | `make ARCH=aarch64 run` |

### Cleanup
To remove all build artifacts and the Limine repository:
```bash
make clean
```

---

## 📜 License
This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.
