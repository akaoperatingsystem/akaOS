# Developer Workflow & Testing Guide

This guide describes how to build, test, and contribute to **akaOS** across all supported architectures.

---

## 1. Development Host Setup

akaOS builds with standard LLVM/Clang toolchains. Linux is used strictly as a development and cross-compilation host.

### Recommended Environment
- **WSL (Ubuntu)** on Windows:
  ```powershell
  wsl.exe -d Ubuntu
  ```
- **Native Linux** (Ubuntu/Debian, Arch Linux, Fedora, Alpine).

### Automated Toolchain Check
Before building, run the dependency verification assistant:
```sh
./build.sh --check-deps
```
This inspects installed compilers, linkers, emulators, and fonts, and prints any missing packages for your distribution.

---

## 2. Core Build & Run Commands

akaOS supports both interactive terminal menus and headless CLI automation.

### Interactive Menus
```sh
./build.sh   # Opens interactive build menu
./run.sh     # Opens interactive QEMU launcher menu
```

### Common CLI Targets
```sh
# x86-64 UEFI (Default)
./build.sh --arch x86_64 --firmware uefi --disk-type fat32
./run.sh --arch x86_64 --firmware uefi --disk-type fat32 --gui

# x86-64 Dual UEFI/BIOS Hybrid ISO
./build.sh --arch x86_64 --firmware both --disk-type iso
./run.sh --arch x86_64 --firmware bios --disk-type iso

# AArch64 (ARM 64-bit) Direct Kernel
./build.sh --arch aarch64
./run.sh --arch aarch64 --headless

# RISC-V 64-bit Direct Kernel
./build.sh --arch riscv64
./run.sh --arch riscv64 --headless
```

---

## 3. Automated QEMU Regression Testing

Always validate changes before submitting pull requests:

```sh
# 1. Run the primary automated boot and regression test
./test.sh

# 2. Verify all 6 architecture compiler targets (smoke test)
./tools/build-arch-smoke.sh --all

# 3. Test filesystem persistence across reboots
./tools/test-persistence.sh

# 4. Test user authentication and privilege separation
./tools/test-auth.sh

# 5. Test native X11 window manager integration
./tools/test-upstream-wm.sh
```

---

## 4. Debugging with GDB

To boot QEMU paused with an active GDB stub listening on `localhost:1234`:
```sh
./debug.sh
```

In a separate terminal, attach your debugger:
```sh
gdb -ex "target remote localhost:1234" -ex "symbol-file build/XSCORE.ELF"
```
