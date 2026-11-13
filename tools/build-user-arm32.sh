#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/host-common.sh"
BUILD_DIR="$ROOT_DIR/build/user_arm32"
INCLUDE_DIR="$ROOT_DIR/sdk/include"
USER_INCLUDE_DIR="$ROOT_DIR/userspace/libc/include"
BUILD_TIMESTAMP="${BUILD_TIMESTAMP:-$(date +%m%d%Y%H%M%S)}"
export BUILD_TIMESTAMP
mkdir -p "$BUILD_DIR"
FONT_FILE="$(akaos_find_dejavu_font)"

USER_LIBC_SOURCES=(
    "$ROOT_DIR/userspace/libc/src/string.c"
    "$ROOT_DIR/userspace/libc/src/stdlib.c"
    "$ROOT_DIR/userspace/libc/src/stdio.c"
    "$ROOT_DIR/userspace/libc/src/malloc.c"
    "$ROOT_DIR/userspace/libc/src/math.c"
    "$ROOT_DIR/userspace/libc/src/fenv.c"
    "$ROOT_DIR/userspace/libc/src/setjmp.c"
    "$ROOT_DIR/userspace/libc/src/syscall.c"
    "$ROOT_DIR/userspace/libc/src/time.c"
    "$ROOT_DIR/userspace/libc/src/account.c"
    "$ROOT_DIR/userspace/libc/src/posix.c"
    "$ROOT_DIR/userspace/libc/src/pthread.c"
    "$ROOT_DIR/userspace/libc/src/atomic64.c"
    "$ROOT_DIR/userspace/libc/src/toolkit.c"
    "$ROOT_DIR/userspace/libc/src/dbus.c"
    "$ROOT_DIR/userspace/libc/src/dbus_codec.c"
    "$ROOT_DIR/userspace/libc/src/dlfcn.c"
    "$ROOT_DIR/userspace/libc/src/epoll.c"
    "$ROOT_DIR/userspace/libc/src/eventfd.c"
    "$ROOT_DIR/userspace/libc/src/timerfd.c"
    "$ROOT_DIR/userspace/libc/src/semaphore.c"
    "$ROOT_DIR/userspace/libc/src/cxxabi.c"
    "$ROOT_DIR/userspace/libc/src/crt_init.c"
    "$ROOT_DIR/userspace/libc/src/getopt.c"
    "$ROOT_DIR/userspace/libc/src/regex.c"
    "$ROOT_DIR/userspace/libc/src/syslog.c"
)

USER_LIBC_OBJECTS=()
for source in "${USER_LIBC_SOURCES[@]}"; do
    base="$(basename "$source" .c)"
    object="$BUILD_DIR/libc-$base.o"
    source_extra_flags=()
    if [[ "arm32" == "x86_32" &&
          ("$base" == "malloc" || "$base" == "stdio" || "$base" == "string" || "$base" == "math" || "$base" == "time") ]]; then
        # These libc translation units implement float/long-double APIs.
        # Keep other i386 userspace code on the no-FPU baseline until the
        # scheduler's x87 isolation regression proves general application use.
        source_extra_flags=(-m80387)
    fi
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -ffunction-sections -fdata-sections \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none "${source_extra_flags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    USER_LIBC_OBJECTS+=("$object")
done

llvm-ar rcs "$BUILD_DIR/libc.a" "${USER_LIBC_OBJECTS[@]}"

clang --target=armv7-unknown-none-eabi \
    -ffreestanding -fpie \
    -c "$ROOT_DIR/userspace/libc/crt/arm32/crt0.S" \
    -o "$BUILD_DIR/user-crt0.o"

clang --target=armv7-unknown-none-eabi \
    -ffreestanding -fpie \
    -c "$ROOT_DIR/userspace/libc/crt/arm32/crt0.S" \
    -o "$BUILD_DIR/user-dynamic-crt0.o"

COMPILER_BUILTINS=()
if [[ "arm32" == "arm32" || "arm32" == "aarch64" || "arm32" == "riscv64" ||
      "arm32" == "riscv32" ]]; then
    bash "$ROOT_DIR/tools/build-compiler-builtins.sh" --arch "arm32"
    COMPILER_BUILTINS=("$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/arm32/libakaos-compiler-builtins.a")
fi

build_xserver() {
    local name="xserver"
    local source_dir="$ROOT_DIR/userspace/bin/xserver"
    local output="XSERVER.ELF"
    local objects=()
    for unit in main client window events draw resource input setup xcore; do
        local object="$BUILD_DIR/user-$name-$unit.o"
        clang --target=armv7-unknown-none-eabi             -std=c17 -Os -Wall -Wextra -Werror             -ffreestanding -fno-builtin -fno-stack-protector -fpie             -ffunction-sections -fdata-sections             -DXSCORE_BUILD_TIMESTAMP=""$BUILD_TIMESTAMP""             -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none             -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" -I"$ROOT_DIR/build/generated" -I"$source_dir/include"             -c "$source_dir/$unit.c" -o "$object"
        objects+=("$object")
    done
    ld.lld -m armelf -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000         -T "$ROOT_DIR/userspace/linker/user.ld"         "$BUILD_DIR/user-crt0.o" "${objects[@]}"         --whole-archive "$BUILD_DIR/libc-crt_init.o" --no-whole-archive         "$BUILD_DIR/libc.a"         "${COMPILER_BUILTINS[@]}"         -o "$BUILD_DIR/$output"
}

build_bzip2() {
    local source_dir="$ROOT_DIR/userspace/bin/bzip2"
    local objects=()
    local bzip_extra_flags=()
    if [[ "arm32" == "x86_32" ]]; then
        bzip_extra_flags=(-m80387)
    fi
    for unit in blocksort huffman crctable randtable compress decompress bzlib bzip2; do
        local object="$BUILD_DIR/bzip2-$unit.o"
        clang --target=armv7-unknown-none-eabi             -std=c17 -Os -Wall -Wextra             -ffreestanding -fno-builtin -fno-stack-protector -fpie             -ffunction-sections -fdata-sections             -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none "${bzip_extra_flags[@]}"             -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" -I"$source_dir"             -D_FILE_OFFSET_BITS=64             -c "$source_dir/$unit.c" -o "$object"
        objects+=("$object")
    done
    ld.lld -m armelf -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000         -T "$ROOT_DIR/userspace/linker/user.ld"         "$BUILD_DIR/user-crt0.o" "${objects[@]}"         --whole-archive "$BUILD_DIR/libc-crt_init.o" --no-whole-archive         "$BUILD_DIR/libc.a"         "${COMPILER_BUILTINS[@]}"         -o "$BUILD_DIR/BZIP2.ELF"
}

build_user_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    local user_extra_flags=()
    if [[ "arm32" == "x86_32" && ("$name" == "hello" || "$name" == "hello-pie" || "$name" == "awk" || "$name" == "bc" || "$name" == "cal") ]]; then
        user_extra_flags=(-m80387)
    fi
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie         -ffunction-sections -fdata-sections \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none "${user_extra_flags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    ld.lld -m armelf -nostdlib -static --gc-sections -s -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/arm32.ld" \
        "$BUILD_DIR/user-crt0.o" "$main_object" \
        --whole-archive "$BUILD_DIR/libc-crt_init.o" --no-whole-archive \
        "$BUILD_DIR/libc.a" \
        "${COMPILER_BUILTINS[@]}" \
        -o "$BUILD_DIR/$output"
}

build_user_cpp_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    local cxx_runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/arm32"
    bash "$ROOT_DIR/tools/build-libunwind.sh" --arch arm32
    clang++ --target=armv7-unknown-none-eabi \
        -std=c++17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fstack-protector-strong -fpie \
        -fexceptions -frtti -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -I"$cxx_runtime_dir/include/c++/v1" -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    # PIE segments are link-relative (starting at zero); XSCore supplies the
    # per-process architecture-specific load bias at exec time.
    ld.lld -m armelf -nostdlib -pie -static --eh-frame-hdr -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "${USER_LIBC_OBJECTS[@]}" "$main_object" \
         \
        "$cxx_runtime_dir/libc++.a" "$cxx_runtime_dir/libc++abi.a" \
        "$cxx_runtime_dir/libunwind.a" \
        "${COMPILER_BUILTINS[@]}" \
        -o "$BUILD_DIR/$output"
}

