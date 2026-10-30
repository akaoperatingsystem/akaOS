#!/usr/bin/env python3
"""Select LXQt's client/UI libraries for an akaOS Wayland-only build."""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch-upstream-lxqt-globalkeys-wayland.py SOURCE_DIR", file=sys.stderr)
        return 2

    source_dir = Path(sys.argv[1])
    path = source_dir / "CMakeLists.txt"
    content = path.read_text(encoding="utf-8")
    if "project(lxqt-globalkeys)" not in content or "set(LXQT_GLOBALKEYS_MAJOR_VERSION 2)" not in content:
        raise ValueError(f"unexpected lxqt-globalkeys source: {path}")

    option = 'option(AKAOS_WAYLAND_ONLY "Build the akaOS Wayland client libraries without the X11 daemon" OFF)\n'
    if option not in content:
        marker = 'option(UPDATE_TRANSLATIONS "Update source translation translations/*.ts files" OFF)\n'
        if content.count(marker) != 1:
            raise ValueError("LXQt globalkeys option insertion point changed")
        content = content.replace(marker, marker + option, 1)

    original = (
        "add_subdirectory(daemon)\n"
        "add_subdirectory(config)\n"
        "add_subdirectory(client)\n"
        "add_subdirectory(ui)\n"
    )
    patched = (
        "if (AKAOS_WAYLAND_ONLY)\n"
        "    add_subdirectory(client)\n"
        "    add_subdirectory(ui)\n"
        "else()\n"
        "    add_subdirectory(daemon)\n"
        "    add_subdirectory(config)\n"
        "    add_subdirectory(client)\n"
        "    add_subdirectory(ui)\n"
        "endif()\n"
    )
    if patched not in content:
        if content.count(original) != 1:
            raise ValueError("LXQt globalkeys target layout changed")
        content = content.replace(original, patched, 1)

    original_autostart = (
        "add_subdirectory(autostart)\n"
        "add_subdirectory(xdg)\n"
    )
    patched_autostart = (
        "if (NOT AKAOS_WAYLAND_ONLY)\n"
        "    add_subdirectory(autostart)\n"
        "    add_subdirectory(xdg)\n"
        "endif()\n"
    )
    if patched_autostart not in content:
        if content.count(original_autostart) != 1:
            raise ValueError("LXQt globalkeys autostart layout changed")
        content = content.replace(original_autostart, patched_autostart, 1)

    path.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
