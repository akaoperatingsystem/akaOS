cmake_minimum_required(VERSION 3.22)

project(akaos_qt_wayland_smoke LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets)

if(NOT DEFINED AKAOS_DYNAMIC_CRT_OBJECT OR
   NOT EXISTS "${AKAOS_DYNAMIC_CRT_OBJECT}")
    message(FATAL_ERROR "AKAOS_DYNAMIC_CRT_OBJECT must name akaOS's dynamic CRT object")
endif()
if(NOT DEFINED AKAOS_PROJECT_ROOT OR
   NOT EXISTS "${AKAOS_PROJECT_ROOT}/userspace/linker/user-dynamic.ld")
    message(FATAL_ERROR "AKAOS_PROJECT_ROOT must name the akaOS source/build root")
endif()

add_executable(qtwayland-smoke main.cpp)
target_compile_features(qtwayland-smoke PRIVATE cxx_std_17)
target_sources(qtwayland-smoke PRIVATE "${AKAOS_DYNAMIC_CRT_OBJECT}")
target_link_libraries(qtwayland-smoke PRIVATE Qt6::Widgets)
target_link_options(qtwayland-smoke PRIVATE
    -pie
    -Wl,--eh-frame-hdr
    -Wl,-z,max-page-size=0x1000
    -Wl,--dynamic-linker=/lib/ld-akaos.so
    "-Wl,-T,${AKAOS_PROJECT_ROOT}/userspace/linker/user-dynamic.ld"
)
set_property(TARGET qtwayland-smoke APPEND PROPERTY LINK_DEPENDS
    "${AKAOS_PROJECT_ROOT}/userspace/linker/user-dynamic.ld")
set_target_properties(qtwayland-smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output"
)
