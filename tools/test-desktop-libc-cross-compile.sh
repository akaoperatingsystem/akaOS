#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="$ROOT_DIR/build/cross-desktop-libc-probes"
mkdir -p "$OUTPUT_DIR"

compile_port() {
    local port="$1"
    local target="$2"
    shift 2
    local -a arch_flags=("$@")

    for unit in math fenv stdio malloc string time setjmp tgmath dlfcn syscall eventfd \
        timerfd pthread hello desktop login; do
        local -a unit_flags=("${arch_flags[@]}")
        if [[ "$port" == "x86_32" && ( "$unit" == "stdio" || "$unit" == "malloc" || "$unit" == "string" || "$unit" == "hello" ) ]]; then
            unit_flags+=( -m80387 )
        fi
        local source="$ROOT_DIR/userspace/libc/src/$unit.c"
        if [[ "$unit" == "tgmath" ]]; then
            source="$ROOT_DIR/userspace/libc/tests/desktop_tgmath.c"
        elif [[ "$unit" == "hello" ]]; then
            source="$ROOT_DIR/userspace/bin/hello/main.c"
        elif [[ "$unit" == "desktop" ]]; then
            source="$ROOT_DIR/userspace/bin/desktop/main.c"
        elif [[ "$unit" == "login" ]]; then
            source="$ROOT_DIR/userspace/bin/login/main.c"
        fi
        clang --target="$target" \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
            "${unit_flags[@]}" \
            -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/userspace/libc/include" \
            -c "$source" \
            -o "$OUTPUT_DIR/$port-$unit.o"
    done
    echo "[PASS] $port libc/desktop/login prerequisite source compilation"
}

compile_port x86_64 x86_64-unknown-none-elf -mno-red-zone -mcmodel=small
compile_port x86_32 i686-unknown-none-elf
compile_port aarch64 aarch64-unknown-none-elf
compile_port arm32 armv7-unknown-none-eabi \
    -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none
compile_port riscv64 riscv64-unknown-none-elf
compile_port riscv32 riscv32-unknown-none-elf

compile_crt() {
    local port="$1"
    local target="$2"
    local source="$3"
    shift 3
    clang --target="$target" "${@}" -ffreestanding -fno-builtin \
        -c "$ROOT_DIR/userspace/libc/crt/$source" \
        -o "$OUTPUT_DIR/$port-$(basename "$source").o"
}

compile_crt x86_64 x86_64-unknown-none-elf x86_64/crt0.S \
    -mno-red-zone -mcmodel=small
compile_crt x86_32 i686-unknown-none-elf x86_32/crt0.S
compile_crt x86_32 i686-unknown-none-elf x86_32/dynamic-crt0.S
compile_crt aarch64 aarch64-unknown-none-elf aarch64/crt0.S
compile_crt arm32 armv7-unknown-none-eabi arm32/crt0.S \
    -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none
compile_crt riscv64 riscv64-unknown-none-elf riscv64/crt0.S
compile_crt riscv32 riscv32-unknown-none-elf riscv32/crt0.S

echo "[PASS] Desktop prerequisite libc and dynamic-loader sources compile for all six architectures."
