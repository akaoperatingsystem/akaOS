# Developer Workflow & Porting Guide

This guide details how to build, test, and contribute to akaOS.

## Development Host Setup
- Recommended environment: Linux or WSL (Ubuntu).
- Toolchain: Clang/LLD, Python 3, QEMU multi-architecture emulators (`qemu-system-x86_64`, `qemu-system-aarch64`, `qemu-system-arm`, `qemu-system-riscv64`, `qemu-system-riscv32`).

## Quick Validation Commands
```sh
# Compile and run QEMU test suite
./test.sh

# Verify all 6 architecture compiler targets
./tools/build-arch-smoke.sh --all
```
