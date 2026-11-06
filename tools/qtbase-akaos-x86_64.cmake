if(NOT DEFINED AKAOS_PROJECT_ROOT)
    get_filename_component(AKAOS_PROJECT_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    if(NOT EXISTS "${AKAOS_PROJECT_ROOT}/sdk/include")
        get_filename_component(AKAOS_PROJECT_ROOT
            "${CMAKE_CURRENT_LIST_DIR}/../akaos-target" ABSOLUTE)
    endif()
endif()
set(akaos_root "${AKAOS_PROJECT_ROOT}")
set(akaos_build "${akaos_root}/build")
set(akaos_cxx_runtime "${akaos_build}/cxx-runtime/llvm-21.1.8/x86_64")
set(akaos_graphics_sysroot "${akaos_build}/upstream-sysroot/graphics-x86_64")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(AKAOS_TARGET ON)
set(QT_QPA_DEFAULT_PLATFORM wayland CACHE STRING
    "Default Qt platform backend for akaOS" FORCE)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    AKAOS_PROJECT_ROOT AKAOS_WAYLAND_SCANNER AKAOS_QTWAYLANDSCANNER)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-none-elf)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-none-elf)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_STRIP llvm-strip)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The akaOS libc implements poll(2), but the cross-build test cannot execute
# target code while configuring Qt. Tell Qt's feature system about the known
# target API instead of selecting its emulated poll fallback.
set(TEST_poll ON CACHE INTERNAL "akaOS libc implements poll(2)" FORCE)
set(FEATURE_poll_poll ON CACHE BOOL "Use akaOS native poll(2)" FORCE)

set(akaos_qtwayland_cmake_helper
    "${CMAKE_CURRENT_LIST_DIR}/qtwayland-host-scanner-target.cmake")
if(EXISTS "${akaos_qtwayland_cmake_helper}")
    set(CMAKE_PROJECT_INCLUDE "${akaos_qtwayland_cmake_helper}"
        CACHE FILEPATH "akaOS Qt Wayland host/target helper" FORCE)
    set(CMAKE_PROJECT_INCLUDE_BEFORE "${akaos_qtwayland_cmake_helper}"
        CACHE FILEPATH "akaOS Qt Wayland host/target helper" FORCE)
endif()

if(NOT DEFINED AKAOS_WAYLAND_SCANNER)
    get_filename_component(akaos_wayland_host_prefix
        "${CMAKE_CURRENT_LIST_DIR}/../wayland-host" ABSOLUTE)
    if(EXISTS "${akaos_wayland_host_prefix}/bin/wayland-scanner")
        set(AKAOS_WAYLAND_SCANNER
            "${akaos_wayland_host_prefix}/bin/wayland-scanner"
            CACHE FILEPATH "Host Wayland protocol scanner" FORCE)
    endif()
endif()

if(DEFINED QT_HOST_PATH AND
   EXISTS "${QT_HOST_PATH}/libexec/qtwaylandscanner")
    set(AKAOS_QTWAYLANDSCANNER
        "${QT_HOST_PATH}/libexec/qtwaylandscanner"
        CACHE FILEPATH "Host Qt Wayland scanner" FORCE)
endif()

set(akaos_target_flags
    "-ffreestanding -fno-builtin -fno-stack-protector -fPIC -D__AKAOS__ -D__linux__ -D_POSIX_THREAD_ATTR_STACKSIZE=200809L -I${akaos_root}/sdk/include -I${akaos_root}/userspace/libc/include -isystem ${akaos_graphics_sysroot}/usr/local/include")
set(CMAKE_C_FLAGS_INIT
    "-nostdinc -idirafter /usr/lib/llvm-21/lib/clang/21/include ${akaos_target_flags}")
set(akaos_cxx_flags
    "-nostdinc -nostdinc++ -I${akaos_cxx_runtime}/include/c++/v1 -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE -idirafter /usr/lib/llvm-21/lib/clang/21/include ${akaos_target_flags} -fhosted -fexceptions -frtti")
set(CMAKE_CXX_FLAGS_INIT "${akaos_cxx_flags}")
set(CMAKE_CXX_FLAGS "${akaos_cxx_flags}" CACHE STRING
    "akaOS C++ compiler flags" FORCE)

set(akaos_link_flags
    "-nostdlib -fuse-ld=lld -Wl,--no-as-needed -Wl,--dynamic-linker=/lib/ld-akaos.so -L${akaos_graphics_sysroot}/usr/lib -L${akaos_graphics_sysroot}/usr/local/lib -L${akaos_build} -L${akaos_cxx_runtime} ${akaos_build}/libc++.so.1 ${akaos_build}/libc++abi.so.1 ${akaos_build}/libunwind.so.1 ${akaos_build}/libm.so.6 ${akaos_build}/libc.so.6 ${akaos_cxx_runtime}/libakaos-compiler-builtins.a")
set(akaos_executable_link_flags
    "${akaos_link_flags} ${akaos_build}/user-dynamic-crt0.o")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${akaos_executable_link_flags}")
set(CMAKE_EXE_LINKER_FLAGS "${akaos_executable_link_flags}" CACHE STRING
    "akaOS executable linker flags" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${akaos_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${akaos_link_flags}")

set(CMAKE_FIND_ROOT_PATH
    "${akaos_graphics_sysroot}"
    "${akaos_build}"
    "${akaos_cxx_runtime}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
