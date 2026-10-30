#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_VERSION=21.1.8
LLVM_COMMIT=2078da43e25a4623cab2d0d60decddf709aaea28
CACHE_ROOT="${AKAOS_LLVM_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos}"
LLVM_SOURCE_DIR="${AKAOS_LLVM_SOURCE_DIR:-$CACHE_ROOT/llvm-project-${LLVM_VERSION}}"

usage() {
    cat <<'EOF'
Usage: ./tools/build-compiler-builtins.sh --arch ARCH

Builds pinned LLVM compiler-rt ABI helpers needed by akaOS C++/libc targets,
including ARM32 soft-float EABI comparisons and RISC-V soft-float and binary128
operations plus x86_64 128-bit integer division. Supported targets: x86_64,
arm32, aarch64, riscv64, riscv32. This is a targeted helper archive, not a
complete compiler runtime for arbitrary apps.
EOF
}

arch=""
fp_mode_source=""
compile_extra_flags=()
builtin_sources=()
required_symbols=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --arch" >&2
                usage >&2
                exit 2
            fi
            arch="$2"
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

case "$arch" in
    x86_64)
        target=x86_64-unknown-none-elf
        builtin_sources=(
            divmodti4.c
            divti3.c
            modti3.c
            udivmodti4.c
            udivti3.c
            umodti3.c
        )
        required_symbols=(
            __divmodti4 __divti3 __modti3 __udivmodti4 __udivti3 __umodti3
        )
        ;;
    arm32)
        target=armv7-unknown-none-eabi
        fp_mode_source=arm/fp_mode.c
        compile_extra_flags=(
            -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none
        )
        builtin_sources=(
            addsf3.c
            adddf3.c
            comparedf2.c
            divsf3.c
            divdf3.c
            divdi3.c
            divmoddi4.c
            extendsfdf2.c
            fixdfdi.c
            fixdfsi.c
            fixunsdfdi.c
            fixunsdfsi.c
            fixsfsi.c
            fixunssfsi.c
            floatdidf.c
            floatsidf.c
            floatsisf.c
            floatundidf.c
            floatunsidf.c
            floatunsisf.c
            moddi3.c
            muldf3.c
            mulsf3.c
            negdf2.c
            negsf2.c
            subdf3.c
            subsf3.c
            truncdfsf2.c
            udivdi3.c
            udivmoddi4.c
            umoddi3.c
            arm/aeabi_dcmp.S
            arm/comparesf2.S
            arm/aeabi_fcmp.S
            arm/aeabi_ldivmod.S
            arm/aeabi_uldivmod.S
        )
        required_symbols=(
            __aeabi_dadd __aeabi_dcmpeq __aeabi_ddiv __aeabi_d2f
            __aeabi_d2ulz __aeabi_dmul __aeabi_dsub __aeabi_f2d
            __aeabi_fadd __aeabi_fdiv __aeabi_fmul __aeabi_fsub
            __aeabi_fcmpeq __aeabi_fcmplt __aeabi_fcmple __aeabi_fcmpge
            __aeabi_fcmpgt __aeabi_f2iz __aeabi_f2uiz __aeabi_i2d
            __aeabi_i2f __aeabi_ldivmod __aeabi_ui2d __aeabi_ui2f
            __aeabi_uldivmod __aeabi_ul2d
            __aeabi_l2d __fixunsdfdi __floatdidf __floatundidf __subdf3
        )
        ;;
    aarch64)
        target=aarch64-unknown-none-elf
        fp_mode_source=aarch64/fp_mode.c
        builtin_sources=(
            addtf3.c
            comparetf2.c
            divmodti4.c
            divti3.c
            divtf3.c
            extenddftf2.c
            extendsftf2.c
            extendsfdf2.c
            fixtfdi.c
            fixtfsi.c
            fixunstfdi.c
            floatditf.c
            floatsitf.c
            floatunsitf.c
            floatunditf.c
            multf3.c
            modti3.c
            subtf3.c
            trunctfdf2.c
            trunctfsf2.c
            udivmodti4.c
            udivti3.c
            umodti3.c
        )
        required_symbols=(
            __addtf3 __divmodti4 __divti3 __divtf3 __extenddftf2
            __extendsftf2 __extendsfdf2 __fe_getround
            __fe_raise_inexact __fixtfdi __fixunstfdi __floatditf
            __floatsitf __floatunsitf
            __floatunditf __modti3 __multf3 __netf2 __subtf3
            __trunctfdf2 __trunctfsf2
            __udivmodti4 __udivti3 __umodti3
        )
        ;;
    riscv64)
        target=riscv64-unknown-none-elf
        fp_mode_source=riscv/fp_mode.c
        builtin_sources=(
            addtf3.c
            addsf3.c
            adddf3.c
            comparesf2.c
            comparedf2.c
            comparetf2.c
            divsf3.c
            divdf3.c
            divmodti4.c
            divti3.c
            divtf3.c
            extenddftf2.c
            extendsftf2.c
            extendsfdf2.c
            fixdfdi.c
            fixdfsi.c
            fixunsdfdi.c
            fixunsdfsi.c
            fixsfsi.c
            fixunssfsi.c
            fixtfdi.c
            fixunstfdi.c
            floatdidf.c
            floatsidf.c
            floatsisf.c
            floatditf.c
            floatsitf.c
            floatunsitf.c
            floatundidf.c
            floatunditf.c
            floatunsidf.c
            floatunsisf.c
            muldf3.c
            mulsf3.c
            multf3.c
            modti3.c
            subdf3.c
            subsf3.c
            subtf3.c
            truncdfsf2.c
            trunctfdf2.c
            trunctfsf2.c
            udivmodti4.c
            udivti3.c
            umodti3.c
        )
        required_symbols=(
            __ltsf2 __ltdf2
            __adddf3 __addsf3 __addtf3 __divdf3 __divmodti4 __divsf3
            __divti3 __divtf3 __extenddftf2 __extendsftf2 __extendsfdf2
            __fe_getround
            __fe_raise_inexact __fixtfdi __fixdfdi __fixdfsi __fixsfsi
            __fixunsdfdi
            __fixunsdfsi __fixunssfsi __fixunstfdi
            __floatdidf __floatsidf __floatsisf
            __floatditf __floatsitf __floatundidf __floatunditf __floatunsidf
            __floatunsisf __floatunsitf __modti3 __muldf3 __mulsf3
            __multf3 __netf2 __subdf3 __subsf3 __subtf3
            __trunctfdf2 __trunctfsf2
            __truncdfsf2
            __udivmodti4 __udivti3 __umodti3
        )
        ;;
    riscv32)
        target=riscv32-unknown-none-elf
        fp_mode_source=riscv/fp_mode.c
        # Clang can lower _BitInt(128)/mode(TI) to the RV32 i128 ABI even
        # though it intentionally does not expose the nonstandard __int128
        # spelling on this target. compiler-rt's generic TF routines can use
        # those two TI-mode typedefs when the target capability is enabled.
        compile_extra_flags=(
            -DCRT_HAS_128BIT=1
            -D__uint128_t=tu_int
            -D__int128_t=ti_int
        )
        builtin_sources=(
            addtf3.c
            addsf3.c
            adddf3.c
            comparesf2.c
            comparedf2.c
            comparetf2.c
            divsf3.c
            divdf3.c
            divmodti4.c
            divti3.c
            divtf3.c
            extenddftf2.c
            extendsftf2.c
            extendsfdf2.c
            fixdfdi.c
            fixdfsi.c
            fixunsdfdi.c
            fixunsdfsi.c
            fixsfsi.c
            fixunssfsi.c
            fixtfdi.c
            fixtfsi.c
            fixunstfdi.c
            floatdidf.c
            floatsidf.c
            floatsisf.c
            floatditf.c
            floatsitf.c
            floatunsitf.c
            floatundidf.c
            floatunditf.c
            floatunsidf.c
            floatunsisf.c
            muldf3.c
            mulsf3.c
            multf3.c
            modti3.c
            subdf3.c
            subsf3.c
            subtf3.c
            truncdfsf2.c
            trunctfdf2.c
            trunctfsf2.c
            udivmodti4.c
            udivti3.c
            umodti3.c
        )
        required_symbols=(
            __ltsf2 __ltdf2
            __adddf3 __addsf3 __addtf3 __divdf3 __divmodti4 __divsf3
            __divti3 __divtf3 __extenddftf2 __extendsftf2 __extendsfdf2
            __fe_getround
            __fe_raise_inexact __fixtfdi __fixdfdi __fixdfsi __fixsfsi
            __fixunsdfdi
            __fixunsdfsi __fixunssfsi __fixunstfdi __fixtfsi
            __floatdidf __floatsidf __floatsisf
            __floatditf __floatsitf __floatundidf __floatunditf __floatunsidf
            __floatunsisf __floatunsitf __modti3 __muldf3 __mulsf3
            __multf3 __netf2 __subdf3 __subsf3 __subtf3
            __truncdfsf2 __trunctfdf2 __trunctfsf2
            __udivmodti4 __udivti3 __umodti3
        )
        ;;
    *)
        echo "Unsupported compiler-rt long-double target '$arch'" >&2
        usage >&2
        exit 2
        ;;
