#!/usr/bin/env bash
# Cross compiler wrapper that targets the akaOS x86-64 userland.
#
# The wrapper applies the freestanding akaOS include set, the pinned LLVM
# resource directory and PIC code generation.  Link-time flags stay in LDFLAGS
# so that the same wrapper serves shared-library and executable links.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_RESOURCE_DIR="${AKAOS_LLVM_RESOURCE_DIR:-/usr/lib/llvm-21/lib/clang/21/include}"

extra_includes=()
if [[ -n "${AKAOS_EXTRA_INCLUDE_DIRS:-}" ]]; then
    read -r -a extra_includes <<<"${AKAOS_EXTRA_INCLUDE_DIRS}"
fi

include_args=()
for directory in "${extra_includes[@]:-}"; do
    [[ -n "$directory" ]] && include_args+=("-I$directory")
done

exec clang \
    --target=x86_64-unknown-none-elf \
    -nostdinc \
    -idirafter "$LLVM_RESOURCE_DIR" \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -fPIC \
    -D__AKAOS__ \
    -I"$ROOT_DIR/sdk/include" \
    -I"$ROOT_DIR/userspace/libc/include" \
    "${include_args[@]}" \
    "$@"
