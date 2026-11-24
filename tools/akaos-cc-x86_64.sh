#!/usr/bin/env bash
# Cross compiler wrapper: compile with Clang, link directly with LLD.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_RESOURCE_DIR="${AKAOS_LLVM_RESOURCE_DIR:-/usr/lib/llvm-21/lib/clang/21/include}"
if [[ $# -eq 1 && "$1" == "-dumpmachine" ]]; then
    printf '%s\n' "${AKAOS_AUTOCONF_HOST:-x86_64-akaos-linux-gnu}"
    exit 0
fi
if [[ $# -eq 1 && ( "$1" == "-dumpversion" || "$1" == "--version" ) ]]; then
    exec clang "$1"
fi
compile_mode=0
has_source=0
for arg in "$@"; do
    case "$arg" in *.c|*.cc|*.cpp|*.m|*.mm) has_source=1 ;; esac
done
args=()
while (($#)); do
    arg="$1"; shift
    case "$arg" in
        -c|-E|-S|-M|-MM)
            compile_mode=1; args+=("$arg")
            ;;
        -o)
            args+=("-o" "$1"); shift
            ;;
        -Wl,*)
            if (( compile_mode || has_source )); then args+=("$arg"); else
                # The link step takes the options of the compiler driver
                # directly, so the prefix of the driver is removed and the
                # rest of the option keeps its own form.
                args+=("${arg#-Wl,}")
            fi
            ;;
        -I|-D|-include|-isystem|-idirafter|-x)
            # The include paths and the definitions belong to the compiler,
            # and a test that compiles and links in one step needs them as
            # well.  A step that only links does not.
            if (( compile_mode || has_source )); then args+=("$arg" "$1"); fi
            shift
            ;;
        -I*|-D*)
            # The build systems pass the include paths and the definitions
            # with the value attached to the option.
            if (( compile_mode || has_source )); then args+=("$arg"); fi
            ;;
        -T|-z|-m|-soname|-version-script)
            if (( compile_mode )); then args+=("$arg" "$1"); fi
            shift
            ;;
        -nostdinc|-f*|-O*|-std=*|-W*) if (( compile_mode )); then args+=("$arg"); fi ;;
        -fuse-ld=*) ;;
        *.c|*.cc|*.cpp|*.m|*.mm|*.o|*.lo|*.a|*.so|*.so.*|-L*|-l*|-shared|-pie|-static|-nostdlib|-s|-r|--gc-sections|--eh-frame-hdr|--allow-shlib-undefined|--no-undefined|--as-needed|--no-as-needed)
            args+=("$arg")
            ;;
        -*) if (( compile_mode )); then args+=("$arg"); fi ;;
        *) args+=("$arg");;
    esac
done
if (( compile_mode || has_source )); then
    exec clang --target=x86_64-unknown-none-elf -nostdinc -idirafter "$LLVM_RESOURCE_DIR" \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC -fuse-ld=lld -nostartfiles \
        -D__AKAOS__ -I"$ROOT_DIR/sdk/include" -I"$ROOT_DIR/userspace/libc/include" \
        "${args[@]}"
fi
exec ld.lld -m elf_x86_64 "${args[@]}"
