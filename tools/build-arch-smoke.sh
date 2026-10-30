#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DIR="$ROOT_DIR/tools/arch-smoke"
OUT_DIR="$ROOT_DIR/build/arch-smoke"
INCLUDE_DIR="$ROOT_DIR/sdk/include"

usage() {
    cat <<'EOF'
Usage: ./tools/build-arch-smoke.sh [--all | --arch NAME]

Builds a freestanding C/assembly/linker smoke image for:
  x86_64 x86_32 aarch64 arm32 riscv64 riscv32

This validates target toolchain and SDK portability. The smoke outputs are not
bootable XSCore ports; native direct-kernel paths are tested separately for
AArch64 and ARM32, while RISC-V remains smoke-build only.
EOF
}

for tool in clang ld.lld readelf; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build dependency: $tool" >&2
        echo "On Ubuntu: sudo apt install clang lld binutils" >&2
        exit 1
    fi
done

build_one() {
    local arch="$1"
    local target
    local expected_machine
    case "$arch" in
        x86_64)
            target="x86_64-unknown-none-elf"
            expected_machine="Advanced Micro Devices X86-64"
            ;;
        x86_32)
            target="i386-unknown-none-elf"
            expected_machine="Intel 80386"
            ;;
        aarch64)
            target="aarch64-unknown-none-elf"
            expected_machine="AArch64"
            ;;
        arm32)
            target="armv7-unknown-none-eabi"
            expected_machine="ARM"
            ;;
        riscv64)
            target="riscv64-unknown-none-elf"
            expected_machine="RISC-V"
            ;;
        riscv32)
            target="riscv32-unknown-none-elf"
            expected_machine="RISC-V"
            ;;
        *)
            echo "Unknown architecture '$arch'" >&2
            usage >&2
            exit 2
            ;;
    esac

    local arch_dir="$OUT_DIR/$arch"
    mkdir -p "$arch_dir"
    echo "[ARCH] $arch ($target)"

    clang --target="$target" \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -I"$INCLUDE_DIR" \
        -c "$SOURCE_DIR/main.c" -o "$arch_dir/main.o"

    clang --target="$target" \
        -ffreestanding -fno-pic -fno-pie \
        -c "$SOURCE_DIR/start_$arch.S" -o "$arch_dir/start.o"

    ld.lld -nostdlib -static -z max-page-size=0x1000 \
        -T "$SOURCE_DIR/linker.ld" \
        "$arch_dir/start.o" "$arch_dir/main.o" \
        -o "$arch_dir/AKAOS-$arch.ELF"

    local machine
    machine="$(readelf -h "$arch_dir/AKAOS-$arch.ELF" | sed -n 's/^  Machine: *//p')"
    if [[ "$machine" != *"$expected_machine"* ]]; then
        echo "Unexpected ELF machine for $arch: $machine" >&2
        exit 1
    fi
    echo "[PASS] $arch architecture smoke build: $machine"
}

mkdir -p "$OUT_DIR"
mode="all"
requested=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)
            mode="all"
            shift
            ;;
        --arch)
            [[ $# -ge 2 ]] || { echo "--arch requires a name" >&2; usage >&2; exit 2; }
            mode="one"
            requested="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option '$1'" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "$mode" == "one" ]]; then
    build_one "$requested"
else
    for arch in x86_64 x86_32 aarch64 arm32 riscv64 riscv32; do
        build_one "$arch"
    done
fi

echo "[DONE] architecture smoke outputs: $OUT_DIR"