build_user_pie_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    local user_extra_flags=()
    if [[ "arm32" == "x86_32" && ("$name" == "hello" || "$name" == "hello-pie") ]]; then
        user_extra_flags=(-m80387)
    fi
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none "${user_extra_flags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    # ET_DYN static PIE images use zero-based segments; XSCore applies their
    # bounded REL/RELA relative relocations at the native user load base.
    ld.lld -m armelf -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "$main_object" \
        --whole-archive "$BUILD_DIR/libc-crt_init.o" --no-whole-archive \
        "$BUILD_DIR/libc.a" \
        "${COMPILER_BUILTINS[@]}" \
        -o "$BUILD_DIR/$output"
}

build_account_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    ld.lld -m armelf -nostdlib -static --gc-sections -s -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/arm32.ld" \
        "$BUILD_DIR/user-crt0.o" "$main_object" \
        --whole-archive "$BUILD_DIR/libc-crt_init.o" --no-whole-archive \
        "$BUILD_DIR/libc.a" \
        "${COMPILER_BUILTINS[@]}" \
        -o "$BUILD_DIR/$output"
}

build_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-answer.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -c "$ROOT_DIR/userspace/libc/tests/shared_answer.c" -o "$object"
    ld.lld -m armelf -nostdlib -shared -soname libakaos-demo.so --hash-style=gnu -z max-page-size=0x1000 \
        "$object" -o "$BUILD_DIR/$output"
}

build_static_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-static.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -ftls-model=initial-exec \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -c "$ROOT_DIR/userspace/libc/tests/shared_static.c" -o "$object"
    ld.lld -m armelf -nostdlib -shared -soname libakaos-static.so \
        --hash-style=gnu -z max-page-size=0x1000 \
        "$object" -o "$BUILD_DIR/$output"
}

build_versioned_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-versioned.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -c "$ROOT_DIR/userspace/libc/tests/shared_versioned.c" -o "$object"
    ld.lld -m armelf -nostdlib -shared -soname libakaos-versioned.so \
        --hash-style=gnu -z max-page-size=0x1000 \
        --version-script "$ROOT_DIR/userspace/libc/tests/shared_versioned.map" \
        "$object" -o "$BUILD_DIR/$output"
}

build_shared_libc() {
    local output="$1"
    local shared_objects=()
    for source in "${USER_LIBC_SOURCES[@]}"; do
        base="$(basename "$source" .c)"
        object="$BUILD_DIR/shared-libc-$base.o"
        local shared_source_extra_flags=()
        if [[ "arm32" == "x86_32" &&
              ("$base" == "malloc" || "$base" == "stdio" || "$base" == "string" || "$base" == "math" || "$base" == "time") ]]; then
            shared_source_extra_flags=(-m80387)
        fi
        clang --target=armv7-unknown-none-eabi \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
            -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
            -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none "${shared_source_extra_flags[@]}" \
            -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
            -c "$source" -o "$object"
        shared_objects+=("$object")
    done
    local signal_object="$BUILD_DIR/shared-libc-signal.o"
    clang --target=armv7-unknown-none-eabi -ffreestanding -fPIC \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -c "$ROOT_DIR/userspace/libc/tests/shared_signal.S" \
        -o "$signal_object"
    ld.lld -m armelf -nostdlib -shared -soname "$output" \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        "${shared_objects[@]}" "$signal_object" "${COMPILER_BUILTINS[@]}" \
        -o "$BUILD_DIR/$output"
}

build_shared_libm() {
    local output="$1"
    local runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/arm32"
    ld.lld -m armelf -nostdlib -shared -soname "$output" \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        "$BUILD_DIR/shared-libc-math.o" "${COMPILER_BUILTINS[@]}" \
        "$BUILD_DIR/libc.so.6" -o "$BUILD_DIR/$output"
    ln -sf "$output" "$BUILD_DIR/libm.so"
}

build_shared_cxx_runtime() {
    local runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/arm32"
    ld.lld -m armelf -nostdlib -shared --eh-frame-hdr \
        -soname libunwind.so.1 --hash-style=gnu -z max-page-size=0x1000 \
        --no-undefined --whole-archive "$runtime_dir/libunwind.a" \
        --no-whole-archive "${COMPILER_BUILTINS[@]}" \
        "$BUILD_DIR/libc.so.6" -o "$BUILD_DIR/libunwind.so.1"
    ld.lld -m armelf -nostdlib -shared --eh-frame-hdr \
        -soname libc++abi.so.1 --hash-style=gnu -z max-page-size=0x1000 \
        --no-undefined --whole-archive "$runtime_dir/libc++abi.a" \
        --no-whole-archive "${COMPILER_BUILTINS[@]}" \
        "$BUILD_DIR/libunwind.so.1" "$BUILD_DIR/libc.so.6" \
        -o "$BUILD_DIR/libc++abi.so.1"
    ld.lld -m armelf -nostdlib -shared --eh-frame-hdr \
        -soname libc++.so.1 --hash-style=gnu -z max-page-size=0x1000 \
        --no-undefined --whole-archive "$runtime_dir/libc++.a" \
        --no-whole-archive "${COMPILER_BUILTINS[@]}" \
        "$BUILD_DIR/libc++abi.so.1" "$BUILD_DIR/libunwind.so.1" \
        "$BUILD_DIR/libc.so.6" -o "$BUILD_DIR/libc++.so.1"
}

build_dynamic_cpp_program() {
    local source="$1"
    local output="$2"
    local runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/arm32"
    local object="$BUILD_DIR/user-cxx-dynamic-main.o"
    clang++ --target=armv7-unknown-none-eabi \
        -std=c++17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fstack-protector-strong -fpie \
        -fexceptions -frtti -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -I"$runtime_dir/include/c++/v1" -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m armelf -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/arm32-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" --no-as-needed \
        "$BUILD_DIR/libc++.so.1" "$BUILD_DIR/libc++abi.so.1" \
        "$BUILD_DIR/libunwind.so.1" "$BUILD_DIR/libc.so.6" \
        "${COMPILER_BUILTINS[@]}" -o "$BUILD_DIR/$output"
}

build_cxx_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-cxx.o"
    local fini_object="$BUILD_DIR/shared-cxx-fini.o"
    clang++ --target=armv7-unknown-none-eabi \
        -std=c++17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -fno-exceptions -fno-rtti \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$ROOT_DIR/userspace/libc/tests/shared_cxx.cpp" -o "$object"
    clang --target=armv7-unknown-none-eabi -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$ROOT_DIR/userspace/libc/crt/cxx-dso-fini.c" -o "$fini_object"
    ld.lld -m armelf -nostdlib -shared -soname "$output" \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        "$object" "$fini_object" --no-as-needed "$BUILD_DIR/libc.so.6" \
        -o "$BUILD_DIR/$output"
}

build_dependent_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-dependent.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -c "$ROOT_DIR/userspace/libc/tests/shared_dependent.c" -o "$object"
    ld.lld -m armelf -nostdlib -shared -soname libakaos-dependent.so -z max-page-size=0x1000 \
        "$object" "$BUILD_DIR/libakaos-demo.so" \
        "$BUILD_DIR/libakaos-versioned.so" -o "$BUILD_DIR/$output"
}

build_loader_fanout_objects() {
    local provider_objects=()
    for index in 0 1 2 3 4; do
        local provider_object="$BUILD_DIR/loader-provider-$index.o"
        local provider_output="$BUILD_DIR/libakaos-loader-dep-$index.so"
        clang --target=armv7-unknown-none-eabi \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
            -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
            -DAKAOS_LOADER_DEPENDENCY_INDEX=$index \
            -c "$ROOT_DIR/userspace/libc/tests/shared_loader_leaf.c" \
            -o "$provider_object"
        ld.lld -m armelf -nostdlib -shared \
            -soname "libakaos-loader-dep-$index.so" \
            -z max-page-size=0x1000 "$provider_object" \
            -o "$provider_output"
        provider_objects+=("$provider_output")
    done
    local fanout_object="$BUILD_DIR/shared-loader-fanout.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -c "$ROOT_DIR/userspace/libc/tests/shared_loader_fanout.c" \
        -o "$fanout_object"
    ld.lld -m armelf -nostdlib -shared \
        -soname libakaos-loader-fanout.so -z max-page-size=0x1000 \
        --no-undefined "$fanout_object" --no-as-needed \
        "${provider_objects[@]}" -o "$BUILD_DIR/libakaos-loader-fanout.so"
}

build_runtime_library() {
    local output="$1"
    local runtime_id="$2"
    local object="$BUILD_DIR/runtime-${runtime_id}.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -DAKAOS_RUNTIME_ID="${runtime_id}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$ROOT_DIR/userspace/libc/tests/desktop_runtime.c" -o "$object"
    ld.lld -m armelf -nostdlib -shared -soname "$output" -z max-page-size=0x1000 \
        "$object" "$BUILD_DIR/runtime-io.o" -o "$BUILD_DIR/$output"
}

