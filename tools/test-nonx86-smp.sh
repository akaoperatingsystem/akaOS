#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

if [[ "${AKAOS_TEST_SKIP_BUILD:-0}" != 1 ]]; then
    bash "$ROOT_DIR/tools/build-aarch64.sh"
    bash "$ROOT_DIR/tools/build-arm32.sh"
    bash "$ROOT_DIR/tools/build-riscv64.sh"
    bash "$ROOT_DIR/tools/build-riscv32.sh"
fi

run_smp_case() {
    local name="$1" expected="$2"; shift 2
    local log="$BUILD_DIR/test-${name}-smp.log"
    rm -f "$log"
    echo "[TEST] ${name} native secondary CPUs (QEMU -smp 4)..."

    # QEMU is intentionally time-bounded: the guest remains at its login
    # prompt, so a timeout after the marker is a successful test completion.
    set +e
    timeout 20s "$@" -serial stdio -display none -no-reboot >"$log" 2>&1
    local rc=$?
    set -e
    if ! grep -Fq "$expected" "$log"; then
        echo "[FAIL] ${name} did not report a secondary heartbeat." >&2
        cat "$log" >&2
        return 1
    fi
    if ! grep -Fq "Secondary per-CPU timer interrupts observed" "$log"; then
        echo "[FAIL] ${name} did not observe a timer interrupt on every online secondary." >&2
        cat "$log" >&2
        return 1
    fi
    if grep -Eq '\[FATAL\]|Exception|(^|[^a-z])EXC([ :\[]|$)|ESR:' "$log"; then
        echo "[FAIL] ${name} reported a fatal exception." >&2
        cat "$log" >&2
        return 1
    fi
    echo "[PASS] ${name} secondary heartbeat and timer IRQ (timeout status ${rc})"
}

run_smp_case aarch64 "Secondary CPU bring-up and heartbeat self-test passed" \
    qemu-system-aarch64 -M virt -cpu cortex-a57 -smp 4 \
    -kernel "$BUILD_DIR/XSCORE_AARCH64.ELF" -m 128M -device ramfb
run_smp_case arm32 "Secondary CPU bring-up and heartbeat self-test passed" \
    qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -smp 4 \
    -kernel "$BUILD_DIR/XSCORE_ARM32.ELF" -m 128M -device ramfb
run_smp_case riscv64 "Secondary CPU bring-up and heartbeat self-test passed" \
    qemu-system-riscv64 -M virt -smp 4 \
    -kernel "$BUILD_DIR/XSCORE_RISCV64.ELF" -m 128M
run_smp_case riscv32 "Secondary CPU bring-up and heartbeat self-test passed" \
    qemu-system-riscv32 -M virt -smp 4 \
    -kernel "$BUILD_DIR/XSCORE_RISCV32.ELF" -m 128M

echo "[PASS] Non-x86 secondary CPU bring-up and per-CPU timer IRQs verified on AArch64, ARM32, RISC-V64, and RISC-V32."
