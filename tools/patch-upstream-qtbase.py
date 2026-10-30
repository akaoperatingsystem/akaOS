#!/usr/bin/env python3
from pathlib import Path
import sys


qt_source = Path(sys.argv[1])
qthread_path = qt_source / "src" / "corelib" / "thread" / "qthread_unix.cpp"
qthread_source = qthread_path.read_text(encoding="utf-8")
akaos_stack_attr_guard = (
    "#if defined(Q_OS_AKAOS) || "
    "(defined(_POSIX_THREAD_ATTR_STACKSIZE) && "
    "(_POSIX_THREAD_ATTR_STACKSIZE-0 > 0))"
)
original_stack_attr_guard = (
    "#if defined(_POSIX_THREAD_ATTR_STACKSIZE) && "
    "(_POSIX_THREAD_ATTR_STACKSIZE-0 > 0)"
)
if akaos_stack_attr_guard not in qthread_source:
    if qthread_source.count(original_stack_attr_guard) != 1:
        raise SystemExit("QtBase pthread stack-size patch target changed")
    qthread_source = qthread_source.replace(
        original_stack_attr_guard, akaos_stack_attr_guard, 1
    )
    qthread_path.write_text(qthread_source, encoding="utf-8")

source_path = qt_source / "src" / "corelib" / "global" / "qsystemdetection.h"
source = source_path.read_text(encoding="utf-8")
marker = "#elif defined(__AKAOS__)\n#  define Q_OS_AKAOS"
if marker not in source:
    original = "#elif defined(__linux__) || defined(__linux)\n#  define Q_OS_LINUX"
    if source.count(original) != 1:
        raise SystemExit("Qt 6.12 platform-detection patch target changed")
    replacement = (
        "#elif defined(__AKAOS__)\n"
        "#  define Q_OS_AKAOS\n"
        "#elif defined(__linux__) || defined(__linux)\n"
        "#  define Q_OS_LINUX"
    )
    source = source.replace(original, replacement, 1)
    source_path.write_text(source, encoding="utf-8")

platform_defs_path = qt_source / "mkspecs" / "linux-clang" / "qplatformdefs.h"
platform_defs = platform_defs_path.read_text(encoding="utf-8")
akaos_features_guard = (
    "#if !defined(__AKAOS__)\n"
    "#  include <features.h>\n"
    "#endif"
)
if akaos_features_guard not in platform_defs:
    if platform_defs.count("#include <features.h>") != 1:
        raise SystemExit("Qt 6.12 Linux platform-definitions patch target changed")
    platform_defs = platform_defs.replace(
        "#include <features.h>", akaos_features_guard, 1
    )
    platform_defs_path.write_text(platform_defs, encoding="utf-8")

for header in ("sys/ipc.h", "sys/shm.h"):
    akaos_header_guard = (
        "#if !defined(__AKAOS__)\n"
        f"#  include <{header}>\n"
        "#endif"
    )
    original_header = f"#include <{header}>"
    if akaos_header_guard not in platform_defs:
        if platform_defs.count(original_header) != 1:
            raise SystemExit(
                "Qt 6.12 System V IPC platform-definitions patch target changed"
            )
        platform_defs = platform_defs.replace(
            original_header, akaos_header_guard, 1
        )
        platform_defs_path.write_text(platform_defs, encoding="utf-8")

akaos_time_header = "#if defined(__AKAOS__)\n#  include <time.h>\n#endif"
if akaos_time_header not in platform_defs:
    time_header = "#include <sys/time.h>"
    if platform_defs.count(time_header) != 1:
        raise SystemExit("Qt 6.12 time platform-definitions patch target changed")
    platform_defs = platform_defs.replace(
        time_header, time_header + "\n\n" + akaos_time_header, 1
    )
    platform_defs_path.write_text(platform_defs, encoding="utf-8")

akaos_large_file_guard = (
    "#if !defined(__AKAOS__)\n"
    "#  define QT_USE_XOPEN_LFS_EXTENSIONS\n"
    "#endif"
)
if akaos_large_file_guard not in platform_defs:
    large_file_define = "#define QT_USE_XOPEN_LFS_EXTENSIONS"
    if platform_defs.count(large_file_define) != 1:
        raise SystemExit("Qt 6.12 large-file platform-definitions patch target changed")
    platform_defs = platform_defs.replace(
        large_file_define, akaos_large_file_guard, 1
    )
    platform_defs_path.write_text(platform_defs, encoding="utf-8")

