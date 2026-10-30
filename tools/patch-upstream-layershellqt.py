#!/usr/bin/env python3
"""Enable the explicit Qt Wayland private target needed by akaOS's cross build."""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch-upstream-layershellqt.py SOURCE_DIR", file=sys.stderr)
        return 2

    cmake_file = Path(sys.argv[1]) / "CMakeLists.txt"
    content = cmake_file.read_text(encoding="utf-8")
    if 'project(layershellqt)' not in content or 'set(PROJECT_VERSION "6.0.0")' not in content:
        print(f"unexpected LayerShellQt source: {cmake_file}", file=sys.stderr)
        return 1

    private_find = 'find_package(Qt6WaylandClientPrivate ${QT_MIN_VERSION} REQUIRED NO_MODULE)\n'
    if private_find not in content:
        anchor = 'find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS WaylandClient Qml)\n'
        if content.count(anchor) != 1:
            print("could not find the pinned LayerShellQt Qt package declaration", file=sys.stderr)
            return 1
        content = content.replace(anchor, anchor + private_find, 1)
        cmake_file.write_text(content, encoding="utf-8")

    source_file = Path(sys.argv[1]) / "src" / "qwaylandlayersurface.cpp"
    source = source_file.read_text(encoding="utf-8")
    old_call = "    window()->sendRecursiveExposeEvent();\n"
    new_call = "    window()->updateExposure();\n"
    if new_call not in source:
        if source.count(old_call) != 1:
            print("LayerShellQt expose call changed; refusing an unsafe patch", file=sys.stderr)
            return 1
        source_file.write_text(source.replace(old_call, new_call, 1),
                               encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
