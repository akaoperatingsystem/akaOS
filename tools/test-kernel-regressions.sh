#!/usr/bin/env bash
# Run serially: builders and older test helpers share generated artifacts.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESULT_DIR="$ROOT_DIR/.test-results/kernel-regressions"
mkdir -p "$RESULT_DIR"
summary="$RESULT_DIR/summary.txt"
: > "$summary"
suites=(
    test.sh tools/test-bios.sh tools/test-smp.sh
    tools/test-x86_32.sh tools/test-aarch64.sh tools/test-arm32.sh
    tools/test-riscv64.sh tools/test-riscv32.sh
    tools/test-persistence.sh tools/test-x86_32-persistence.sh
    tools/test-nonx86-persistence.sh tools/test-reboot.sh
    tools/test-nonx86-lifecycle.sh tools/test-advanced-network.sh
    tools/test-nonx86-network.sh tools/test-compositor-clients.sh
)
failed=0
for suite in "${suites[@]}"; do
    name="$(basename "$suite" .sh)"
    log="$RESULT_DIR/$name.log"
    printf '[RUN] %s\n' "$suite"
    if timeout 900s bash "$ROOT_DIR/$suite" > "$log" 2>&1; then
        printf '[PASS] %s\n' "$suite" | tee -a "$summary"
    else
        status=$?
        printf '[FAIL] %s (exit %s)\n' "$suite" "$status" | tee -a "$summary"
        tail -n 20 "$log"
        failed=1
    fi
done
exit "$failed"