esac

for command_name in clang llvm-ar llvm-nm; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing build tool: $command_name" >&2
        echo "On Ubuntu WSL install: sudo apt-get install clang llvm" >&2
        exit 1
    fi
done

# build-libunwind verifies the pinned upstream checkout and its sparse source
# set. This keeps compiler-rt provenance aligned with the C++ runtime.
AKAOS_LLVM_SOURCES_ONLY=1 bash "$ROOT_DIR/tools/build-libunwind.sh" \
    --arch "$arch" >/dev/null

BUILTINS_SOURCE_DIR="$LLVM_SOURCE_DIR/compiler-rt/lib/builtins"
if [[ ! -f "$BUILTINS_SOURCE_DIR/CMakeLists.txt" ||
      ( -n "$fp_mode_source" && ! -f "$BUILTINS_SOURCE_DIR/$fp_mode_source" ) ]]; then
    echo "Pinned compiler-rt builtin sources are missing under $BUILTINS_SOURCE_DIR" >&2
    exit 1
fi
actual_commit="$(git -c "safe.directory=$LLVM_SOURCE_DIR" -C "$LLVM_SOURCE_DIR" rev-parse HEAD)"
if [[ "$actual_commit" != "$LLVM_COMMIT" ]]; then
    echo "LLVM source checkout is not the pinned compiler-rt revision: $actual_commit" >&2
    exit 1