build_dynamic_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    clang --target=armv7-unknown-none-eabi \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    # This is a real ET_DYN executable: PT_INTERP names the native loader and
    # libc plus the demo provider contribute DT_NEEDED entries and symbol
    # relocations.  No libc object is linked into the executable; startup,
    # memory, string, and I/O calls must resolve through libc.so.6.
    ld.lld -m armelf -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/arm32-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$main_object" \
        --no-as-needed "$BUILD_DIR/libc.so.6" \
        "$BUILD_DIR/libakaos-demo.so" "${COMPILER_BUILTINS[@]}" \
        -o "$BUILD_DIR/$output"
}

clang --target=armv7-unknown-none-eabi \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
    -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/libc/tests/desktop_io.c" \
    -o "$BUILD_DIR/runtime-io.o"

echo "[BUILD] akaOS ARM32 userspace binaries"
build_user_program hello "$ROOT_DIR/userspace/bin/hello/main.c" HELLO.ELF
build_user_cpp_program cxxabi-test "$ROOT_DIR/userspace/bin/cxxabi-test/main.cpp" CXXABI_TEST.ELF
build_user_pie_program hello-pie "$ROOT_DIR/userspace/bin/hello/main.c" HELLO_PIE.ELF
build_user_program cat "$ROOT_DIR/userspace/bin/cat/main.c" CAT.ELF
build_user_program ls "$ROOT_DIR/userspace/bin/ls/main.c" LS.ELF
build_user_program dir "$ROOT_DIR/userspace/bin/dir/main.c" DIR.ELF
build_user_program dircolors "$ROOT_DIR/userspace/bin/dircolors/main.c" DIRCOLORS.ELF
build_user_program grep "$ROOT_DIR/userspace/bin/grep/main.c" GREP.ELF
build_user_program head "$ROOT_DIR/userspace/bin/head/main.c" HEAD.ELF
build_user_program tail "$ROOT_DIR/userspace/bin/tail/main.c" TAIL.ELF
build_user_program wc "$ROOT_DIR/userspace/bin/wc/main.c" WC.ELF
build_user_program uname "$ROOT_DIR/userspace/bin/uname/main.c" UNAME.ELF
build_user_program akafetch "$ROOT_DIR/userspace/bin/akafetch/main.c" AKAFETCH.ELF
build_account_program whoami "$ROOT_DIR/userspace/bin/whoami/main.c" WHOAMI.ELF
build_user_program touch "$ROOT_DIR/userspace/bin/touch/main.c" TOUCH.ELF
build_user_program mkdir "$ROOT_DIR/userspace/bin/mkdir/main.c" MKDIR.ELF
build_user_program rm "$ROOT_DIR/userspace/bin/rm/main.c" RM.ELF
build_user_program cp "$ROOT_DIR/userspace/bin/cp/main.c" CP.ELF
build_user_program mv "$ROOT_DIR/userspace/bin/mv/main.c" MV.ELF
build_user_program pwd "$ROOT_DIR/userspace/bin/pwd/main.c" PWD.ELF
build_user_program echo "$ROOT_DIR/userspace/bin/echo/main.c" ECHO.ELF
build_user_program clear "$ROOT_DIR/userspace/bin/clear/main.c" CLEAR.ELF
build_user_program sync "$ROOT_DIR/userspace/bin/sync/main.c" SYNC.ELF
build_user_program chmod "$ROOT_DIR/userspace/bin/chmod/main.c" CHMOD.ELF
build_user_program chown "$ROOT_DIR/userspace/bin/chown/main.c" CHOWN.ELF
build_user_program ted "$ROOT_DIR/userspace/bin/ted/main.c" TED.ELF
build_user_program init "$ROOT_DIR/userspace/bin/init/main.c" INIT.ELF
build_account_program login "$ROOT_DIR/userspace/bin/login/main.c" LOGIN.ELF
build_account_program su "$ROOT_DIR/userspace/bin/su/main.c" SU.ELF
build_account_program passwd "$ROOT_DIR/userspace/bin/passwd/main.c" PASSWD.ELF
build_account_program sudo "$ROOT_DIR/userspace/bin/sudo/main.c" SUDO.ELF
build_user_program reboot "$ROOT_DIR/userspace/bin/reboot/main.c" REBOOT.ELF
build_user_program poweroff "$ROOT_DIR/userspace/bin/poweroff/main.c" POWEROFF.ELF
build_user_program lumen "$ROOT_DIR/userspace/bin/lumen/main.c" LUMEN.ELF
build_user_program palette "$ROOT_DIR/userspace/bin/palette/main.c" PALETTE.ELF
build_user_program aurora "$ROOT_DIR/userspace/bin/aurora/main.c" AURORA.ELF
build_user_program terminal "$ROOT_DIR/userspace/bin/terminal/main.c" TERMINAL.ELF
build_user_program notify "$ROOT_DIR/userspace/bin/notify/main.c" NOTIFY.ELF
build_user_program beep "$ROOT_DIR/userspace/bin/beep/main.c" BEEP.ELF
build_user_program ping "$ROOT_DIR/userspace/bin/ping/main.c" PING.ELF
build_user_program netstat "$ROOT_DIR/userspace/bin/netstat/main.c" NETSTAT.ELF
build_user_program tty "$ROOT_DIR/userspace/bin/tty/main.c" TTY.ELF
build_user_program stty "$ROOT_DIR/userspace/bin/stty/main.c" STTY.ELF
build_user_program ps "$ROOT_DIR/userspace/bin/ps/main.c" PS.ELF
build_account_program akash "$ROOT_DIR/userspace/bin/akash/main.c" AKASH.ELF
build_account_program bash "$ROOT_DIR/userspace/bin/bash/main.c" BASH.ELF
build_user_program nslookup "$ROOT_DIR/userspace/bin/nslookup/main.c" NSLOOKUP.ELF
build_user_program curl "$ROOT_DIR/userspace/bin/curl/main.c" CURL.ELF
build_user_program pkg "$ROOT_DIR/userspace/bin/pkg/main.c" PKG.ELF
build_user_program wget "$ROOT_DIR/userspace/bin/wget/main.c" WGET.ELF
build_user_program httpd "$ROOT_DIR/userspace/bin/httpd/main.c" HTTPD.ELF
build_user_program akaos-config "$ROOT_DIR/userspace/bin/akaos-config/main.c" AKAOS_CONFIG.ELF
build_user_program sysmon "$ROOT_DIR/userspace/bin/sysmon/main.c" SYSMON.ELF
build_user_program ip "$ROOT_DIR/userspace/bin/ip/main.c" IP.ELF
build_user_program mount "$ROOT_DIR/userspace/bin/mount/main.c" MOUNT.ELF
build_user_program umount "$ROOT_DIR/userspace/bin/umount/main.c" UMOUNT.ELF
build_user_program telnetd "$ROOT_DIR/userspace/bin/telnetd/main.c" TELNETD.ELF
build_user_program tftpd "$ROOT_DIR/userspace/bin/tftpd/main.c" TFTPD.ELF
build_user_program tftp "$ROOT_DIR/userspace/bin/tftp/main.c" TFTP.ELF
build_user_program desktop "$ROOT_DIR/userspace/bin/desktop/main.c" DESKTOP.ELF
build_user_program desktop-compat "$ROOT_DIR/userspace/bin/desktop-compat/main.c" DESKTOP_COMPAT.ELF
build_user_program notepad "$ROOT_DIR/userspace/bin/notepad/main.c" NOTEPAD.ELF
build_user_program taskmgr "$ROOT_DIR/userspace/bin/taskmgr/main.c" TASKMGR.ELF
build_user_program webviewer "$ROOT_DIR/userspace/bin/webviewer/main.c" WEBVIEWER.ELF
build_user_program status_cgi "$ROOT_DIR/userspace/bin/status_cgi/main.c" STATUS_CGI.ELF
build_user_program less "$ROOT_DIR/userspace/bin/less/main.c" LESS.ELF
build_user_program more "$ROOT_DIR/userspace/bin/more/main.c" MORE.ELF
build_user_program find "$ROOT_DIR/userspace/bin/find/main.c" FIND.ELF
build_user_program sort "$ROOT_DIR/userspace/bin/sort/main.c" SORT.ELF
build_user_program uniq "$ROOT_DIR/userspace/bin/uniq/main.c" UNIQ.ELF
build_user_program xargs "$ROOT_DIR/userspace/bin/xargs/main.c" XARGS.ELF
build_user_program test "$ROOT_DIR/userspace/bin/test/main.c" TEST.ELF
build_account_program sshd "$ROOT_DIR/userspace/bin/sshd/main.c" SSHD.ELF
build_account_program ssh "$ROOT_DIR/userspace/bin/ssh/main.c" SSH.ELF
build_user_program sleep "$ROOT_DIR/userspace/bin/sleep/main.c" SLEEP.ELF
build_user_program which "$ROOT_DIR/userspace/bin/which/main.c" WHICH.ELF
build_user_program env "$ROOT_DIR/userspace/bin/env/main.c" ENV.ELF
build_user_program cut "$ROOT_DIR/userspace/bin/cut/main.c" CUT.ELF
build_user_program tr "$ROOT_DIR/userspace/bin/tr/main.c" TR.ELF
build_user_program df "$ROOT_DIR/userspace/bin/df/main.c" DF.ELF
build_user_program du "$ROOT_DIR/userspace/bin/du/main.c" DU.ELF
build_user_program kill "$ROOT_DIR/userspace/bin/kill/main.c" KILL.ELF
build_user_program apropos "$ROOT_DIR/userspace/bin/apropos/main.c" APROPOS.ELF
build_user_program aspell "$ROOT_DIR/userspace/bin/aspell/main.c" ASPELL.ELF
build_user_program at "$ROOT_DIR/userspace/bin/at/main.c" AT.ELF
build_user_program awk "$ROOT_DIR/userspace/bin/awk/main.c" AWK.ELF
build_user_program basename "$ROOT_DIR/userspace/bin/basename/main.c" BASENAME.ELF
build_user_program dirname "$ROOT_DIR/userspace/bin/dirname/main.c" DIRNAME.ELF
build_user_program dirs "$ROOT_DIR/userspace/bin/dirs/main.c" DIRS.ELF
build_user_program dos2unix "$ROOT_DIR/userspace/bin/dos2unix/main.c" DOS2UNIX.ELF
build_user_program dmesg "$ROOT_DIR/userspace/bin/dmesg/main.c" DMESG.ELF
build_user_program base32 "$ROOT_DIR/userspace/bin/base32/main.c" BASE32.ELF
build_user_program base64 "$ROOT_DIR/userspace/bin/base64/main.c" BASE64.ELF
build_user_program bc "$ROOT_DIR/userspace/bin/bc/main.c" BC.ELF
build_user_program bg "$ROOT_DIR/userspace/bin/bg/main.c" BG.ELF
build_user_program bind "$ROOT_DIR/userspace/bin/bind/main.c" BIND.ELF
build_user_program break "$ROOT_DIR/userspace/bin/break/main.c" BREAK.ELF
build_user_program builtin "$ROOT_DIR/userspace/bin/builtin/main.c" BUILTIN.ELF
build_user_program cal "$ROOT_DIR/userspace/bin/cal/main.c" CAL.ELF
build_user_program caller "$ROOT_DIR/userspace/bin/caller/main.c" CALLER.ELF
build_user_program case "$ROOT_DIR/userspace/bin/case/main.c" CASE.ELF
build_user_program cd "$ROOT_DIR/userspace/bin/cd/main.c" CD.ELF
build_user_program cfdisk "$ROOT_DIR/userspace/bin/cfdisk/main.c" CFDISK.ELF
build_user_program chattr "$ROOT_DIR/userspace/bin/chattr/main.c" CHATTR.ELF
build_user_program chgrp "$ROOT_DIR/userspace/bin/chgrp/main.c" CHGRP.ELF
build_user_program chpasswd "$ROOT_DIR/userspace/bin/chpasswd/main.c" CHPASSWD.ELF
build_user_program chroot "$ROOT_DIR/userspace/bin/chroot/main.c" CHROOT.ELF
build_user_program chkconfig "$ROOT_DIR/userspace/bin/chkconfig/main.c" CHKCONFIG.ELF
build_user_program cksum "$ROOT_DIR/userspace/bin/cksum/main.c" CKSUM.ELF
build_user_program hyprland "$ROOT_DIR/userspace/bin/hyprland/main.c" HYPRLAND.ELF
build_user_program hyprctl "$ROOT_DIR/userspace/bin/hyprctl/main.c" HYPRCTL.ELF
build_user_program sway "$ROOT_DIR/userspace/bin/sway/main.c" SWAY.ELF
build_user_program river "$ROOT_DIR/userspace/bin/river/main.c" RIVER.ELF
build_user_program niri "$ROOT_DIR/userspace/bin/niri/main.c" NIRI.ELF
build_user_program wayfire "$ROOT_DIR/userspace/bin/wayfire/main.c" WAYFIRE.ELF
build_user_program labwc "$ROOT_DIR/userspace/bin/labwc/main.c" LABWC.ELF
build_user_program weston "$ROOT_DIR/userspace/bin/weston/main.c" WESTON.ELF
build_user_program qtile "$ROOT_DIR/userspace/bin/qtile/main.c" QTILE.ELF
build_user_program awesome "$ROOT_DIR/userspace/bin/awesome/main.c" AWESOME.ELF
build_user_program xmonad "$ROOT_DIR/userspace/bin/xmonad/main.c" XMONAD.ELF
build_user_program herbstluftwm "$ROOT_DIR/userspace/bin/herbstluftwm/main.c" HERBSTLUFTWM.ELF
build_user_program spectrwm "$ROOT_DIR/userspace/bin/spectrwm/main.c" SPECTRWM.ELF
build_user_program fluxbox "$ROOT_DIR/userspace/bin/fluxbox/main.c" FLUXBOX.ELF
build_user_program icewm "$ROOT_DIR/userspace/bin/icewm/main.c" ICEWM.ELF
build_user_program jwm "$ROOT_DIR/userspace/bin/jwm/main.c" JWM.ELF
build_user_program wmaker "$ROOT_DIR/userspace/bin/wmaker/main.c" WMAKER.ELF
build_user_program fvwm "$ROOT_DIR/userspace/bin/fvwm/main.c" FVWM.ELF
build_user_program budgie "$ROOT_DIR/userspace/bin/budgie/main.c" BUDGIE.ELF
build_user_program cosmic "$ROOT_DIR/userspace/bin/cosmic/main.c" COSMIC.ELF
build_user_program deepin "$ROOT_DIR/userspace/bin/deepin/main.c" DEEPIN.ELF
build_user_program pantheon "$ROOT_DIR/userspace/bin/pantheon/main.c" PANTHEON.ELF
build_user_program ukui "$ROOT_DIR/userspace/bin/ukui/main.c" UKUI.ELF
build_user_program enlightenment "$ROOT_DIR/userspace/bin/enlightenment/main.c" ENLIGHTENMENT.ELF
build_bzip2
build_user_program pkill "$ROOT_DIR/userspace/bin/pkill/main.c" PKILL.ELF
build_user_program uptime "$ROOT_DIR/userspace/bin/uptime/main.c" UPTIME.ELF
build_user_program free "$ROOT_DIR/userspace/bin/free/main.c" FREE.ELF
build_user_program vmstat "$ROOT_DIR/userspace/bin/vmstat/main.c" VMSTAT.ELF
build_user_program watch "$ROOT_DIR/userspace/bin/watch/main.c" WATCH.ELF
build_user_program sed "$ROOT_DIR/userspace/bin/sed/main.c" SED.ELF
build_user_program diff "$ROOT_DIR/userspace/bin/diff/main.c" DIFF.ELF
build_user_program diff3 "$ROOT_DIR/userspace/bin/diff3/main.c" DIFF3.ELF
build_user_program cmp "$ROOT_DIR/userspace/bin/cmp/main.c" CMP.ELF
build_user_program patch "$ROOT_DIR/userspace/bin/patch/main.c" PATCH.ELF
build_user_program tar "$ROOT_DIR/userspace/bin/tar/main.c" TAR.ELF
build_user_program cpio "$ROOT_DIR/userspace/bin/cpio/main.c" CPIO.ELF
build_user_program cron "$ROOT_DIR/userspace/bin/cron/main.c" CRON.ELF
build_user_program csplit "$ROOT_DIR/userspace/bin/csplit/main.c" CSPLIT.ELF
build_user_program date "$ROOT_DIR/userspace/bin/date/main.c" DATE.ELF
build_user_program declare "$ROOT_DIR/userspace/bin/declare/main.c" DECLARE.ELF
build_user_program dc "$ROOT_DIR/userspace/bin/dc/main.c" DC.ELF
build_user_program dd "$ROOT_DIR/userspace/bin/dd/main.c" DD.ELF
build_user_program ddrescue "$ROOT_DIR/userspace/bin/ddrescue/main.c" DDRESCUE.ELF
build_user_program ddrescuelog "$ROOT_DIR/userspace/bin/ddrescuelog/main.c" DDRESCUELOG.ELF
build_user_program lsusb "$ROOT_DIR/userspace/bin/lsusb/main.c" LSUSB.ELF
build_user_program crond "$ROOT_DIR/userspace/bin/crond/main.c" CROND.ELF
build_user_program crontab "$ROOT_DIR/userspace/bin/crontab/main.c" CRONTAB.ELF
build_user_program gzip "$ROOT_DIR/userspace/bin/gzip/main.c" GZIP.ELF
build_user_program gunzip "$ROOT_DIR/userspace/bin/gunzip/main.c" GUNZIP.ELF
build_user_program route "$ROOT_DIR/userspace/bin/route/main.c" ROUTE.ELF
build_user_program traceroute "$ROOT_DIR/userspace/bin/traceroute/main.c" TRACEROUTE.ELF
build_user_program dig "$ROOT_DIR/userspace/bin/dig/main.c" DIG.ELF
build_user_program arp "$ROOT_DIR/userspace/bin/arp/main.c" ARP.ELF
build_user_program lscpu "$ROOT_DIR/userspace/bin/lscpu/main.c" LSCPU.ELF
build_user_program lspci "$ROOT_DIR/userspace/bin/lspci/main.c" LSPCI.ELF
build_user_program gpuinfo "$ROOT_DIR/userspace/bin/gpuinfo/main.c" GPUINFO.ELF
build_user_program sndinfo "$ROOT_DIR/userspace/bin/sndinfo/main.c" SNDINFO.ELF
build_user_program acpi "$ROOT_DIR/userspace/bin/acpi/main.c" ACPI.ELF
build_user_program xsdisplay "$ROOT_DIR/userspace/bin/xsdisplay/main.c" XSDISPLAY.ELF
build_xserver
# wayland-bridge compatibility binary for headless/generic architecture profiles
build_user_program wayland-bridge "$ROOT_DIR/userspace/bin/desktop-compat/main.c" WAYLAND_BRIDGE.ELF
build_user_program x11-client "$ROOT_DIR/userspace/bin/desktop-compat/main.c" X11_CLIENT.ELF
build_user_program x11-wm-client "$ROOT_DIR/userspace/bin/desktop-compat/main.c" X11_WM_CLIENT.ELF
build_user_program cairo-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" CAIRO_UPSTREAM_TEST.ELF
build_user_program fontconfig-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" FONTCONFIG_UPSTREAM_TEST.ELF
build_user_program freetype-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" FREETYPE_UPSTREAM_TEST.ELF
build_user_program fribidi-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" FRIBIDI_UPSTREAM_TEST.ELF
build_user_program glib-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" GLIB_UPSTREAM_TEST.ELF
build_user_program graphene-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" GRAPHENE_UPSTREAM_TEST.ELF
build_user_program gtk-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" GTK_UPSTREAM_TEST.ELF
build_user_program gtk3-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" GTK3_UPSTREAM_TEST.ELF
build_user_program harfbuzz-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" HARFBUZZ_UPSTREAM_TEST.ELF
build_user_program pango-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" PANGO_UPSTREAM_TEST.ELF
build_user_program wayland-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" WAYLAND_UPSTREAM_TEST.ELF
build_user_program xkbcommon-upstream-test "$ROOT_DIR/userspace/bin/desktop-compat/main.c" XKBCOMMON_UPSTREAM_TEST.ELF
build_user_program qtquick-wayland-smoke "$ROOT_DIR/userspace/bin/desktop-compat/main.c" QT_QUICK_WAYLAND_SMOKE.ELF
build_user_program dbus-daemon "$ROOT_DIR/userspace/bin/dbus-daemon/main.c" DBUS_DAEMON.ELF
build_user_program dltest "$ROOT_DIR/userspace/bin/dltest/main.c" DLTEST.ELF
build_user_program display-protocols "$ROOT_DIR/userspace/bin/display-protocols/main.c" DISPLAY_PROTOCOLS.ELF
build_user_program ld-akaos "$ROOT_DIR/userspace/bin/ld-akaos/main.c" LD_AKAOS.ELF
build_shared_object libakaos-demo.so
build_static_shared_object libakaos-static.so
build_versioned_shared_object libakaos-versioned.so
build_shared_libc libc.so.6
build_shared_cxx_runtime
build_shared_libm libm.so.6
build_dynamic_cpp_program "$ROOT_DIR/userspace/bin/cxx-dynamic-test/main.cpp" CXX_DYNAMIC_TEST.ELF
build_cxx_shared_object libakaos-cxx.so
build_dynamic_program dynamic-hello "$ROOT_DIR/userspace/bin/dynamic-hello/main.c" DYNAMIC_HELLO.ELF
build_dependent_shared_object libakaos-dependent.so
build_loader_fanout_objects
build_runtime_library libpthread.so 1
build_runtime_library libglib.so 2
build_runtime_library libgio.so 3
build_runtime_library libgtk.so 4
build_runtime_library libgtk-4.so 5
build_runtime_library libQt6Core.so 6
build_runtime_library libQt6Gui.so 7
build_runtime_library libQt6Widgets.so 8

