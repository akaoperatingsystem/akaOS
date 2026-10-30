#!/bin/bash
# Capture the improved akaOS `about` and `version` command output.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/tty-info-screen.ppm"
SCREEN_PNG="$BUILD_DIR/tty-info-screen.png"
RUN_LOG="$BUILD_DIR/tty-info-capture.log"

"$ROOT_DIR/build.sh" >/dev/null

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.tty-info.fd"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG"
: > "$RUN_LOG"
(
    for _ in $(seq 1 600); do
        grep -Fq "[INFO] akaOS shell active" "$RUN_LOG" && break
        sleep 0.1
    done
    sleep 1
    printf 'sendkey a\nsendkey b\nsendkey o\nsendkey u\nsendkey t\nsendkey ret\n'
    sleep 1
    printf 'sendkey v\nsendkey e\nsendkey r\nsendkey s\nsendkey i\nsendkey o\nsendkey n\nsendkey ret\n'
    sleep 1
    printf 'screendump %s\n' "$SCREEN_PPM"
    sleep 1
    printf 'quit\n'
) | qemu-system-x86_64 \
    -machine q35 \
    -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$OVMF_VARS" \
    -drive format=raw,file="$BUILD_DIR/akaos.img" \
    -serial file:"$RUN_LOG" \
    -monitor stdio \
    -display none \
    -no-reboot >"$BUILD_DIR/tty-info-monitor.log" 2>&1 || true

grep -Fq "[INFO] akaOS shell active" "$RUN_LOG"
test -s "$SCREEN_PPM"

python3 - "$SCREEN_PPM" "$SCREEN_PNG" <<'EOF'
import sys
from PIL import Image
source, target = sys.argv[1], sys.argv[2]
Image.open(source).convert("RGB").save(target)
print("saved", target)
EOF
