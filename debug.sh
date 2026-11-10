#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

"$ROOT_DIR/build.sh"
OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$ROOT_DIR/build/OVMF_VARS.debug.fd"
prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"

# Keep the unauthenticated debugger accessible only to this host user.
GDB_DIR="$(mktemp -d /tmp/akaos-gdb.XXXXXXXXXX)"
trap 'rm -rf -- "$GDB_DIR"' EXIT
GDB_SOCKET="$GDB_DIR/gdb.sock"

echo "XSCore is paused at startup. Attach with GDB 9 or newer:"
echo "gdb -ex 'target remote $GDB_SOCKET' -ex 'symbol-file build/XSCORE.ELF'"
qemu-system-x86_64 \
    -machine q35 \
    -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$OVMF_VARS" \
    -drive format=raw,file="$ROOT_DIR/build/akaos.img" \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -chardev "socket,path=$GDB_SOCKET,server=on,wait=off,id=gdb0" \
    -gdb chardev:gdb0 -S