echo "[BUILD] akaOS ARM32 initramfs archive"
python3 "$ROOT_DIR/tools/mkinitramfs.py" "$ROOT_DIR/build/initramfs_arm32.img" \
    --dir "/bin" \
    --dir "/etc" \
    --dir "/usr" \
    --dir "/usr/share" \
    --dir "/usr/share/fonts" \
    --dir "/usr/share/fonts/truetype" \
    --dir "/usr/share/fonts/truetype/akaos" \
    --file "/usr/share/fonts/truetype/akaos/default.ttf:$FONT_FILE:0444" \
    --dir "/usr/local" \
    --dir "/usr/local/lib" \
    --dir "/usr/local/lib/fonts" \
    --file "/usr/local/lib/fonts/DejaVuSans.ttf:$FONT_FILE:0444" \
    --dir "/usr/share/licenses" \
    --dir "/usr/share/licenses/DejaVu" \
    --file "/usr/share/licenses/DejaVu/LICENSE.txt:$ROOT_DIR/userspace/licenses/DejaVuSans-LICENSE.txt:0444" \
    --dir "/root:0700" \
    --dir "/home" \
    --dir "/home/user:0755" \
    --dir "/mnt" \
    --dir "/tmp" \
    --file "/bin/hello:$BUILD_DIR/HELLO.ELF:exec" \
    --file "/bin/cxxabi-test:$BUILD_DIR/CXXABI_TEST.ELF:exec" \
    --file "/bin/cxx-dynamic-test:$BUILD_DIR/CXX_DYNAMIC_TEST.ELF:exec" \
    --file "/bin/dynamic-hello:$BUILD_DIR/DYNAMIC_HELLO.ELF:exec" \
    --file "/bin/hello-pie:$BUILD_DIR/HELLO_PIE.ELF:exec" \
    --file "/bin/hello-pi:$BUILD_DIR/HELLO_PIE.ELF:exec" \
    --file "/bin/cat:$BUILD_DIR/CAT.ELF:exec" \
    --file "/bin/ls:$BUILD_DIR/LS.ELF:exec" \
    --file "/bin/dir:$BUILD_DIR/DIR.ELF:exec" \
    --file "/bin/d:$BUILD_DIR/DIR.ELF:exec" \
    --file "/bin/dircolors:$BUILD_DIR/DIRCOLORS.ELF:exec" \
    --file "/bin/grep:$BUILD_DIR/GREP.ELF:exec" \
    --file "/bin/head:$BUILD_DIR/HEAD.ELF:exec" \
    --file "/bin/tail:$BUILD_DIR/TAIL.ELF:exec" \
    --file "/bin/wc:$BUILD_DIR/WC.ELF:exec" \
    --file "/bin/uname:$BUILD_DIR/UNAME.ELF:exec" \
    --file "/bin/akafetch:$BUILD_DIR/AKAFETCH.ELF:exec" \
    --file "/bin/whoami:$BUILD_DIR/WHOAMI.ELF:exec" \
    --file "/bin/touch:$BUILD_DIR/TOUCH.ELF:exec" \
    --file "/bin/mkdir:$BUILD_DIR/MKDIR.ELF:exec" \
    --file "/bin/rm:$BUILD_DIR/RM.ELF:exec" \
    --file "/bin/cp:$BUILD_DIR/CP.ELF:exec" \
    --file "/bin/mv:$BUILD_DIR/MV.ELF:exec" \
    --file "/bin/chmod:$BUILD_DIR/CHMOD.ELF:exec" \
    --file "/bin/chown:$BUILD_DIR/CHOWN.ELF:exec" \
    --file "/bin/ted:$BUILD_DIR/TED.ELF:exec" \
    --file "/bin/pwd:$BUILD_DIR/PWD.ELF:exec" \
    --file "/bin/echo:$BUILD_DIR/ECHO.ELF:exec" \
    --file "/bin/clear:$BUILD_DIR/CLEAR.ELF:exec" \
    --file "/bin/sync:$BUILD_DIR/SYNC.ELF:exec" \
    --file "/bin/init:$BUILD_DIR/INIT.ELF:exec" \
    --file "/bin/login:$BUILD_DIR/LOGIN.ELF:4755" \
    --file "/bin/su:$BUILD_DIR/SU.ELF:4755" \
    --file "/bin/passwd:$BUILD_DIR/PASSWD.ELF:4755" \
    --file "/bin/sudo:$BUILD_DIR/SUDO.ELF:4755" \
    --file "/bin/reboot:$BUILD_DIR/REBOOT.ELF:exec" \
    --file "/bin/poweroff:$BUILD_DIR/POWEROFF.ELF:exec" \
    --file "/bin/lumen:$BUILD_DIR/LUMEN.ELF:exec" \
    --file "/bin/palette:$BUILD_DIR/PALETTE.ELF:exec" \
    --file "/bin/aurora:$BUILD_DIR/AURORA.ELF:exec" \
    --file "/bin/terminal:$BUILD_DIR/TERMINAL.ELF:exec" \
    --file "/bin/notify:$BUILD_DIR/NOTIFY.ELF:exec" \
    --file "/bin/beep:$BUILD_DIR/BEEP.ELF:exec" \
    --file "/bin/ping:$BUILD_DIR/PING.ELF:exec" \
    --file "/bin/netstat:$BUILD_DIR/NETSTAT.ELF:exec" \
    --file "/bin/tty:$BUILD_DIR/TTY.ELF:exec" \
    --file "/bin/stty:$BUILD_DIR/STTY.ELF:exec" \
    --file "/bin/ps:$BUILD_DIR/PS.ELF:exec" \
    --file "/bin/akash:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/bash:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/sh:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/rbash:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/nslookup:$BUILD_DIR/NSLOOKUP.ELF:exec" \
    --file "/bin/curl:$BUILD_DIR/CURL.ELF:exec" \
    --file "/bin/pkg:$BUILD_DIR/PKG.ELF:exec" \
    --file "/bin/wget:$BUILD_DIR/WGET.ELF:exec" \
    --file "/bin/httpd:$BUILD_DIR/HTTPD.ELF:exec" \
    --file "/bin/akaos-config:$BUILD_DIR/AKAOS_CONFIG.ELF:exec" \
    --file "/bin/sysmon:$BUILD_DIR/SYSMON.ELF:exec" \
    --file "/bin/ip:$BUILD_DIR/IP.ELF:exec" \
    --file "/bin/mount:$BUILD_DIR/MOUNT.ELF:exec" \
    --file "/bin/umount:$BUILD_DIR/UMOUNT.ELF:exec" \
    --file "/bin/telnetd:$BUILD_DIR/TELNETD.ELF:exec" \
    --file "/bin/tftpd:$BUILD_DIR/TFTPD.ELF:exec" \
    --file "/bin/tftp:$BUILD_DIR/TFTP.ELF:exec" \
    --file "/bin/desktop:$BUILD_DIR/DESKTOP.ELF:exec" \
    --file "/bin/startlxde:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/openbox:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/pcmanfm:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/startlxqt:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/lxqt-panel:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/pcmanfm-qt:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/gnome-shell:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/mutter:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/gnome-session:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/plasma:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/kwin:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/startplasma:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/notepad:$BUILD_DIR/NOTEPAD.ELF:exec" \
    --file "/bin/taskmgr:$BUILD_DIR/TASKMGR.ELF:exec" \
    --file "/bin/webviewer:$BUILD_DIR/WEBVIEWER.ELF:exec" \
    --file "/bin/status_cgi:$BUILD_DIR/STATUS_CGI.ELF:exec" \
    --file "/bin/less:$BUILD_DIR/LESS.ELF:exec" \
    --file "/bin/more:$BUILD_DIR/MORE.ELF:exec" \
    --file "/bin/find:$BUILD_DIR/FIND.ELF:exec" \
    --file "/bin/sort:$BUILD_DIR/SORT.ELF:exec" \
    --file "/bin/uniq:$BUILD_DIR/UNIQ.ELF:exec" \
    --file "/bin/xargs:$BUILD_DIR/XARGS.ELF:exec" \
    --file "/bin/test:$BUILD_DIR/TEST.ELF:exec" \
    --file "/bin/[:$BUILD_DIR/TEST.ELF:exec" \
    --file "/bin/sshd:$BUILD_DIR/SSHD.ELF:exec" \
    --file "/bin/ssh:$BUILD_DIR/SSH.ELF:exec" \
    --file "/bin/sleep:$BUILD_DIR/SLEEP.ELF:exec" \
    --file "/bin/which:$BUILD_DIR/WHICH.ELF:exec" \
    --file "/bin/env:$BUILD_DIR/ENV.ELF:exec" \
    --file "/bin/cut:$BUILD_DIR/CUT.ELF:exec" \
    --file "/bin/tr:$BUILD_DIR/TR.ELF:exec" \
    --file "/bin/df:$BUILD_DIR/DF.ELF:exec" \
    --file "/bin/du:$BUILD_DIR/DU.ELF:exec" \
    --file "/bin/kill:$BUILD_DIR/KILL.ELF:exec" \
    --file "/bin/apropos:$BUILD_DIR/APROPOS.ELF:exec" \
    --file "/bin/aspell:$BUILD_DIR/ASPELL.ELF:exec" \
    --file "/bin/at:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/atq:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/atrm:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/batch:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/awk:$BUILD_DIR/AWK.ELF:exec" \
    --file "/bin/gawk:$BUILD_DIR/AWK.ELF:exec" \
    --file "/bin/basename:$BUILD_DIR/BASENAME.ELF:exec"     --file "/bin/dirname:$BUILD_DIR/DIRNAME.ELF:exec"     --file "/bin/dirs:$BUILD_DIR/DIRS.ELF:exec"     --file "/bin/dos2unix:$BUILD_DIR/DOS2UNIX.ELF:exec" \
    --file "/bin/dmesg:$BUILD_DIR/DMESG.ELF:exec" \
    --file "/bin/unix2dos:$BUILD_DIR/DOS2UNIX.ELF:exec" \
    --file "/bin/base32:$BUILD_DIR/BASE32.ELF:exec" \
    --file "/bin/base64:$BUILD_DIR/BASE64.ELF:exec" \
    --file "/bin/bc:$BUILD_DIR/BC.ELF:exec" \
    --file "/bin/bg:$BUILD_DIR/BG.ELF:exec" \
    --file "/bin/bind:$BUILD_DIR/BIND.ELF:exec" \
    --file "/bin/break:$BUILD_DIR/BREAK.ELF:exec" \
    --file "/bin/builtin:$BUILD_DIR/BUILTIN.ELF:exec" \
    --file "/bin/cal:$BUILD_DIR/CAL.ELF:exec" \
    --file "/bin/ncal:$BUILD_DIR/CAL.ELF:exec" \
    --file "/bin/caller:$BUILD_DIR/CALLER.ELF:exec" \
    --file "/bin/case:$BUILD_DIR/CASE.ELF:exec" \
    --file "/bin/cd:$BUILD_DIR/CD.ELF:exec" \
    --file "/bin/cfdisk:$BUILD_DIR/CFDISK.ELF:exec" \
    --file "/bin/chattr:$BUILD_DIR/CHATTR.ELF:exec" \
    --file "/bin/chgrp:$BUILD_DIR/CHGRP.ELF:exec"     --file "/bin/chpasswd:$BUILD_DIR/CHPASSWD.ELF:exec"     --file "/bin/chroot:$BUILD_DIR/CHROOT.ELF:exec"     --file "/bin/chkconfig:$BUILD_DIR/CHKCONFIG.ELF:exec"     --file "/bin/cksum:$BUILD_DIR/CKSUM.ELF:exec"     --file "/bin/hyprland:$BUILD_DIR/HYPRLAND.ELF:exec"     --file "/bin/hyprctl:$BUILD_DIR/HYPRCTL.ELF:exec"     --file "/usr/local/bin/hyprland:$BUILD_DIR/HYPRLAND.ELF:exec"     --file "/usr/local/bin/hyprctl:$BUILD_DIR/HYPRCTL.ELF:exec" \
    --file "/bin/sway:$BUILD_DIR/SWAY.ELF:exec" \
    --file "/usr/local/bin/sway:$BUILD_DIR/SWAY.ELF:exec" \
    --file "/bin/river:$BUILD_DIR/RIVER.ELF:exec" \
    --file "/usr/local/bin/river:$BUILD_DIR/RIVER.ELF:exec" \
    --file "/bin/niri:$BUILD_DIR/NIRI.ELF:exec" \
    --file "/usr/local/bin/niri:$BUILD_DIR/NIRI.ELF:exec" \
    --file "/bin/wayfire:$BUILD_DIR/WAYFIRE.ELF:exec" \
    --file "/usr/local/bin/wayfire:$BUILD_DIR/WAYFIRE.ELF:exec" \
    --file "/bin/labwc:$BUILD_DIR/LABWC.ELF:exec" \
    --file "/usr/local/bin/labwc:$BUILD_DIR/LABWC.ELF:exec" \
    --file "/bin/weston:$BUILD_DIR/WESTON.ELF:exec" \
    --file "/usr/local/bin/weston:$BUILD_DIR/WESTON.ELF:exec" \
    --file "/bin/qtile:$BUILD_DIR/QTILE.ELF:exec" \
    --file "/usr/local/bin/qtile:$BUILD_DIR/QTILE.ELF:exec" \
    --file "/bin/awesome:$BUILD_DIR/AWESOME.ELF:exec" \
    --file "/usr/local/bin/awesome:$BUILD_DIR/AWESOME.ELF:exec" \
    --file "/bin/xmonad:$BUILD_DIR/XMONAD.ELF:exec" \
    --file "/usr/local/bin/xmonad:$BUILD_DIR/XMONAD.ELF:exec" \
    --file "/bin/herbstluftwm:$BUILD_DIR/HERBSTLUFTWM.ELF:exec" \
    --file "/usr/local/bin/herbstluftwm:$BUILD_DIR/HERBSTLUFTWM.ELF:exec" \
    --file "/bin/spectrwm:$BUILD_DIR/SPECTRWM.ELF:exec" \
    --file "/usr/local/bin/spectrwm:$BUILD_DIR/SPECTRWM.ELF:exec" \
    --file "/bin/fluxbox:$BUILD_DIR/FLUXBOX.ELF:exec" \
    --file "/usr/local/bin/fluxbox:$BUILD_DIR/FLUXBOX.ELF:exec" \
    --file "/bin/icewm:$BUILD_DIR/ICEWM.ELF:exec" \
    --file "/usr/local/bin/icewm:$BUILD_DIR/ICEWM.ELF:exec" \
    --file "/bin/jwm:$BUILD_DIR/JWM.ELF:exec" \
    --file "/usr/local/bin/jwm:$BUILD_DIR/JWM.ELF:exec" \
    --file "/bin/wmaker:$BUILD_DIR/WMAKER.ELF:exec" \
    --file "/usr/local/bin/wmaker:$BUILD_DIR/WMAKER.ELF:exec" \
    --file "/bin/fvwm:$BUILD_DIR/FVWM.ELF:exec" \
    --file "/usr/local/bin/fvwm:$BUILD_DIR/FVWM.ELF:exec" \
    --file "/bin/budgie:$BUILD_DIR/BUDGIE.ELF:exec" \
    --file "/usr/local/bin/budgie:$BUILD_DIR/BUDGIE.ELF:exec" \
    --file "/bin/cosmic:$BUILD_DIR/COSMIC.ELF:exec" \
    --file "/usr/local/bin/cosmic:$BUILD_DIR/COSMIC.ELF:exec" \
    --file "/bin/deepin:$BUILD_DIR/DEEPIN.ELF:exec" \
    --file "/usr/local/bin/deepin:$BUILD_DIR/DEEPIN.ELF:exec" \
    --file "/bin/pantheon:$BUILD_DIR/PANTHEON.ELF:exec" \
    --file "/usr/local/bin/pantheon:$BUILD_DIR/PANTHEON.ELF:exec" \
    --file "/bin/ukui:$BUILD_DIR/UKUI.ELF:exec" \
    --file "/usr/local/bin/ukui:$BUILD_DIR/UKUI.ELF:exec" \
    --file "/bin/enlightenment:$BUILD_DIR/ENLIGHTENMENT.ELF:exec" \
    --file "/usr/local/bin/enlightenment:$BUILD_DIR/ENLIGHTENMENT.ELF:exec" \
    --file "/bin/bzip2:$BUILD_DIR/BZIP2.ELF:exec" \
    --file "/bin/bunzip2:$BUILD_DIR/BZIP2.ELF:exec" \
    --file "/bin/bzcat:$BUILD_DIR/BZIP2.ELF:exec" \
    --dir "/var/spool" \
    --dir "/var/spool/at" \
    --dir "/var/spool/at/spool" \
    --file "/bin/pkill:$BUILD_DIR/PKILL.ELF:exec" \
    --file "/bin/uptime:$BUILD_DIR/UPTIME.ELF:exec" \
    --file "/bin/free:$BUILD_DIR/FREE.ELF:exec"     --file "/bin/vmstat:$BUILD_DIR/VMSTAT.ELF:exec" \
    --file "/bin/watch:$BUILD_DIR/WATCH.ELF:exec" \
    --file "/bin/sed:$BUILD_DIR/SED.ELF:exec" \
    --file "/bin/diff:$BUILD_DIR/DIFF.ELF:exec"     --file "/bin/diff3:$BUILD_DIR/DIFF3.ELF:exec" \
    --file "/bin/cmp:$BUILD_DIR/CMP.ELF:exec" \
    --file "/bin/patch:$BUILD_DIR/PATCH.ELF:exec" \
    --file "/bin/tar:$BUILD_DIR/TAR.ELF:exec"     --file "/bin/cpio:$BUILD_DIR/CPIO.ELF:exec"     --file "/bin/cron:$BUILD_DIR/CRON.ELF:exec"     --file "/bin/csplit:$BUILD_DIR/CSPLIT.ELF:exec"     --file "/bin/date:$BUILD_DIR/DATE.ELF:exec"     --file "/bin/declare:$BUILD_DIR/DECLARE.ELF:exec"     --file "/bin/typeset:$BUILD_DIR/DECLARE.ELF:exec"     --file "/bin/dc:$BUILD_DIR/DC.ELF:exec"     --file "/bin/dd:$BUILD_DIR/DD.ELF:exec"     --file "/bin/ddrescue:$BUILD_DIR/DDRESCUE.ELF:exec"     --file "/bin/ddrescuelog:$BUILD_DIR/DDRESCUELOG.ELF:exec"     --file "/bin/lsusb:$BUILD_DIR/LSUSB.ELF:exec"     --file "/bin/crond:$BUILD_DIR/CROND.ELF:exec"     --file "/bin/crontab:$BUILD_DIR/CRONTAB.ELF:exec" \
    --file "/bin/gzip:$BUILD_DIR/GZIP.ELF:exec" \
    --file "/bin/gunzip:$BUILD_DIR/GUNZIP.ELF:exec" \
    --file "/bin/route:$BUILD_DIR/ROUTE.ELF:exec" \
    --file "/bin/traceroute:$BUILD_DIR/TRACEROUTE.ELF:exec" \
    --file "/bin/dig:$BUILD_DIR/DIG.ELF:exec" \
    --file "/bin/arp:$BUILD_DIR/ARP.ELF:exec" \
    --file "/bin/lscpu:$BUILD_DIR/LSCPU.ELF:exec" \
    --file "/bin/lspci:$BUILD_DIR/LSPCI.ELF:exec" \
    --file "/bin/gpuinfo:$BUILD_DIR/GPUINFO.ELF:exec" \
    --file "/bin/sndinfo:$BUILD_DIR/SNDINFO.ELF:exec" \
    --file "/bin/acpi:$BUILD_DIR/ACPI.ELF:exec" \
    --file "/bin/xsdisplay:$BUILD_DIR/XSDISPLAY.ELF:exec" \
    --file "/bin/dbus-daemon:$BUILD_DIR/DBUS_DAEMON.ELF:exec" \
    --file "/bin/dltest:$BUILD_DIR/DLTEST.ELF:exec" \
    --file "/bin/display-protocols:$BUILD_DIR/DISPLAY_PROTOCOLS.ELF:exec" \
    --file "/bin/x11-server:$BUILD_DIR/DISPLAY_PROTOCOLS.ELF:exec" \
    --file "/bin/wayland-compositor:$BUILD_DIR/DISPLAY_PROTOCOLS.ELF:exec" \
    --file "/bin/xserver:$BUILD_DIR/XSERVER.ELF:exec" \
    --file "/bin/wayland-bridge:$BUILD_DIR/WAYLAND_BRIDGE.ELF:exec" \
    --file "/bin/x11-client:$BUILD_DIR/X11_CLIENT.ELF:exec" \
    --file "/bin/x11-wm-client:$BUILD_DIR/X11_WM_CLIENT.ELF:exec" \
    --file "/bin/cairo-upstream-test:$BUILD_DIR/CAIRO_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/fontconfig-upstream-test:$BUILD_DIR/FONTCONFIG_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/freetype-upstream-test:$BUILD_DIR/FREETYPE_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/fribidi-upstream-test:$BUILD_DIR/FRIBIDI_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/glib-upstream-test:$BUILD_DIR/GLIB_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/graphene-upstream-test:$BUILD_DIR/GRAPHENE_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/gtk-upstream-test:$BUILD_DIR/GTK_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/gtk3-upstream-test:$BUILD_DIR/GTK3_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/gtk3-x11-upstream-test:$BUILD_DIR/GTK3_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/harfbuzz-upstream-test:$BUILD_DIR/HARFBUZZ_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/pango-upstream-test:$BUILD_DIR/PANGO_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/wayland-upstream-test:$BUILD_DIR/WAYLAND_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/xkbcommon-upstream-test:$BUILD_DIR/XKBCOMMON_UPSTREAM_TEST.ELF:exec" \
    --file "/bin/qtwayland-smoke:$BUILD_DIR/QT_QUICK_WAYLAND_SMOKE.ELF:exec" \
    --file "/bin/qtquick-wayland-smoke:$BUILD_DIR/QT_QUICK_WAYLAND_SMOKE.ELF:exec" \
    --file "/bin/dbus-update-activation-environment:$BUILD_DIR/DBUS_DAEMON.ELF:exec" \
    --file "/bin/lxqt-leave:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/lxqt-session:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/etc/test_all_181_commands.sh:$ROOT_DIR/userspace/etc/test_all_181_commands.sh:0755" \
    --dir "/lib" \
    --file "/lib/ld-akaos.so:$BUILD_DIR/LD_AKAOS.ELF:exec" \
    --file "/lib/libc.so.6:$BUILD_DIR/libc.so.6:0444" \
    --file "/lib/libm.so.6:$BUILD_DIR/libm.so.6:0444" \
    --file "/lib/libc.so:$BUILD_DIR/libc.so.6:0444" \
    --file "/lib/libc++.so.1:$BUILD_DIR/libc++.so.1:0444" \
    --file "/lib/libc++.so:$BUILD_DIR/libc++.so.1:0444" \
    --file "/lib/libc++abi.so.1:$BUILD_DIR/libc++abi.so.1:0444" \
    --file "/lib/libc++abi.so:$BUILD_DIR/libc++abi.so.1:0444" \
    --file "/lib/libunwind.so.1:$BUILD_DIR/libunwind.so.1:0444" \
    --file "/lib/libunwind.so:$BUILD_DIR/libunwind.so.1:0444" \
    --file "/lib/libakaos-cxx.so:$BUILD_DIR/libakaos-cxx.so:0444" \
    --file "/lib/libakaos-demo.so:$BUILD_DIR/libakaos-demo.so:0444" \
    --file "/lib/libakaos-static.so:$BUILD_DIR/libakaos-static.so:0444" \
    --file "/lib/libakaos-versioned.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-dependent.so:$BUILD_DIR/libakaos-dependent.so:0444" \
    --file "/lib/libakaos-loader-fanout.so:$BUILD_DIR/libakaos-loader-fanout.so:0444" \
    --file "/lib/libakaos-loader-dep-0.so:$BUILD_DIR/libakaos-loader-dep-0.so:0444" \
    --file "/lib/libakaos-loader-dep-1.so:$BUILD_DIR/libakaos-loader-dep-1.so:0444" \
    --file "/lib/libakaos-loader-dep-2.so:$BUILD_DIR/libakaos-loader-dep-2.so:0444" \
    --file "/lib/libakaos-loader-dep-3.so:$BUILD_DIR/libakaos-loader-dep-3.so:0444" \
    --file "/lib/libakaos-loader-dep-4.so:$BUILD_DIR/libakaos-loader-dep-4.so:0444" \
    --file "/lib/libakaos-capacity-00.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-01.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-02.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-03.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-04.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-05.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-06.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-07.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-08.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-09.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-10.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-11.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libpthread.so:$BUILD_DIR/libpthread.so:0444" \
    --file "/lib/libpthread.so.0:$BUILD_DIR/libpthread.so:0444" \
    --file "/lib/libglib.so:$BUILD_DIR/libglib.so:0444" \
    --file "/lib/libglib-2.0.so.0:$BUILD_DIR/libglib.so:0444" \
    --file "/lib/libgobject-2.0.so.0:$BUILD_DIR/libglib.so:0444" \
    --file "/lib/libgio.so:$BUILD_DIR/libgio.so:0444" \
    --file "/lib/libgio-2.0.so.0:$BUILD_DIR/libgio.so:0444" \
    --file "/lib/libgtk.so:$BUILD_DIR/libgtk.so:0444" \
    --file "/lib/libgtk-3.so.0:$BUILD_DIR/libgtk.so:0444" \
    --file "/lib/libgdk-3.so.0:$BUILD_DIR/libgtk.so:0444" \
    --file "/lib/libgtk-4.so:$BUILD_DIR/libgtk-4.so:0444" \
    --file "/lib/libgtk-4.so.1:$BUILD_DIR/libgtk-4.so:0444" \
    --file "/lib/libgdk-4.so.1:$BUILD_DIR/libgtk-4.so:0444" \
    --file "/lib/libQt6Core.so:$BUILD_DIR/libQt6Core.so:0444" \
    --file "/lib/libQt6Core.so.6:$BUILD_DIR/libQt6Core.so:0444" \
    --file "/lib/libQt6Gui.so:$BUILD_DIR/libQt6Gui.so:0444" \
    --file "/lib/libQt6Gui.so.6:$BUILD_DIR/libQt6Gui.so:0444" \
    --file "/lib/libQt6Widgets.so:$BUILD_DIR/libQt6Widgets.so:0444" \
    --file "/lib/libQt6Widgets.so.6:$BUILD_DIR/libQt6Widgets.so:0444" \
    --file "/etc/passwd:$ROOT_DIR/userspace/etc/passwd:0644" \
    --file "/etc/group:$ROOT_DIR/userspace/etc/group:0644" \
    --file "/etc/sudoers:$ROOT_DIR/userspace/etc/sudoers:0440" \
    --file "/etc/shadow:$ROOT_DIR/userspace/etc/shadow:0600" \
    --file "/etc/akaos.conf:$ROOT_DIR/userspace/etc/akaos.conf:0644" \
    --file "/etc/pkg.conf:$ROOT_DIR/userspace/etc/pkg.conf:0644" \
    --file "/etc/dbus-services.conf:$ROOT_DIR/userspace/etc/dbus-services.conf:0644" \
    --file "/etc/resolv.conf:$ROOT_DIR/userspace/etc/resolv.conf:0644" \
    --file "/etc/fstab:$ROOT_DIR/userspace/etc/fstab:0644" \
    --file "/etc/aurora-apps:$ROOT_DIR/userspace/etc/aurora-apps:0644" \
    --dir "/var" \
    --dir "/var/lib" \
    --dir "/var/lib/pkg" \
    --dir "/var/cache" \
    --dir "/var/cache/pkg" \
    --dir "/var/www" \
    --dir "/var/www/cgi-bin" \
    --file "/var/www/cgi-bin/status:$BUILD_DIR/STATUS_CGI.ELF:exec" \
    --file "/var/www/index.html:$ROOT_DIR/userspace/var/www/index.html:0644" \
    --data "/etc/motd:akaOS XSCore initramfs\n"

(cd "$ROOT_DIR/build" && llvm-objcopy -I binary -O elf32-littlearm -B arm \
    initramfs_arm32.img initramfs_arm32_archive.o)

echo "[DONE] Built $ROOT_DIR/build/initramfs_arm32.img"
