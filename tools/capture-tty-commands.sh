#!/bin/bash
# Boot akaOS, execute interactive shell commands (`ls`, `cat /etc/motd`), and capture the screen.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

BUILD_DIR="$ROOT_DIR/build"
SCREEN_PPM="$BUILD_DIR/tty-commands-screen.ppm"
SCREEN_PNG="$BUILD_DIR/tty-commands-screen.png"
RUN_LOG="$BUILD_DIR/tty-commands-capture.log"

"$ROOT_DIR/build.sh" >/dev/null

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.tty-cmd.fd"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"

rm -f "$SCREEN_PPM" "$SCREEN_PNG" "$RUN_LOG"
: > "$RUN_LOG"
(
    for _ in $(seq 1 600); do
        grep -Fq "[INFO] akaOS shell active" "$RUN_LOG" && break
        sleep 0.1
    done
    sleep 1
    printf 'sendkey l\nsendkey s\nsendkey ret\n'
    sleep 1
    printf 'sendkey c\nsendkey a\nsendkey t\nsendkey spc\nsendkey slash\nsendkey e\nsendkey t\nsendkey c\nsendkey slash\nsendkey m\nsendkey o\nsendkey t\nsendkey d\nsendkey ret\n'
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
    -no-reboot >"$BUILD_DIR/tty-commands-monitor.log" 2>&1 || true

grep -Fq "[INFO] akaOS shell active" "$RUN_LOG"
test -s "$SCREEN_PPM"

python3 - "$SCREEN_PPM" "$SCREEN_PNG" <<'EOF'
import sys
from PIL import Image
source, target = sys.argv[1], sys.argv[2]
Image.open(source).convert("RGB").save(target)
print("saved", target)
EOF
