#!/usr/bin/env python3
"""Apply akaOS Wayland-only guards to the pinned upstream LXQt panel."""

import sys
from pathlib import Path


def replace_once(content: str, old: str, new: str, label: str) -> str:
    if new in content and old not in content:
        return content
    if content.count(old) != 1:
        raise ValueError(f"unexpected {label} source layout")
    return content.replace(old, new, 1)


def patch_cmake(source_dir: Path) -> None:
    path = source_dir / "CMakeLists.txt"
    content = path.read_text(encoding="utf-8")
    if "project(lxqt-panel)" not in content:
        raise ValueError(f"unexpected LXQt panel source: {path}")

    option = (
        'option(AKAOS_WAYLAND_ONLY "Build the panel without X11 backends" OFF)\n'
        'if (AKAOS_WAYLAND_ONLY)\n'
        '    add_definitions(-DAKAOS_WAYLAND_ONLY)\n'
        'endif()\n'
    )
    if option not in content:
        content = replace_once(
            content,
            "project(lxqt-panel)\n",
            "project(lxqt-panel)\n\n" + option,
            "Wayland-only option",
        )
    path.write_text(content, encoding="utf-8")

    path = source_dir / "panel" / "backends" / "CMakeLists.txt"
    content = path.read_text(encoding="utf-8")
    content = replace_once(
        content,
        "add_subdirectory(wayland)\nadd_subdirectory(xcb)\n",
        "add_subdirectory(wayland)\n"
        "if (NOT AKAOS_WAYLAND_ONLY)\n"
        "    add_subdirectory(xcb)\n"
        "endif()\n",
        "XCB backend selection",
    )
    path.write_text(content, encoding="utf-8")


