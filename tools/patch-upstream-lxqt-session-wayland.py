#!/usr/bin/env python3
"""Apply the reproducible Wayland-only akaOS adaptations to LXQt session."""

import sys
from pathlib import Path


def replace_once(content: str, old: str, new: str, label: str) -> str:
    if new in content and old not in content:
        return content
    if content.count(old) != 1:
        raise ValueError(f"unexpected {label} source layout")
    return content.replace(old, new, 1)


def guard_function(content: str, signature: str, next_signature: str | None,
                   unused_parameter: str) -> str:
    start = content.find(signature)
    if start < 0:
        raise ValueError(f"missing LXQt session function: {signature}")
    open_brace = content.find("{", start)
    if open_brace < 0:
        raise ValueError(f"missing function body: {signature}")
    end = content.find(next_signature, open_brace) if next_signature else len(content)
    if end < 0:
        raise ValueError(f"missing function boundary after: {signature}")
    if "#ifndef AKAOS_WAYLAND_ONLY" in content[open_brace:end]:
        return content
    close_brace = content.rfind("\n}", open_brace, end)
    if close_brace < 0:
        raise ValueError(f"missing function terminator: {signature}")
    body = content[open_brace + 1:close_brace]
    wrapped = (
        "\n#ifndef AKAOS_WAYLAND_ONLY\n" + body +
        "\n#else\n    (void)" + unused_parameter + ";\n#endif\n"
    )
    return content[:open_brace + 1] + wrapped + content[close_brace:]


def patch_root_cmake(source_dir: Path) -> None:
    path = source_dir / "CMakeLists.txt"
    content = path.read_text(encoding="utf-8")
    if "project(lxqt-session)" not in content or 'set(LXQT_MINIMUM_VERSION "2.4.0")' not in content:
        raise ValueError(f"unexpected lxqt-session source: {path}")

    if 'option(AKAOS_WAYLAND_ONLY "Build the akaOS Wayland session without X11-only tools" OFF)' not in content:
        content = replace_once(
            content,
            'option(WITH_LIBUDEV "Build with libudev support" ON)\n',
            'option(WITH_LIBUDEV "Build with libudev support" ON)\n'
            'option(AKAOS_WAYLAND_ONLY "Build the akaOS Wayland session without X11-only tools" OFF)\n'
            'if (AKAOS_WAYLAND_ONLY)\n'
            '    add_definitions(-DAKAOS_WAYLAND_ONLY)\n'
            'endif()\n',
            "Wayland-only build option",
        )
    if "if (NOT AKAOS_WAYLAND_ONLY)\n    find_package(X11 REQUIRED)" not in content:
        content = replace_once(
            content,
            "find_package(X11 REQUIRED)\n",
            "if (NOT AKAOS_WAYLAND_ONLY)\n    find_package(X11 REQUIRED)\nendif()\n",
            "X11 package lookup",
        )
    content = replace_once(
        content,
        'if (CMAKE_SYSTEM_NAME STREQUAL "Linux")\n    pkg_search_module(PROCPS REQUIRED libproc2 libprocps)',
        'if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT AKAOS_WAYLAND_ONLY)\n'
        '    pkg_search_module(PROCPS REQUIRED libproc2 libprocps)',
        "procps package lookup",
    )
    if "if (NOT AKAOS_WAYLAND_ONLY)\n    add_subdirectory(lxqt-config-session)" not in content:
        content = replace_once(
            content,
            "add_subdirectory(lxqt-config-session)\n",
            "if (NOT AKAOS_WAYLAND_ONLY)\n"
            "    add_subdirectory(lxqt-config-session)\n"
            "endif()\n",
            "X11 session-configuration tool",
        )
    path.write_text(content, encoding="utf-8")


def patch_session_cmake(source_dir: Path) -> None:
    path = source_dir / "lxqt-session" / "CMakeLists.txt"
    content = path.read_text(encoding="utf-8")
    if "if (NOT AKAOS_WAYLAND_ONLY)\n    include_directories(${X11_INCLUDE_DIR})" not in content:
        content = replace_once(
            content,
            "include_directories(\n    ${X11_INCLUDE_DIR}\n)\n",
            "if (NOT AKAOS_WAYLAND_ONLY)\n"
            "    include_directories(${X11_INCLUDE_DIR})\n"
            "endif()\n",
            "session-manager X11 include directory",
        )
    content = content.replace(
        'if (CMAKE_SYSTEM_NAME STREQUAL "Linux")',
        'if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT AKAOS_WAYLAND_ONLY)',
    )
    content = content.replace("    src/numlock.cpp\n", "")
    if "target_link_libraries(lxqt-session ${X11_LIBRARIES})" not in content:
        content = replace_once(
            content,
            "    ${X11_LIBRARIES}\n    KF6::WindowSystem\n)",
            "    KF6::WindowSystem\n)\n"
            "if (NOT AKAOS_WAYLAND_ONLY)\n"
            "    target_link_libraries(lxqt-session ${X11_LIBRARIES})\n"
            "endif()",
            "session-manager X11 link library",
        )
    path.write_text(content, encoding="utf-8")


