#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_VERSION=21.1.8
LLVM_TAG="llvmorg-${LLVM_VERSION}"
LLVM_COMMIT=2078da43e25a4623cab2d0d60decddf709aaea28
CACHE_ROOT="${AKAOS_LLVM_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos}"
LLVM_SOURCE_DIR="${AKAOS_LLVM_SOURCE_DIR:-$CACHE_ROOT/llvm-project-${LLVM_VERSION}}"
BUILD_ROOT="${AKAOS_LLVM_BUILD_ROOT:-$CACHE_ROOT/build/cxx-exception-runtime-${LLVM_VERSION}-$(basename "$LLVM_SOURCE_DIR")}"
ARTIFACT_ROOT="$ROOT_DIR/build/cxx-runtime/llvm-${LLVM_VERSION}"

usage() {
    cat <<'EOF'
Usage: ./tools/build-libunwind.sh [--arch ARCH|all]

Builds upstream LLVM libc++, libc++abi, and libunwind static runtimes for akaOS's
freestanding ELF targets. ARCH is x86_64, x86_32, aarch64, arm32, riscv64,
or riscv32. The default is all six targets. Ubuntu WSL dependencies:
  sudo apt-get install clang lld llvm cmake ninja-build git

This builds the standard C++ library and its exception/RTTI runtime archives;
it does not by itself establish upstream desktop compatibility.
EOF
}

requested_arch=all
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --arch" >&2
                usage >&2
                exit 2
            fi
            requested_arch="$2"
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

for command_name in clang clang++ ld.lld llvm-ar llvm-nm cmake ninja git; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing build tool: $command_name" >&2
        echo "On Ubuntu WSL install: sudo apt-get install clang lld llvm cmake ninja-build git" >&2
        exit 1
    fi
done

case "$requested_arch" in
    all) architectures=(x86_64 x86_32 aarch64 arm32 riscv64 riscv32) ;;
    x86_64|x86_32|aarch64|arm32|riscv64|riscv32)
        architectures=("$requested_arch")
        ;;
    *)
        echo "Unsupported architecture '$requested_arch'" >&2
        usage >&2
        exit 2
        ;;
esac

if [[ ! -d "$LLVM_SOURCE_DIR/.git" ]]; then
    if [[ -e "$LLVM_SOURCE_DIR" ]]; then
        echo "LLVM cache path exists but is not a Git source checkout: $LLVM_SOURCE_DIR" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$LLVM_SOURCE_DIR")"
    echo "[FETCH] LLVM ${LLVM_VERSION} sources from the official llvm-project repository"
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$LLVM_TAG" \
        https://github.com/llvm/llvm-project.git "$LLVM_SOURCE_DIR"
fi

safe_git() {
    git -c "safe.directory=$LLVM_SOURCE_DIR" -C "$LLVM_SOURCE_DIR" "$@"
}

safe_git sparse-checkout init --cone
safe_git sparse-checkout set libunwind libcxxabi libcxx cmake runtimes llvm/cmake llvm/include libc/shared libc/hdr libc/include libc/src/__support compiler-rt/lib/builtins
safe_git checkout "$LLVM_TAG"
if [[ ! -f "$LLVM_SOURCE_DIR/libunwind/CMakeLists.txt" ]]; then
    echo "LLVM cache does not contain the upstream libunwind source: $LLVM_SOURCE_DIR" >&2
    exit 1
fi
actual_commit="$(safe_git rev-parse HEAD)"
if [[ "$actual_commit" != "$LLVM_COMMIT" ]]; then
    echo "LLVM tag $LLVM_TAG resolved to unexpected commit $actual_commit" >&2
    echo "Expected pinned upstream commit $LLVM_COMMIT; refusing an unreviewed runtime source." >&2
    exit 1
fi

if [[ "${AKAOS_LLVM_SOURCES_ONLY:-0}" == 1 ]]; then
    echo "[DONE] Verified pinned LLVM sources for ${architectures[*]}"
    exit 0
fi

resource_dir="$(clang -print-resource-dir)"
common_flags="-ffreestanding -fno-builtin -fno-stack-protector -fPIC -nostdinc -isystem $resource_dir/include -isystem $ROOT_DIR/userspace/libc/include -isystem $ROOT_DIR/sdk/include -I $LLVM_SOURCE_DIR/libc"