common_platform_defs_path = (
    qt_source / "mkspecs" / "common" / "posix" / "qplatformdefs.h"
)
common_platform_defs = common_platform_defs_path.read_text(encoding="utf-8")
akaos_open_largefile = (
    "#if defined(__AKAOS__)\n"
    "#  define QT_OPEN_LARGEFILE 0\n"
    "#else\n"
    "#  define QT_OPEN_LARGEFILE O_LARGEFILE\n"
    "#endif"
)
original_open_largefile = "#define QT_OPEN_LARGEFILE       O_LARGEFILE"
if akaos_open_largefile not in common_platform_defs:
    if common_platform_defs.count(original_open_largefile) != 1:
        raise SystemExit("Qt 6.12 large-file open flag patch target changed")
    common_platform_defs = common_platform_defs.replace(
        original_open_largefile, akaos_open_largefile, 1
    )
    common_platform_defs_path.write_text(
        common_platform_defs, encoding="utf-8"
    )

resource_path = qt_source / "src" / "corelib" / "io" / "qresource.cpp"
resource_source = resource_path.read_text(encoding="utf-8")
akaos_page_size_branch = (
    "#elif defined(__AKAOS__)\n"
    "    auto getpagesize = [] { return sysconf(_SC_PAGESIZE); };\n"
)
if akaos_page_size_branch not in resource_source:
    windows_page_size_branch = (
        "#ifdef Q_OS_WIN\n"
        "    auto getpagesize = [] {\n"
        "        SYSTEM_INFO sysinfo;\n"
        "        ::GetSystemInfo(&sysinfo);\n"
        "        return sysinfo.dwAllocationGranularity;\n"
        "    };\n"
        "#endif"
    )
    if resource_source.count(windows_page_size_branch) != 1:
        raise SystemExit("Qt 6.12 resource page-size patch target changed")
    resource_source = resource_source.replace(
        windows_page_size_branch,
        windows_page_size_branch[:-len("#endif")]
        + akaos_page_size_branch
        + "#endif",
        1,
    )
    resource_path.write_text(resource_source, encoding="utf-8")

file_mapping_path = (
    qt_source / "src" / "corelib" / "io" / "qfsfileengine_unix.cpp"
)
file_mapping_source = file_mapping_path.read_text(encoding="utf-8")
akaos_file_mapping_page_size = (
    "#elif defined(__AKAOS__)\n"
    "    int pageSize = sysconf(_SC_PAGESIZE);\n"
)
if akaos_file_mapping_page_size not in file_mapping_source:
    original_page_size_branches = (
        "#if defined(Q_OS_INTEGRITY)\n"
        "    int pageSize = sysconf(_SC_PAGESIZE);\n"
        "#else\n"
        "    int pageSize = getpagesize();\n"
        "#endif"
    )
    if file_mapping_source.count(original_page_size_branches) != 1:
        raise SystemExit("Qt 6.12 file-mapping page-size patch target changed")
    file_mapping_source = file_mapping_source.replace(
        original_page_size_branches,
        "#if defined(Q_OS_INTEGRITY)\n"
        "    int pageSize = sysconf(_SC_PAGESIZE);\n"
        + akaos_file_mapping_page_size
        + "#else\n"
        "    int pageSize = getpagesize();\n"
        "#endif",
        1,
    )
    file_mapping_path.write_text(file_mapping_source, encoding="utf-8")

timer_info_path = (
    qt_source / "src" / "corelib" / "kernel" / "qtimerinfo_unix.cpp"
)
timer_info_source = timer_info_path.read_text(encoding="utf-8")
akaos_times_header_guard = (
    "#if !defined(__AKAOS__)\n"
    "#  include <sys/times.h>\n"
    "#endif"
)
if akaos_times_header_guard not in timer_info_source:
    times_header = "#include <sys/times.h>"
    if timer_info_source.count(times_header) != 1:
        raise SystemExit("Qt 6.12 timer info header patch target changed")
    timer_info_source = timer_info_source.replace(
        times_header, akaos_times_header_guard, 1
    )
    timer_info_path.write_text(timer_info_source, encoding="utf-8")

forkfd_header_path = (
    qt_source / "src" / "3rdparty" / "forkfd" / "forkfd.h"
)
forkfd_header = forkfd_header_path.read_text(encoding="utf-8")
akaos_resource_header_guard = (
    "#if defined(__AKAOS__)\n"
    "#  include <sys/resource.h>\n"
    "#endif"
)
if akaos_resource_header_guard not in forkfd_header:
    standard_types_header = "#include <stdint.h>"
    if forkfd_header.count(standard_types_header) != 1:
        raise SystemExit("Qt 6.12 forkfd resource header patch target changed")
    forkfd_header = forkfd_header.replace(
        standard_types_header,
        standard_types_header + "\n" + akaos_resource_header_guard,
        1,
    )
    forkfd_header_path.write_text(forkfd_header, encoding="utf-8")