def patch_session_source(source_dir: Path) -> None:
    path = source_dir / "lxqt-session" / "src" / "sessionapplication.cpp"
    content = path.read_text(encoding="utf-8")
    if '#if defined(WITH_LIBUDEV_MONITOR)\n#include "UdevNotifier.h"' not in content:
        content = replace_once(
            content,
            '#include "UdevNotifier.h"\n',
            '#if defined(WITH_LIBUDEV_MONITOR)\n#include "UdevNotifier.h"\n#endif\n',
            "UDev notifier include",
        )
    if "#ifndef AKAOS_WAYLAND_ONLY\n#include <X11/XKBlib.h>" not in content:
        content = replace_once(
            content,
            "#include <X11/XKBlib.h>\n",
            "#ifndef AKAOS_WAYLAND_ONLY\n#include <X11/XKBlib.h>\n#endif\n",
            "X11 keyboard include",
        )
    content = guard_function(
        content,
        "void SessionApplication::loadKeyboardSettings(LXQt::Settings& settings)",
        "\nvoid SessionApplication::loadMouseSettings",
        "settings",
    )
    content = guard_function(
        content,
        "void SessionApplication::loadMouseSettings(LXQt::Settings& settings)",
        "\n# if 0",
        "settings",
    )
    content = guard_function(
        content,
        "void SessionApplication::setLeftHandedMouse(bool mouse_left_handed)",
        "\nbool SessionApplication::updateDBusEnvironment()",
        "mouse_left_handed",
    )
    diagnostic = (
        "int SessionApplication::exec()\n{\n"
        "    if (!registered) {\n"
        "        qCWarning(SESSION) << \"Session D-Bus registration failed for\"\n"
        "                           << qgetenv(\"DBUS_SESSION_BUS_ADDRESS\") << \":\"\n"
        "                           << QDBusConnection::sessionBus().lastError().message();\n"
        "        return 0;\n"
        "    }\n"
    )
    if diagnostic not in content:
        previously_diagnostic = (
            "int SessionApplication::exec()\n{\n"
            "    if (!registered) {\n"
            "        qCWarning(SESSION) << \"Session D-Bus registration failed:\"\n"
            "                           << QDBusConnection::sessionBus().lastError().message();\n"
            "        return 0;\n"
            "    }\n"
        )
        if previously_diagnostic in content:
            content = content.replace(previously_diagnostic, diagnostic, 1)
        else:
            content = replace_once(
                content,
                "int SessionApplication::exec()\n{\n"
                "    if (!registered)\n"
                "        return 0;\n",
                diagnostic,
                "session D-Bus registration diagnostic",
            )
    path.write_text(content, encoding="utf-8")

    leave_file = source_dir / "lxqt-leave" / "leavedialog.cpp"
    leave_source = leave_file.read_text(encoding="utf-8")
    lock_item = (
        '    item->setData(Qt::UserRole, -1);\n'
        '    ui->listWidget->addItem(item);\n'
    )
    disabled_lock_item = (
        '    item->setData(Qt::UserRole, -1);\n'
        '    if (mScreensaver->availableActions().isEmpty())\n'
        '        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);\n'
        '    ui->listWidget->addItem(item);\n'
    )
    if disabled_lock_item not in leave_source:
        if leave_source.count(lock_item) != 1:
            raise ValueError("LXQt Leave lock action layout changed; refusing an unsafe patch")
        leave_source = leave_source.replace(lock_item, disabled_lock_item, 1)
        leave_file.write_text(leave_source, encoding="utf-8")

    module_manager_file = source_dir / "lxqt-session" / "src" / "lxqtmodman.cpp"
    module_manager = module_manager_file.read_text(encoding="utf-8")
    if "#ifndef AKAOS_WAYLAND_ONLY\n#include <NETWM>" not in module_manager:
        module_manager = replace_once(
            module_manager,
            "#include <NETWM>\n",
            "#ifndef AKAOS_WAYLAND_ONLY\n#include <NETWM>\n#endif\n",
            "X11 window-manager protocol include",
        )
    module_manager = guard_function(
        module_manager,
        "void LXQtModuleManager::startWm(LXQt::Settings *settings)",
        "\nvoid LXQtModuleManager::startProcess",
        "settings",
    )
    module_manager_file.write_text(module_manager, encoding="utf-8")


