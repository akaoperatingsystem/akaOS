if(DEFINED AKAOS_WAYLAND_SCANNER AND EXISTS "${AKAOS_WAYLAND_SCANNER}" AND
   NOT TARGET Wayland::Scanner)
    add_executable(Wayland::Scanner IMPORTED GLOBAL)
    set_target_properties(Wayland::Scanner PROPERTIES
        IMPORTED_LOCATION "${AKAOS_WAYLAND_SCANNER}"
    )
endif()

if(DEFINED AKAOS_QTWAYLANDSCANNER AND
   EXISTS "${AKAOS_QTWAYLANDSCANNER}" AND
   NOT TARGET Qt6::qtwaylandscanner)
    add_executable(Qt6::qtwaylandscanner IMPORTED GLOBAL)
    set_target_properties(Qt6::qtwaylandscanner PROPERTIES
        IMPORTED_LOCATION "${AKAOS_QTWAYLANDSCANNER}"
    )
endif()

if(DEFINED AKAOS_PROJECT_ROOT)
    set(_akaos_wayland_sysroot
        "${AKAOS_PROJECT_ROOT}/build/upstream-sysroot/graphics-x86_64")
    set(_akaos_wayland_include
        "${_akaos_wayland_sysroot}/usr/local/include")

    if(NOT TARGET Wayland::Client AND
       EXISTS "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-client.so")
        add_library(Wayland::Client UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Wayland::Client PROPERTIES
            IMPORTED_LOCATION
                "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-client.so"
            INTERFACE_INCLUDE_DIRECTORIES "${_akaos_wayland_include}"
            INTERFACE_LINK_LIBRARIES m
        )
    endif()

    if(NOT TARGET Wayland::Cursor AND
       EXISTS "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-cursor.so")
        add_library(Wayland::Cursor UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Wayland::Cursor PROPERTIES
            IMPORTED_LOCATION
                "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-cursor.so"
            INTERFACE_INCLUDE_DIRECTORIES "${_akaos_wayland_include}"
            INTERFACE_LINK_LIBRARIES Wayland::Client
        )
    endif()

    if(NOT TARGET Wayland::Egl AND
       EXISTS "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-egl.so")
        add_library(Wayland::Egl UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Wayland::Egl PROPERTIES
            IMPORTED_LOCATION
                "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-egl.so"
            INTERFACE_INCLUDE_DIRECTORIES "${_akaos_wayland_include}"
            INTERFACE_LINK_LIBRARIES Wayland::Client
        )
    endif()

    if(NOT TARGET Wayland::Server AND
       EXISTS "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-server.so")
        add_library(Wayland::Server UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Wayland::Server PROPERTIES
            IMPORTED_LOCATION
                "${_akaos_wayland_sysroot}/usr/local/lib/libwayland-server.so"
            INTERFACE_INCLUDE_DIRECTORIES "${_akaos_wayland_include}"
            INTERFACE_LINK_LIBRARIES "m;ffi"
        )
    endif()
endif()