core_cmake_path = qt_source / "src" / "corelib" / "CMakeLists.txt"
core_cmake = core_cmake_path.read_text(encoding="utf-8")
akaos_storage_branch = (
    "qt_internal_extend_target(Core CONDITION AKAOS_TARGET\n"
    "    SOURCES\n"
    "        io/qstandardpaths_unix.cpp\n"
    "        io/qstorageinfo_stub.cpp\n"
    ")"
)
linux_storage_branch = (
    "qt_internal_extend_target(Core CONDITION LINUX AND NOT ANDROID AND NOT OHOS AND NOT VXWORKS\n"
    "    SOURCES\n"
    "        io/qstandardpaths_unix.cpp\n"
    "        io/qstorageinfo_linux.cpp\n"
    ")"
)
akaos_linux_storage_branch = (
    "qt_internal_extend_target(Core CONDITION LINUX AND NOT AKAOS_TARGET AND NOT ANDROID AND NOT OHOS AND NOT VXWORKS\n"
    "    SOURCES\n"
    "        io/qstandardpaths_unix.cpp\n"
    "        io/qstorageinfo_linux.cpp\n"
    ")"
)
if akaos_storage_branch not in core_cmake:
    if core_cmake.count(linux_storage_branch) != 1:
        raise SystemExit("Qt 6.12 storage-info source-selection patch target changed")
    core_cmake = core_cmake.replace(
        linux_storage_branch,
        akaos_storage_branch + "\n\n" + akaos_linux_storage_branch,
        1,
    )
    core_cmake_path.write_text(core_cmake, encoding="utf-8")

tools_configure_path = qt_source / "src" / "tools" / "configure.cmake"
tools_configure = tools_configure_path.read_text(encoding="utf-8")
original_scanner_condition = (
    "qt_feature(\"qtwaylandscanner\" PRIVATE\n"
    "    CONDITION TARGET Wayland::Scanner AND NOT INTEGRITY AND NOT ANDROID "
    "AND NOT WASM AND NOT IOS AND NOT QNX AND NOT VXWORKS AND NOT OHOS\n"
    ")"
)
previous_akaos_scanner_condition = (
    "qt_feature(\"qtwaylandscanner\" PRIVATE\n"
    "    CONDITION (TARGET Wayland::Scanner OR AKAOS_QTWAYLANDSCANNER_HOST) "
    "AND NOT INTEGRITY AND NOT ANDROID AND NOT WASM AND NOT IOS AND NOT QNX "
    "AND NOT VXWORKS AND NOT OHOS\n"
    ")"
)
akaos_scanner_condition = (
    "if(AKAOS_QTWAYLANDSCANNER_HOST)\n"
    "    qt_feature(\"qtwaylandscanner\" PRIVATE CONDITION TRUE)\n"
    "else()\n"
    "    qt_feature(\"qtwaylandscanner\" PRIVATE\n"
    "        CONDITION TARGET Wayland::Scanner AND NOT INTEGRITY AND NOT ANDROID "
    "AND NOT WASM AND NOT IOS AND NOT QNX AND NOT VXWORKS AND NOT OHOS\n"
    "    )\n"
    "endif()"
)
if akaos_scanner_condition not in tools_configure:
    if tools_configure.count(previous_akaos_scanner_condition) == 1:
        tools_configure = tools_configure.replace(
            previous_akaos_scanner_condition, akaos_scanner_condition, 1
        )
    elif tools_configure.count(original_scanner_condition) == 1:
        tools_configure = tools_configure.replace(
            original_scanner_condition, akaos_scanner_condition, 1
        )
    else:
        raise SystemExit("Qt 6.12 Wayland scanner feature patch target changed")
    tools_configure_path.write_text(tools_configure, encoding="utf-8")

portal_transfer_path = (
    qt_source / "src" / "gui" / "platform" / "unix" /
    "qxdgdesktopportalfiletransfer.cpp"
)
portal_transfer_source = portal_transfer_path.read_text(encoding="utf-8")
akaos_portal_open_flags = (
    "#if defined(__AKAOS__)\n"
    "    constexpr int portalFileOpenFlags = O_RDONLY;\n"
    "#else\n"
    "    constexpr int portalFileOpenFlags = O_PATH;\n"
    "#endif\n"
    "    for (const auto &url : urls) {\n"
    "        if (int fd = qt_safe_open(QFile::encodeName(url.toLocalFile()), portalFileOpenFlags); fd >= 0)"
)
original_portal_open_flags = (
    "    for (const auto &url : urls) {\n"
    "        if (int fd = qt_safe_open(QFile::encodeName(url.toLocalFile()), O_PATH); fd >= 0)"
)
if akaos_portal_open_flags not in portal_transfer_source:
    if portal_transfer_source.count(original_portal_open_flags) != 1:
        raise SystemExit("Qt 6.12 portal file-open flag patch target changed")
    portal_transfer_source = portal_transfer_source.replace(
        original_portal_open_flags, akaos_portal_open_flags, 1
    )
    portal_transfer_path.write_text(portal_transfer_source, encoding="utf-8")