def patch_proc_reaper(source_dir: Path) -> None:
    path = source_dir / "lxqt-session" / "src" / "procreaper.cpp"
    content = path.read_text(encoding="utf-8")
    content = content.replace(
        "#if defined(Q_OS_LINUX)\n",
        "#if defined(Q_OS_LINUX) && !defined(AKAOS_WAYLAND_ONLY)\n",
    )
    if "#include <fcntl.h>\n" not in content:
        content = content.replace(
            "#include <unistd.h>\n",
            "#include <unistd.h>\n#include <fcntl.h>\n#include <cstdlib>\n#include <vector>\n",
            1,
        )

    stop_start = content.find("void ProcReaper::stop(")
    if stop_start < 0:
        raise ValueError("missing ProcReaper::stop()")
    head, stop = content[:stop_start], content[stop_start:]
    procps_end = (
        "    procps_pids_unref(&info);\n"
        "# else\n"
        "    PROCTAB * proc_dir = ::openproc(PROC_FILLSTAT);\n"
        "    while (proc_t * proc = ::readproc(proc_dir, nullptr))\n"
        "    {\n"
        "        if (proc->ppid == my_pid)\n"
        "        {\n"
        "            children.push_back(proc->tgid);\n"
        "        }\n"
        "        ::freeproc(proc);\n"
        "    }\n"
        "    ::closeproc(proc_dir);\n"
        "# endif\n"
        "#elif defined(Q_OS_FREEBSD)"
    )
    akaos_proc_scan = (
        "    procps_pids_unref(&info);\n"
        "# else\n"
        "    PROCTAB * proc_dir = ::openproc(PROC_FILLSTAT);\n"
        "    while (proc_t * proc = ::readproc(proc_dir, nullptr))\n"
        "    {\n"
        "        if (proc->ppid == my_pid)\n"
        "        {\n"
        "            children.push_back(proc->tgid);\n"
        "        }\n"
        "        ::freeproc(proc);\n"
        "    }\n"
        "    ::closeproc(proc_dir);\n"
        "# endif\n"
        "#elif defined(AKAOS_WAYLAND_ONLY)\n"
        "    char process_list[4096];\n"
        "    int descriptor = ::open(\"/proc/processes\", O_RDONLY);\n"
        "    if (descriptor >= 0) {\n"
        "        size_t used = 0;\n"
        "        while (used + 1U < sizeof(process_list)) {\n"
        "            ssize_t amount = ::read(descriptor, process_list + used,\n"
        "                                    sizeof(process_list) - used - 1U);\n"
        "            if (amount < 0 && errno == EINTR) continue;\n"
        "            if (amount <= 0) break;\n"
        "            used += static_cast<size_t>(amount);\n"
        "        }\n"
        "        (void)::close(descriptor);\n"
        "        process_list[used] = '\\0';\n"
        "        char *line = process_list;\n"
        "        while (*line) {\n"
        "            char *next = std::strchr(line, '\\n');\n"
        "            if (next) *next = '\\0';\n"
        "            char *pid_end = nullptr;\n"
        "            long child_pid = std::strtol(line, &pid_end, 10);\n"
        "            if (pid_end != line) {\n"
        "                char *ppid_start = pid_end;\n"
        "                while (*ppid_start == ' ' || *ppid_start == '\\t')\n"
        "                    ++ppid_start;\n"
        "                char *ppid_end = nullptr;\n"
        "                long parent_pid = std::strtol(ppid_start, &ppid_end, 10);\n"
        "                if (ppid_end != ppid_start && parent_pid == my_pid &&\n"
        "                    child_pid > 0 && child_pid != my_pid)\n"
        "                    children.push_back(static_cast<pid_t>(child_pid));\n"
        "            }\n"
        "            if (!next) break;\n"
        "            line = next + 1;\n"
        "        }\n"
        "    } else {\n"
        "        qCWarning(SESSION) << \"Unable to read /proc/processes:\" << strerror(errno);\n"
        "    }\n"
        "#elif defined(Q_OS_FREEBSD)"
    )
    if "#elif defined(AKAOS_WAYLAND_ONLY)" not in stop:
        if stop.count(procps_end) != 1:
            raise ValueError("procps stop block changed; refusing an unsafe patch")
        stop = stop.replace(procps_end, akaos_proc_scan, 1)
    path.write_text(head + stop, encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch-upstream-lxqt-session-wayland.py SOURCE_DIR", file=sys.stderr)
        return 2
    source_dir = Path(sys.argv[1])
    try:
        patch_root_cmake(source_dir)
        patch_session_cmake(source_dir)
        patch_session_source(source_dir)
        patch_proc_reaper(source_dir)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