def patch_panel_source(source_dir: Path) -> None:
    path = source_dir / "panel" / "lxqtpanel.cpp"
    content = path.read_text(encoding="utf-8")
    content = replace_once(
        content,
        "#include <KWindowSystem>\n#include <KX11Extras>\n#include <NETWM>\n",
        "#include <KWindowSystem>\n"
        "#ifndef AKAOS_WAYLAND_ONLY\n"
        "#include <KX11Extras>\n#include <NETWM>\n"
        "#endif\n",
        "panel X11 headers",
    )
    content = replace_once(
        content,
        "    if(qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) //TODO: cache in bool isPlatformX11\n"
        "        KX11Extras::setOnDesktop(effectiveWinId(), NET::OnAllDesktops);\n",
        "#ifndef AKAOS_WAYLAND_ONLY\n"
        "    if(qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) //TODO: cache in bool isPlatformX11\n"
        "        KX11Extras::setOnDesktop(effectiveWinId(), NET::OnAllDesktops);\n"
        "#endif\n",
        "panel show X11 desktop placement",
    )

    start = content.find("void LXQtPanel::updateWmStrut()")
    end = content.find("\nbool LXQtPanel::canPlacedOn", start)
    if start < 0 or end < 0:
        raise ValueError("LXQt panel strut function boundary changed")
    strut = content[start:end]
    if "#ifndef AKAOS_WAYLAND_ONLY" not in strut:
        strut = replace_once(
            strut,
            "    if(qGuiApp->nativeInterface<QNativeInterface::QX11Application>())\n    {\n",
            "#ifndef AKAOS_WAYLAND_ONLY\n"
            "    if(qGuiApp->nativeInterface<QNativeInterface::QX11Application>())\n    {\n",
            "panel X11 strut branch",
        )
        strut = replace_once(
            strut,
            "    }\n    else if(mLayerWindow && qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())\n",
            "    }\n#endif\n"
            "    if(mLayerWindow && qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())\n",
            "panel Wayland layer-shell branch",
        )
        content = content[:start] + strut + content[end:]

    dialog_calls = (
        "    WId wid = mConfigDialog->windowHandle()->winId();\n\n"
        "    KX11Extras::activateWindow(wid);\n"
        "    KX11Extras::setOnDesktop(wid, KX11Extras::currentDesktop());\n"
    )
    guarded_dialog_calls = (
        "#ifndef AKAOS_WAYLAND_ONLY\n"
        "    WId wid = mConfigDialog->windowHandle()->winId();\n\n"
        "    KX11Extras::activateWindow(wid);\n"
        "    KX11Extras::setOnDesktop(wid, KX11Extras::currentDesktop());\n"
        "#endif\n"
    )
    if guarded_dialog_calls not in content:
        if content.count(dialog_calls) != 2:
            raise ValueError("LXQt panel config-dialog X11 calls changed")
        content = content.replace(dialog_calls, guarded_dialog_calls)

    event_start = content.find("bool LXQtPanel::event(QEvent *event)")
    event_end = content.find("\nvoid LXQtPanel::", event_start + 1)
    if event_start < 0 or event_end < 0:
        raise ValueError("LXQt panel event function boundary changed")
    event = content[event_start:event_end]
    if "#ifndef AKAOS_WAYLAND_ONLY" not in event:
        event = replace_once(
            event,
            "    case QEvent::WinIdChange:\n    {\n",
            "    case QEvent::WinIdChange:\n    {\n"
            "#ifndef AKAOS_WAYLAND_ONLY\n",
            "panel X11 window-id handler",
        )
        event = replace_once(
            event,
            "        break;\n    }\n    case QEvent::DragEnter:",
            "#endif\n        break;\n    }\n    case QEvent::DragEnter:",
            "panel X11 event guard end",
        )
        content = content[:event_start] + event + content[event_end:]
    path.write_text(content, encoding="utf-8")

    path = source_dir / "panel" / "plugin.cpp"
    content = path.read_text(encoding="utf-8")
    content = replace_once(
        content,
        "#include <KX11Extras>\n",
        "#ifndef AKAOS_WAYLAND_ONLY\n#include <KX11Extras>\n#endif\n",
        "plugin X11 header",
    )
    plugin_calls = (
        "    WId wid = mConfigDialog->windowHandle()->winId();\n"
        "    KX11Extras::activateWindow(wid);\n"
        "    KX11Extras::setOnDesktop(wid, KX11Extras::currentDesktop());\n"
    )
    guarded_plugin_calls = (
        "#ifndef AKAOS_WAYLAND_ONLY\n"
        "    WId wid = mConfigDialog->windowHandle()->winId();\n"
        "    KX11Extras::activateWindow(wid);\n"
        "    KX11Extras::setOnDesktop(wid, KX11Extras::currentDesktop());\n"
        "#endif\n"
    )
    content = replace_once(content, plugin_calls, guarded_plugin_calls, "plugin config-dialog X11 calls")
    path.write_text(content, encoding="utf-8")


def patch_panel_defaults(source_dir: Path) -> None:
    path = source_dir / "panel" / "resources" / "panel.conf"
    content = path.read_text(encoding="utf-8")
    content = replace_once(
        content,
        "plugins=fancymenu,desktopswitch,quicklaunch,taskbar,statusnotifier,tray,mount,volume,worldclock,showdesktop\n",
        "plugins=fancymenu,quicklaunch,taskbar,statusnotifier,worldclock,showdesktop\n",
        "default panel plugin list",
    )
    for unsupported_section in (
        "[desktopswitch]\ntype=desktopswitch\n\n",
        "[mount]\ntype=mount\n\n",
        "[volume]\ndevice=0\ntype=volume\n\n",
        "[tray]\ntype=tray\n",
    ):
        content = content.replace(unsupported_section, "")
    path.write_text(content, encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch-upstream-lxqt-panel-wayland.py SOURCE_DIR", file=sys.stderr)
        return 2
    source_dir = Path(sys.argv[1])
    patch_cmake(source_dir)
    patch_panel_source(source_dir)
    patch_panel_defaults(source_dir)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