for arch in "${architectures[@]}"; do
    processor=""
    target=""
    arch_flags=()
    case "$arch" in
        x86_64)
            processor=x86_64
            target=x86_64-unknown-none-elf
            ;;
        x86_32)
            processor=i686
            target=i686-unknown-none-elf
            # Keep the i386 x87 ABI available for long double types used by
            # libc++ headers; actual applications still select their own
            # floating-point baseline.
            arch_flags=(-mno-sse -mno-mmx)
            ;;
        aarch64)
            processor=aarch64
            target=aarch64-unknown-none-elf
            ;;
        arm32)
            processor=arm
            target=armv7-unknown-none-eabi
            arch_flags=(-mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none)
            ;;
        riscv64)
            processor=riscv64
            target=riscv64-unknown-none-elf
            ;;
        riscv32)
            processor=riscv32
            target=riscv32-unknown-none-elf
            ;;
    esac

    target_flags="${arch_flags[*]:-}"
    c_flags="$common_flags $target_flags"
    asm_flags="$target_flags"
    libcxxabi_baremetal=ON
    if [[ "$arch" == arm32 ]]; then
        asm_flags="$target_flags -fPIC"
        # LLVM libc++abi's bare-metal ARM EHABI cleanup stub uses an absolute
        # literal load for _Unwind_Resume, which cannot be linked into the
        # per-process static PIE. The normal relative branch remains PIC-safe.
        libcxxabi_baremetal=OFF
    fi
    # The target is a freestanding akaOS ABI, so libc++ cannot infer a host
    # ctype bit layout.  Use libc++'s portable rune-mask table.
    cxx_flags="$c_flags -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE"
    build_dir="$BUILD_ROOT/$arch"

    echo "[BUILD] LLVM libunwind ${LLVM_VERSION} for ${arch} (${target})"
    cmake -Wno-dev \
        -S "$LLVM_SOURCE_DIR/runtimes" \
        -B "$build_dir" \
        -G Ninja \
        "-DLLVM_ENABLE_RUNTIMES=libunwind;libcxxabi;libcxx" \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR="$processor" \
        -DCMAKE_C_COMPILER="$(command -v clang)" \
        -DCMAKE_CXX_COMPILER="$(command -v clang++)" \
        -DCMAKE_ASM_COMPILER="$(command -v clang)" \
        -DCMAKE_C_COMPILER_TARGET="$target" \
        -DCMAKE_CXX_COMPILER_TARGET="$target" \
        -DCMAKE_ASM_COMPILER_TARGET="$target" \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DCMAKE_C_FLAGS="$c_flags" \
        -DCMAKE_CXX_FLAGS="$cxx_flags" \
        -DCMAKE_ASM_FLAGS="$asm_flags" \
        -DLIBUNWIND_ENABLE_SHARED=OFF \
        -DLIBUNWIND_ENABLE_STATIC=ON \
        -DLIBUNWIND_ENABLE_THREADS=ON \
        -DLIBUNWIND_ENABLE_ASSERTIONS=OFF \
        -DLIBUNWIND_ENABLE_FRAME_APIS=ON \
        -DLIBUNWIND_IS_BAREMETAL=OFF \
        -DLIBUNWIND_HAS_DL_LIB=OFF \
        -DLIBUNWIND_HAS_PTHREAD_LIB=OFF \
        -DLIBUNWIND_INCLUDE_TESTS=OFF \
        -DLIBUNWIND_INCLUDE_DOCS=OFF \
        -DLIBUNWIND_INSTALL_LIBRARY=OFF \
        -DLIBCXXABI_ENABLE_SHARED=OFF \
        -DLIBCXXABI_ENABLE_STATIC=ON \
        -DLIBCXXABI_ENABLE_EXCEPTIONS=ON \
        -DLIBCXXABI_ENABLE_THREADS=ON \
        -DLIBCXXABI_HAS_PTHREAD_API=ON \
        -DLIBCXXABI_HAS_PTHREAD_LIB=OFF \
        -DLIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL=OFF \
        -DLIBCXXABI_ENABLE_ASSERTIONS=OFF \
        -DLIBCXXABI_BAREMETAL="$libcxxabi_baremetal" \
        -DLIBCXXABI_INCLUDE_TESTS=OFF \
        -DLIBCXXABI_INSTALL_LIBRARY=OFF \
        -DLIBCXX_ENABLE_SHARED=OFF \
        -DLIBCXX_ENABLE_STATIC=ON \
        -DLIBCXX_ENABLE_EXCEPTIONS=ON \
        -DLIBCXX_ENABLE_RTTI=ON \
        -DLIBCXX_ENABLE_THREADS=ON \
        -DLIBCXX_HAS_PTHREAD_API=ON \
        -DLIBCXX_HAS_PTHREAD_LIB=OFF \
        -DLIBCXX_HAS_RT_LIB=OFF \
        -DLIBCXX_HAS_ATOMIC_LIB=OFF \
        -DLIBCXX_INCLUDE_TESTS=OFF \
        -DLIBCXX_INCLUDE_DOCS=OFF \
        -DLIBCXX_INSTALL_LIBRARY=OFF

    cmake --build "$build_dir" --target cxxabi cxx --parallel "${AKAOS_BUILD_JOBS:-2}"
    unwind_archive="$build_dir/lib/libunwind.a"
    cxxabi_archive="$build_dir/lib/libc++abi.a"
    libcxx_archive="$build_dir/lib/libc++.a"
    if [[ ! -s "$unwind_archive" || ! -s "$cxxabi_archive" || ! -s "$libcxx_archive" ]]; then
        echo "Expected upstream libc++, libc++abi, and unwind archives were not all produced" >&2
        exit 1
    fi
    if ! llvm-ar t "$unwind_archive" | grep '^UnwindRegistersRestore\.S\.o$' >/dev/null; then
        echo "${arch}: upstream register-restore assembly is missing from libunwind.a" >&2
        exit 1
    fi
    if ! llvm-nm --defined-only "$unwind_archive" | grep -E ' T _Unwind_RaiseException$' >/dev/null; then
        echo "${arch}: libunwind.a does not export _Unwind_RaiseException" >&2
        exit 1
    fi
    if [[ "$arch" == arm32 ]]; then
        if ! llvm-nm --defined-only "$unwind_archive" | grep -E ' T __aeabi_unwind_cpp_pr0$' >/dev/null; then
            echo "${arch}: libunwind.a does not export the ARM EHABI personality entry point" >&2
            exit 1
        fi
    elif ! llvm-nm --defined-only "$unwind_archive" | grep -E ' T __register_frame$' >/dev/null; then
        echo "${arch}: libunwind.a does not export the DWARF frame-registration API" >&2
        exit 1
    fi
    for symbol in __cxa_throw __gxx_personality_v0 __dynamic_cast; do
        if ! llvm-nm --defined-only "$cxxabi_archive" | grep -E " T $symbol$" >/dev/null; then
            echo "${arch}: libc++abi.a does not export required C++ runtime symbol $symbol" >&2
            exit 1
        fi
    done
    artifact_dir="$ARTIFACT_ROOT/$arch"
    mkdir -p "$artifact_dir"
    cp "$unwind_archive" "$artifact_dir/libunwind.a"
    cp "$cxxabi_archive" "$artifact_dir/libc++abi.a"
    cp "$libcxx_archive" "$artifact_dir/libc++.a"
    mkdir -p "$artifact_dir/include/c++"
    # The libc++ header tree holds well over a thousand files and the artifact
    # directory sits on a slow host filesystem, so the copy runs as one
    # archive stream instead of one file operation per header.  A marker
    # records the copied tree so repeated builds skip the copy entirely.
    source_functional_header="$build_dir/include/c++/v1/functional"
    if [[ ! -s "$source_functional_header" ]]; then
        echo "${arch}: upstream libc++ functional header is missing or empty" >&2
        exit 1
    fi
    header_marker="$artifact_dir/include/c++/.akaos-v1-complete"
    header_signature="$(stat -c %s "$source_functional_header")"
    if [[ ! -f "$header_marker" || "$(cat "$header_marker")" != "$header_signature" ]]; then
        rm -rf "$artifact_dir/include/c++/v1"
        mkdir -p "$artifact_dir/include/c++/v1"
        tar -C "$build_dir/include/c++/v1" -cf - . | \
            tar -C "$artifact_dir/include/c++/v1" -xf -
        printf '%s\n' "$header_signature" > "$header_marker"
    fi
    # The streamed copy can preserve a newer zero-byte placeholder.
    cp -p "$source_functional_header" "$artifact_dir/include/c++/v1/functional"

    echo "[DONE] ${arch} upstream libc++, libc++abi, libunwind, and headers: $artifact_dir"
done
