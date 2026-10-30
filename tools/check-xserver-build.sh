#!/usr/bin/env bash
# Compile the akaOS X11 display server with the flags the main build uses.
#
# The display server changes often and the full build is slow, so this script
# only type checks and warns, exactly like the real build does, without
# linking the kernel and the user space runtime.
set -euo pipefail

# The build host can carry include and pkg-config search paths for an unrelated
# toolchain.  Clang honours CPATH even under -nostdinc, so it is cleared here
# as well.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DIR="$ROOT_DIR/userspace/bin/xserver"
OUTPUT_DIR="${1:-${TMPDIR:-/tmp}/akaos-xserver-check}"
GENERATED_DIR="$ROOT_DIR/build/generated"

if ! command -v clang >/dev/null 2>&1; then
    echo "clang is required to check the display server" >&2
    exit 2
fi

# The display server draws text with the same bitmap font the console uses, so
# the generated font header has to exist before the check.
if [[ ! -f "$GENERATED_DIR/font8x16_data.h" ]]; then
    mkdir -p "$GENERATED_DIR"
    python3 "$ROOT_DIR/tools/generate_vga_font.py" "$GENERATED_DIR/font8x16_data.h"
fi

mkdir -p "$OUTPUT_DIR"
status=0
for unit in main client window events draw resource input setup xcore; do
    if ! clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -DXSCORE_BUILD_TIMESTAMP="\"check\"" \
        -I "$ROOT_DIR/sdk/include" \
        -I "$ROOT_DIR/userspace/libc/include" \
        -I "$GENERATED_DIR" \
        -I "$SOURCE_DIR/include" \
        -c "$SOURCE_DIR/${unit}.c" -o "$OUTPUT_DIR/user-xserver-${unit}.o"; then
        echo "[FAIL] userspace/bin/xserver/${unit}.c" >&2
        status=1
    fi
done

if [[ $status -eq 0 ]]; then
    echo "[DONE] The X11 display server compiles cleanly"
fi
exit $status