wayland_display_path = (
    qt_source / "src" / "plugins" / "platforms" / "wayland" /
    "qwaylanddisplay.cpp"
)
wayland_display_source = wayland_display_path.read_text(encoding="utf-8")
akaos_poll_header = (
    "#if defined(__AKAOS__)\n"
    "#  include <poll.h>\n"
    "#endif"
)
poll_header_block = "\n\n" + akaos_poll_header
if poll_header_block in wayland_display_source:
    wayland_display_source = wayland_display_source.replace(
        poll_header_block, "", 1
    )
    wayland_display_path.write_text(wayland_display_source, encoding="utf-8")

gui_configure_path = qt_source / "src" / "gui" / "configure.cmake"
gui_configure_source = gui_configure_path.read_text(encoding="utf-8")
akaos_wayland_qpa_condition = (
    "CONDITION QT_FEATURE_gui AND LINUX AND NOT ANDROID AND NOT QT_FEATURE_xcb "
    "AND NOT QT_FEATURE_eglfs AND NOT QT_FEATURE_directfb "
    "AND NOT QT_FEATURE_linuxfb AND NOT QT_FEATURE_wayland_client"
)
original_qpa_condition = (
    "CONDITION QT_FEATURE_gui AND LINUX AND NOT ANDROID AND NOT QT_FEATURE_xcb "
    "AND NOT QT_FEATURE_eglfs AND NOT QT_FEATURE_directfb "
    "AND NOT QT_FEATURE_linuxfb"
)
if akaos_wayland_qpa_condition not in gui_configure_source:
    if gui_configure_source.count(original_qpa_condition) != 1:
        raise SystemExit("Qt 6.12 QPA warning condition patch target changed")
    gui_configure_source = gui_configure_source.replace(
        original_qpa_condition, akaos_wayland_qpa_condition, 1
    )
    gui_configure_path.write_text(gui_configure_source, encoding="utf-8")

wayland_shm_path = (
    qt_source / "src" / "plugins" / "platforms" / "wayland" /
    "qwaylandshmbackingstore.cpp"
)
wayland_shm_source = wayland_shm_path.read_text(encoding="utf-8")
akaos_page_size = (
    "#if defined(Q_OS_AKAOS)\n"
    "    const int pageSize = sysconf(_SC_PAGESIZE);\n"
    "#else\n"
    "    const int pageSize = getpagesize();\n"
    "#endif\n"
    "    const int alloc = alignTo(stride * size.height(), pageSize);"
)
original_page_size = (
    "    const int alloc = alignTo(stride * size.height(), getpagesize());"
)
if akaos_page_size not in wayland_shm_source:
    if wayland_shm_source.count(original_page_size) != 1:
        raise SystemExit("Qt 6.12 Wayland shared-memory page-size patch target changed")
    wayland_shm_source = wayland_shm_source.replace(
        original_page_size, akaos_page_size, 1
    )
    wayland_shm_path.write_text(wayland_shm_source, encoding="utf-8")

akaos_memfd_path = (
    "#if defined(Q_OS_AKAOS)\n"
    "    fd = memfd_create(\"wayland-shm\", MFD_CLOEXEC | MFD_ALLOW_SEALING);\n"
    "#elif defined(SYS_memfd_create)\n"
    "    fd = syscall(SYS_memfd_create, \"wayland-shm\", MFD_CLOEXEC | MFD_ALLOW_SEALING);\n"
    "#endif\n"
    "    if (fd >= 0)\n"
    "        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL);"
)
original_memfd_path = (
    "#ifdef SYS_memfd_create\n"
    "    fd = syscall(SYS_memfd_create, \"wayland-shm\", MFD_CLOEXEC | MFD_ALLOW_SEALING);\n"
    "    if (fd >= 0)\n"
    "        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL);\n"
    "#endif"
)
if akaos_memfd_path not in wayland_shm_source:
    if wayland_shm_source.count(original_memfd_path) != 1:
        raise SystemExit("Qt 6.12 Wayland memfd portability patch target changed")
    wayland_shm_source = wayland_shm_source.replace(
        original_memfd_path, akaos_memfd_path, 1
    )
    wayland_shm_path.write_text(wayland_shm_source, encoding="utf-8")