fi

BUILD_DIR="$ROOT_DIR/build/compiler-rt/$arch"
ARTIFACT_DIR="$ROOT_DIR/build/cxx-runtime/llvm-${LLVM_VERSION}/$arch"
mkdir -p "$BUILD_DIR" "$ARTIFACT_DIR"
compile_flags=(
    "--target=$target"
    -std=c11 -O2 -Wall -Wextra -Werror
    -ffreestanding -fno-builtin -fno-stack-protector -fPIC
    -fvisibility=hidden
    -I"$BUILTINS_SOURCE_DIR"
    "${compile_extra_flags[@]}"
)

builtin_objects=()
for source in "${builtin_sources[@]}"; do
    object="$BUILD_DIR/${source%.*}.o"
    mkdir -p "$(dirname "$object")"
    if [[ "$source" == *.S ]]; then
        clang --target="$target" "${compile_extra_flags[@]}" \
            -ffreestanding -c "$BUILTINS_SOURCE_DIR/$source" -o "$object"
    else
        clang "${compile_flags[@]}" -c "$BUILTINS_SOURCE_DIR/$source" -o "$object"
    fi
    builtin_objects+=("$object")
done
if [[ -n "$fp_mode_source" ]]; then
    fp_mode_object="$BUILD_DIR/fp_mode.o"
    clang "${compile_flags[@]}" -c "$BUILTINS_SOURCE_DIR/$fp_mode_source" -o "$fp_mode_object"
    builtin_objects+=("$fp_mode_object")
fi

archive="$ARTIFACT_DIR/libakaos-compiler-builtins.a"
archive_temporary="$ARTIFACT_DIR/libakaos-compiler-builtins.tmp.a"
llvm-ar rcs "$archive_temporary" "${builtin_objects[@]}"
mv -f "$archive_temporary" "$archive"

for symbol in "${required_symbols[@]}"; do
    if ! llvm-nm --defined-only "$archive" | grep -E " T $symbol$" >/dev/null; then
        echo "$arch: compiler-rt archive is missing required symbol $symbol" >&2
        exit 1
    fi
done

echo "[DONE] Built pinned LLVM compiler-rt ABI helpers for $arch: $archive"
