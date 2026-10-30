#!/usr/bin/env python3
"""Apply akaOS's Wayland-only build toggle to the pinned liblxqt CMake file."""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch-upstream-liblxqt-wayland-only.py SOURCE_DIR", file=sys.stderr)
        return 2

    cmake_file = Path(sys.argv[1]) / "CMakeLists.txt"
    content = cmake_file.read_text(encoding="utf-8")
    if "project(liblxqt)" not in content or 'set(QT_MINIMUM_VERSION "6.6.0")' not in content:
        print(f"unexpected liblxqt source: {cmake_file}", file=sys.stderr)
        return 1
    if "option(AKAOS_WAYLAND_ONLY" not in content:
        build_option = 'option(BUILD_BACKLIGHT_LINUX_BACKEND "Build the Linux backend for the backlight" ON)\n'
        if content.count(build_option) != 1:
            print("could not find the pinned liblxqt build options", file=sys.stderr)
            return 1
        if content.count("\nif (NOT APPLE)\n") != 6 or \
           content.count("\nendif (NOT APPLE)\n") != 5:
            print("liblxqt X11 source layout changed; refusing an unsafe patch", file=sys.stderr)
            return 1

        content = content.replace(
            build_option,
            build_option
            + 'option(AKAOS_WAYLAND_ONLY "Build without liblxqt X11 integration" OFF)\n',
            1,
        )
        content = content.replace(
            "\nif (NOT APPLE)\n",
            "\nif (NOT APPLE AND NOT AKAOS_WAYLAND_ONLY)\n",
        )
        content = content.replace(
            "\nendif (NOT APPLE)\n",
            "\nendif (NOT APPLE AND NOT AKAOS_WAYLAND_ONLY)\n",
        )
        cmake_file.write_text(content, encoding="utf-8")

    cmake_text = cmake_file.read_text(encoding="utf-8")
    option_line = 'option(AKAOS_WAYLAND_ONLY "Build without liblxqt X11 integration" OFF)\n'
    compile_definition = (
        'if (AKAOS_WAYLAND_ONLY)\n'
        '    add_definitions(-DAKAOS_WAYLAND_ONLY)\n'
        'endif()\n'
    )
    if compile_definition not in cmake_text:
        option_start = cmake_text.find("option(AKAOS_WAYLAND_ONLY ")
        if option_start < 0:
            print("could not place the akaOS Wayland compile definition", file=sys.stderr)
            return 1
        option_end = cmake_text.find("\n", option_start)
        if option_end < 0:
            print("unterminated AKAOS_WAYLAND_ONLY option", file=sys.stderr)
            return 1
        cmake_text = (cmake_text[:option_end + 1] + compile_definition +
                      cmake_text[option_end + 1:])

    for variable, member in (
        ("PUB_HDRS", "lxqtscreensaver.h"),
        ("PUBLIC_CLASSES", "ScreenSaver"),
        ("SRCS", "lxqtscreensaver.cpp"),
        ("MOCS", "lxqtscreensaver.h"),
    ):
        gated = (
            "if (NOT APPLE AND NOT AKAOS_WAYLAND_ONLY)\n"
            f"    list (APPEND {variable}\n"
            f"        {member}\n"
            "    )\n"
            "endif (NOT APPLE AND NOT AKAOS_WAYLAND_ONLY)\n"
        )
        restored = (
            "if (NOT APPLE)\n"
            f"    list (APPEND {variable}\n"
            f"        {member}\n"
            "    )\n"
            "endif (NOT APPLE)\n"
        )
        if gated in cmake_text:
            cmake_text = cmake_text.replace(gated, restored, 1)
        elif restored not in cmake_text:
            print(f"could not preserve Wayland ScreenSaver {variable} entry",
                  file=sys.stderr)
            return 1
    cmake_file.write_text(cmake_text, encoding="utf-8")

    source_file = cmake_file.parent / "lxqtsingleapplication.cpp"
    source = source_file.read_text(encoding="utf-8")
    x11_includes = "#include <KWindowInfo>\n#include <KX11Extras>\n"
    x11_window_logic = (
        "        WId window = mActivationWindow->effectiveWinId();\n"
        "        KWindowInfo info(window, NET::WMDesktop);\n"
        "        int windowDesktop = info.desktop();\n"
        "\n"
        "        if (windowDesktop != KX11Extras::currentDesktop())\n"
        "            KX11Extras::setCurrentDesktop(windowDesktop);\n"
        "\n"
    )
    if "KWindowInfo" in source or "KX11Extras" in source:
        if source.count(x11_includes) != 1 or source.count(x11_window_logic) != 1:
            print("liblxqt single-application X11 block changed; refusing an unsafe patch", file=sys.stderr)
            return 1
        source = source.replace(x11_includes, "", 1)
        source = source.replace(x11_window_logic, "", 1)
        source_file.write_text(source, encoding="utf-8")

    screensaver_file = cmake_file.parent / "lxqtscreensaver.cpp"
    screensaver = screensaver_file.read_text(encoding="utf-8")
    if "#include <memory>\n" in screensaver and \
       "#ifndef AKAOS_WAYLAND_ONLY\n#include <memory>\n#endif" not in screensaver:
        screensaver = screensaver.replace(
            "#include <memory>\n",
            "#ifndef AKAOS_WAYLAND_ONLY\n#include <memory>\n#endif\n",
            1,
        )

    x11_start = "#include <X11/extensions/scrnsaver.h>\n"
    x11_end = "\n\nnamespace LXQt {"
    start = screensaver.find(x11_start)
    end = screensaver.find(x11_end, start)
    if start >= 0 and end >= 0:
        if "#ifndef AKAOS_WAYLAND_ONLY\n#include <X11/extensions/scrnsaver.h>" not in screensaver:
            screensaver = (screensaver[:start] + "#ifndef AKAOS_WAYLAND_ONLY\n" +
                           screensaver[start:end] + "#endif\n" + screensaver[end:])
    elif "#include <X11/extensions/scrnsaver.h>" in screensaver:
        print("liblxqt ScreenSaver X11 include block changed", file=sys.stderr)
        return 1

    helper_start = screensaver.find("static bool GetProperty(")
    helper_end = screensaver.find("class ScreenSaverPrivate", helper_start)
    if helper_start >= 0 and helper_end >= 0:
        if "#ifndef AKAOS_WAYLAND_ONLY" not in screensaver[helper_start:helper_end]:
            screensaver = (screensaver[:helper_start] +
                           "#ifndef AKAOS_WAYLAND_ONLY\n" +
                           screensaver[helper_start:helper_end] +
                           "#endif\n\n" + screensaver[helper_end:])
    else:
        print("could not locate liblxqt ScreenSaver X11 helpers", file=sys.stderr)
        return 1

    method = "bool ScreenSaverPrivate::isScreenSaverLocked()"
    method_start = screensaver.find(method)
    if method_start < 0:
        print("could not locate liblxqt ScreenSaver lock-state method", file=sys.stderr)
        return 1
    open_brace = screensaver.find("{", method_start)
    depth = 1
    cursor = open_brace + 1
    while cursor < len(screensaver) and depth:
        if screensaver[cursor] == "{":
            depth += 1
        elif screensaver[cursor] == "}":
            depth -= 1
        cursor += 1
    if depth != 0:
        print("unterminated liblxqt ScreenSaver lock-state method", file=sys.stderr)
        return 1
    body = screensaver[open_brace + 1:cursor - 1]
    if "#if defined(AKAOS_WAYLAND_ONLY)" not in body:
        body = (
            "\n#if defined(AKAOS_WAYLAND_ONLY)\n"
            "    return m_lockProcess && m_lockProcess->state() != QProcess::NotRunning;\n"
            "#else\n" + body + "\n#endif\n"
        )
        screensaver = screensaver[:open_brace + 1] + body + screensaver[cursor - 1:]

    available_method = "QList<QAction*> ScreenSaver::availableActions()"
    available_start = screensaver.find(available_method)
    if available_start < 0:
        print("could not locate liblxqt ScreenSaver actions method", file=sys.stderr)
        return 1
    available_body = screensaver.find("{", available_start)
    empty_command_check = "    if (d->lock_command.isEmpty()) return ret;\n"
    if empty_command_check not in screensaver[available_body:]:
        return_declaration = "    QList<QAction*> ret;\n"
        return_start = screensaver.find(return_declaration, available_body)
        if return_start < 0:
            print("could not find ScreenSaver action result list", file=sys.stderr)
            return 1
        return_end = return_start + len(return_declaration)
        screensaver = screensaver[:return_end] + (
            "    Q_D(ScreenSaver);\n"
            "    if (d->lock_command.isEmpty()) return ret;\n"
        ) + screensaver[return_end:]

    empty_lock_command = (
        '            qWarning() << Q_FUNC_INFO << "Empty screen lock_command";\n'
        "            return;\n"
    )
    if empty_lock_command in screensaver:
        screensaver = screensaver.replace(
            empty_lock_command,
            '            qWarning() << Q_FUNC_INFO << "Empty screen lock_command";\n'
            '#if defined(AKAOS_WAYLAND_ONLY)\n'
            '            Q_EMIT done();\n'
            '#endif\n'
            "            return;\n",
            1,
        )
    screensaver_file.write_text(screensaver, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
