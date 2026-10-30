#!/usr/bin/env python3
from pathlib import Path
import sys


qtwayland_source = Path(sys.argv[1])
cmake_path = qtwayland_source / "src" / "CMakeLists.txt"
cmake_source = cmake_path.read_text(encoding="utf-8")

original_server_gate = """if (QT_FEATURE_wayland_server)
    add_subdirectory(compositor)
endif()

if (QT_FEATURE_wayland_client)
    add_subdirectory(client)
endif()

if (QT_FEATURE_wayland_server OR QT_FEATURE_wayland_client)
"""
akaos_server_gate = """set(akaos_build_wayland_server ${QT_FEATURE_wayland_server})
if (AKAOS_TARGET)
    # akaOS supplies its own Wayland compositor; QtWayland is built as a client.
    set(akaos_build_wayland_server OFF)
endif()

if (akaos_build_wayland_server)
    add_subdirectory(compositor)
endif()

if (QT_FEATURE_wayland_client)
    add_subdirectory(client)
endif()

if (akaos_build_wayland_server OR QT_FEATURE_wayland_client)
"""

if akaos_server_gate not in cmake_source:
    if cmake_source.count(original_server_gate) != 1:
        raise SystemExit("QtWayland 6.12 compositor-selection patch target changed")
    cmake_source = cmake_source.replace(
        original_server_gate, akaos_server_gate, 1
    )
    cmake_path.write_text(cmake_source, encoding="utf-8")
