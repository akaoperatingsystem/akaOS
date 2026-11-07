#!/usr/bin/env bash
set -euo pipefail

# The build host can carry include and pkg-config search paths for an unrelated
# toolchain.  Clang honours CPATH even under -nostdinc and pkg-config prefers
# PKG_CONFIG_PATH over PKG_CONFIG_LIBDIR, so both are cleared to keep the
# build self contained.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
ESP_DIR="$BUILD_DIR/esp"
INCLUDE_DIR="$ROOT_DIR/sdk/include"
UPSTREAM_GLIB_BUILD="$BUILD_DIR/upstream-build/glib-2.90.0/x86_64-akaos-v7"
UPSTREAM_GLIB_SOURCE="$BUILD_DIR/upstream-source/glib-2.90.0/glib-2.90.0"
UPSTREAM_PIXMAN_BUILD="$BUILD_DIR/upstream-build/pixman-0.46.4/x86_64-akaos"
UPSTREAM_CAIRO_IMAGE_BUILD="$BUILD_DIR/upstream-build/cairo-1.18.6/x86_64-akaos"
UPSTREAM_CAIRO_FREETYPE_BUILD="$BUILD_DIR/upstream-build/cairo-1.18.6/x86_64-akaos-freetype"
UPSTREAM_CAIRO_BUILD="$UPSTREAM_CAIRO_IMAGE_BUILD"
UPSTREAM_FREETYPE_BUILD="$BUILD_DIR/upstream-build/freetype-2.14.3/x86_64-akaos"
UPSTREAM_HARFBUZZ_BUILD="$BUILD_DIR/upstream-build/harfbuzz-14.5.0/x86_64-akaos-shared-runtime"
UPSTREAM_FONTCONFIG_BUILD="$BUILD_DIR/upstream-build/fontconfig-2.18.3/x86_64-akaos-current-libc"
UPSTREAM_EXPAT_BUILD="$BUILD_DIR/upstream-build/expat-2.8.5/x86_64-akaos-linux"
UPSTREAM_FRIBIDI_BUILD="$BUILD_DIR/upstream-build/fribidi-1.0.17/x86_64-akaos"
UPSTREAM_FRIBIDI_SOURCE="$BUILD_DIR/upstream-source/fribidi-1.0.17/extracted"
UPSTREAM_PANGO_BUILD="$BUILD_DIR/upstream-build/pango-1.58.2/x86_64-akaos"
UPSTREAM_PANGO_SOURCE="$BUILD_DIR/upstream-source/pango-1.58.2/extracted"
UPSTREAM_GRAPHENE_BUILD="$BUILD_DIR/upstream-build/graphene-1.10.8/x86_64-akaos"
UPSTREAM_GRAPHENE_SOURCE="$BUILD_DIR/upstream-source/graphene-1.10.8/extracted"
UPSTREAM_GTK_BUILD="$BUILD_DIR/upstream-build/gtk-4.22.5/x86_64-akaos-wayland"
UPSTREAM_GTK_SOURCE="$BUILD_DIR/upstream-source/gtk-4.22.5/extracted"
UPSTREAM_GTK3_BUILD="$BUILD_DIR/upstream-build/gtk-3.24.52/x86_64-akaos-wayland"
UPSTREAM_GTK3_SOURCE="$BUILD_DIR/upstream-source/gtk-3.24.52/extracted"
UPSTREAM_ATK_BUILD="$BUILD_DIR/upstream-build/atk-2.38.0/x86_64-akaos"
UPSTREAM_WAYLAND_BUILD="$BUILD_DIR/upstream-build/wayland-1.26.0/x86_64-akaos-wayland"
UPSTREAM_WAYLAND_SOURCE="$BUILD_DIR/upstream-source/wayland-1.26.0/extracted"
UPSTREAM_WAYLAND_HOST_PREFIX="$BUILD_DIR/host-tools/wayland-1.26.0"
UPSTREAM_WAYLAND_PROTOCOLS_BUILD="$BUILD_DIR/upstream-build/wayland-protocols-1.46/native-data"
UPSTREAM_XKBCOMMON_BUILD="$BUILD_DIR/upstream-build/libxkbcommon-1.13.2/x86_64-akaos"
UPSTREAM_XKBCOMMON_SOURCE="$BUILD_DIR/upstream-source/libxkbcommon-1.13.2/extracted"
UPSTREAM_GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
UPSTREAM_LXQT_PREFIX="${AKAOS_LXQT_RUNTIME_PREFIX:-$UPSTREAM_GRAPHICS_SYSROOT/usr/local}"
UPSTREAM_QT_PREFIX="$BUILD_DIR/upstream-sysroot/qtbase-6.12.0-x86_64/usr/local"
UPSTREAM_QT_BUILD_DIR="${AKAOS_QT_BUILD_DIR:-}"
UPSTREAM_QT_HOST_PREFIX="${AKAOS_QT_HOST_PREFIX:-}"
UPSTREAM_QT_DECLARATIVE_BUILD_DIR="${AKAOS_QT_DECLARATIVE_BUILD_DIR:-}"
UPSTREAM_QT_TARGET_ROOT="${AKAOS_QT_TARGET_ROOT:-$ROOT_DIR}"
UPSTREAM_QT_HOST_QML_TOOLS_DIR="${AKAOS_QT_HOST_QML_TOOLS_DIR:-}"
if [[ -z "$UPSTREAM_QT_HOST_QML_TOOLS_DIR" &&
      -n "$UPSTREAM_QT_DECLARATIVE_BUILD_DIR" &&
      -f "$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/CMakeCache.txt" ]]; then
    UPSTREAM_QT_HOST_QML_TOOLS_DIR="$(sed -n \
        's/^Qt6QmlTools_DIR:[^=]*=//p' \
        "$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/CMakeCache.txt" | head -n 1)"
fi
source "$ROOT_DIR/tools/host-common.sh"

AKAOS_BUILD_JOBS="${AKAOS_BUILD_JOBS:-$(akaos_detect_cpu_count)}"
export AKAOS_BUILD_JOBS

run_upstream_refresh() {
    local component="$1"
    shift
    if [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" == "1" ]]; then
        echo "[SKIP] Upstream $component refresh (reusing existing build artifacts)"
        return 0
    fi
    "$@"
}

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

Run without options in an interactive terminal to open the enhanced configuration menu.

Target Architecture & Firmware:
  --arch NAME                  Target CPU architecture:
                                 x86_64  : 64-bit x86 PC (UEFI / SeaBIOS)
                                 x86_32  : 32-bit x86 PC (UEFI FAT32 / BIOS Hybrid ISO)
                                 aarch64 : 64-bit ARM (QEMU virt / direct kernel)
                                 arm32   : 32-bit ARM (ARMv7-A QEMU virt)
                                 riscv64 : 64-bit RISC-V (RV64GC QEMU virt)
                                 riscv32 : 32-bit RISC-V (RV32GC QEMU virt)
  --firmware NAME              Boot firmware: uefi, bios, or both (default depends on arch)
  --disk-type, --file-type NAME Boot media format: fat32 or iso (inferred when omitted)

Desktop & Session Configuration:
  --desktop, --desktop-env NAME
                               Default desktop environment:
                                 xfce    : XFCE 4.18 Desktop Session (xfwm4, xfconf, panel)
                                 lxde    : LXDE Desktop Suite (Openbox, LXPanel, PCManFM)
                                 openbox : Standalone Openbox 3.6.1 Window Manager
                                 aurora  : akaOS native compositor & resident desktop
                                 lxqt    : Lightweight Qt Desktop Environment
                                 none    : None (No desktop environment / Headless server)
  --session-mode NAME          Default boot session mode:
                                 console : Direct tty1 shell login (default)
                                 desktop : Autostart graphical desktop environment
  --software, --software-bundle NAME
                               Software package bundle selection:
                                 all     : Full Suite (124+ tools, GUI apps, WMs, GTK3/4, Qt6, X11 libs)
                                 libs    : Runtime Libraries (libc, libm, libc++, GLib, GTK, Qt6, X11 libs)
                                 desktop : Desktop Essentials (GUIs, Editors, Tools, Panels, Compositor)
                                 net     : Network & Servers (wget, curl, httpd, sshd, telnetd, tftpd)
                                 dev     : Developer & Diagnostics (akafetch, pkg, lscpu, lspci, gpuinfo)
                                 manual  : Custom Interactive Package Picker (Select individual packages)
                                 minimal : Minimal Base (Core Unix utilities & akash shell only)
                                 none    : None (Barebones kernel bootstrap only)
  --packages LIST              Comma-separated list of individual packages to bundle (manual mode)

Kernel & Build Flags:
  --test                       Enable automated self-test mode in generated boot media
  --verbose                    Enable verbose kernel hardware and boot diagnostics
  --check-deps                 Check selected host dependencies and exit
  --menu                       Open the interactive terminal configuration menu
  -h, --help                   Display this comprehensive help manual

Examples:
  ./build.sh                                     # Launch interactive configuration TUI
  ./build.sh --arch x86_64 --firmware uefi --disk-type fat32
  ./build.sh --arch x86_64 --desktop xfce --session-mode desktop
  ./build.sh --arch x86_64 --desktop lxde --session-mode desktop
  ./build.sh --arch aarch64 --session-mode console
  ./build.sh --arch riscv64 --test
EOF
}

configure_build_interactively() {
    source "$ROOT_DIR/tools/interactive-ui.sh"
    akaos_ui_require_terminal || exit 2

    local menu_arch=x86_64
    local menu_firmware=uefi
    local menu_disk=fat32
    local menu_desktop=none
    local menu_session=console
    local menu_software=all
    local menu_test=off
    local menu_verbose=off
    local action selection summary report
    local -a dependency_args

    while true; do
        if ! action="$(akaos_ui_menu \
            "akaOS Build Configuration" \
            "Select an option to customize, then select Start Build." \
            "start" \
            "arch" "Architecture: $menu_arch" \
            "firmware" "Firmware: $menu_firmware" \
            "disk" "Format: $menu_disk" \
            "desktop" "Desktop: $menu_desktop" \
            "session" "Startup mode: $menu_session" \
            "software" "Software package: $menu_software" \
            "test" "Test mode: $menu_test" \
            "verbose" "Verbose logging: $menu_verbose" \
            "deps" "Check host dependencies" \
            "start" "Start build" \
            "quit" "Exit")"; then
            exit 0
        fi

        case "$action" in
            software)
                if selection="$(akaos_ui_menu \
                    "Software Packages" "Select software bundle to include in akaOS:" "$menu_software" \
                    "all" "Full Suite (All 124+ Tools, GUI apps, WMs, GTK3/4, Qt6, X11 libs)" \
                    "apps" "All Apps (All 124+ tools without heavy upstream GUI libs)" \
                    "libs" "Runtime Libraries (Shared libc, GLib, GTK3/4, Qt6, Cairo, X11 libs)" \
                    "desktop" "Desktop Essentials (GUIs, Editors, Panels, Tools, Compositors)" \
                    "manual" "Manual Package Selector (Checklist of individual packages)" \
                    "net" "Network & Server Suite (wget, curl, httpd, sshd, telnetd, tftpd, ip)" \
                    "dev" "Developer & Build Tools (akafetch, pkg, lscpu, lspci, gpuinfo, sysmon)" \
                    "minimal" "Minimal Base (Core Unix utilities & akash shell only)" \
                    "none" "None (Barebones kernel bootstrap only)")"; then
                    menu_software="$selection"
                    if [[ "$menu_software" == "manual" ]]; then
                        local selected_pkgs
                        if selected_pkgs="$(akaos_ui_checklist \
                            "Manual Package Selection" \
                            "Choose packages to include (Space to toggle, Enter to confirm):" \
                            "akafetch" "Real CPU/GPU hardware spec visualizer" ON \
                            "wget" "GNU-compatible HTTP file download tool" ON \
                            "curl" "Network transfer client" ON \
                            "pkg" "akaOS native package manager" ON \
                            "httpd" "Micro web server daemon" ON \
                            "sshd" "Secure shell server daemon" ON \
                            "ssh" "Secure shell client" ON \
                            "telnetd" "Telnet remote terminal server daemon" ON \
                            "tftpd" "TFTP network file server daemon" ON \
                            "tftp" "TFTP client utility" ON \
                            "ip" "Network interface & address management" ON \
                            "route" "IP routing table management" ON \
                            "ping" "ICMP network echo probe" ON \
                            "traceroute" "Network packet path tracer" ON \
                            "netstat" "Network socket & connection status" ON \
                            "arp" "Address resolution protocol cache tool" ON \
                            "dig" "DNS lookup utility" ON \
                            "nslookup" "Name service lookup query" ON \
                            "terminal" "Graphical terminal emulator" ON \
                            "notepad" "Graphical text editor" ON \
                            "taskmgr" "Graphical process & task manager" ON \
                            "webviewer" "Graphical web & HTML viewer" ON \
                            "palette" "GUI color theme & palette inspector" ON \
                            "ted" "Simple text editor" ON \
                            "less" "Terminal pager" ON \
                            "more" "Basic terminal pager" ON \
                            "sysmon" "System telemetry & hardware monitor" ON \
                            "lscpu" "CPU architecture & topology information" ON \
                            "lspci" "PCI discovery & device identifier" ON \
                            "gpuinfo" "GPU hardware & driver status" ON \
                            "sndinfo" "Sound device & AC97/HDA status" ON \
                            "acpi" "ACPI power & thermal diagnostics" ON \
                            "uptime" "System uptime & load average" ON \
                            "free" "Memory utilization statistics" ON \
                            "df" "Disk filesystem space usage" ON \
                            "du" "Disk directory space estimation" ON \
                            "tar" "POSIX ustar archive utility" ON \
                            "gzip" "GNU gzip compression utility" ON \
                            "gunzip" "Gzip decompression utility" ON \
                            "sed" "Stream editor for text transformation" ON \
                            "grep" "Regular expression pattern matching" ON \
                            "diff" "File difference comparison utility" ON \
                            "patch" "Source code patch application tool" ON \
                            "cmp" "Byte-by-byte file comparison" ON \
                            "find" "Recursive filesystem search tool" ON \
                            "xargs" "Command argument constructor from stdin" ON \
                            "watch" "Periodic command output monitor" ON \
                            "cut" "Columnar text extraction" ON \
                            "tr" "Character translation & deletion" ON \
                            "sort" "Text line sorting utility" ON \
                            "uniq" "Repeated line filtering utility" ON \
                            "head" "Display first lines of a file" ON \
                            "tail" "Display last lines of a file" ON \
                            "wc" "Word, line, and byte counter" ON \
                            "which" "Locate executable path" ON \
                            "touch" "Update timestamp or create empty file" ON \
                            "mkdir" "Directory creation tool" ON \
                            "rm" "File and directory removal tool" ON \
                            "cp" "File and directory copy tool" ON \
                            "mv" "File and directory move/rename tool" ON \
                            "chmod" "File mode & permission changer" ON \
                            "chown" "File owner & group changer" ON \
                            "clear" "Terminal screen clearing tool" ON \
                            "sync" "Buffer cache to disk synchronizer" ON \
                            "reboot" "System reboot utility" ON \
                            "poweroff" "System poweroff utility" ON \
                            "ps" "Process status snapshot" ON \
                            "kill" "Signal delivery to process" ON \
                            "pkill" "Signal delivery by process name" ON \
                            "sleep" "Execution delay utility" ON \
                            "tty" "Terminal device name reporter" ON \
                            "stty" "Terminal line discipline config" ON \
                            "beep" "Console acoustic bell generator" ON \
                            "openbox" "Openbox 3.6.1 window manager" ON \
                            "xfwm4" "XFCE 4.18 window manager" ON \
                            "xfce4-session" "XFCE 4.18 desktop session manager" ON \
                            "xfconfd" "XFCE configuration storage daemon" ON \
                            "lxpanel" "LXDE desktop panel (0.11.1)" ON \
                            "pcmanfm" "PCManFM desktop & file manager" ON \
                            "lumen" "Native hardware compositor engine" ON \
                            "aurora" "akaOS resident desktop shell" ON \
                            "xserver" "Native X11 display server" ON \
                            "dbus-daemon" "D-Bus IPC message broker" ON)"; then
                            menu_custom_packages="$selected_pkgs"
                        fi
                    fi
                fi
                ;;
            arch)
                if selection="$(akaos_ui_menu \
                    "Target Architecture" "Select the target CPU architecture." "$menu_arch" \
                    "x86_64" "64-bit x86 PC (UEFI / SeaBIOS)" \
                    "x86_32" "32-bit x86 PC (UEFI / BIOS Hybrid)" \
                    "aarch64" "64-bit ARM (QEMU virt / direct kernel)" \
                    "arm32" "32-bit ARM (ARMv7-A QEMU virt)" \
                    "riscv64" "64-bit RISC-V (RV64GC QEMU virt)" \
                    "riscv32" "32-bit RISC-V (RV32GC QEMU virt)")"; then
                    menu_arch="$selection"
                fi
                ;;
            firmware)
                if selection="$(akaos_ui_menu \
                    "Firmware" "Select the firmware target." "$menu_firmware" \
                    "uefi" "Build for UEFI (GOP Framebuffer)" \
                    "bios" "Build for legacy BIOS (Multiboot2)" \
                    "both" "Build UEFI and BIOS boot support")"; then
                    menu_firmware="$selection"
                fi
                ;;
            disk)
                if selection="$(akaos_ui_menu \
                    "Disk / File Format" "Select the boot-media image format." "$menu_disk" \
                    "fat32" "FAT32 ESP disk image (.img)" \
                    "iso" "Bootable hybrid optical image (.iso)")"; then
                    menu_disk="$selection"
                fi
                ;;
            desktop)
                if selection="$(akaos_ui_menu \
                    "Select Desktop Environment" "Choose desktop environment to bundle:" "$menu_desktop" \
                    "none" "None (Pure Console / No GUI desktop installed)" \
                    "xfce" "XFCE 4.18 (Full desktop session, panel, xfwm4)" \
                    "lxde" "LXDE Suite (Openbox, LXPanel, PCManFM)" \
                    "openbox" "Openbox 3.6.1 (Clean lightweight window manager)" \
                    "aurora" "Aurora (akaOS native resident desktop)" \
                    "lxqt" "LXQt (Qt6 based modern desktop session)")"; then
                    menu_desktop="$selection"
                    if [[ "$menu_desktop" == "none" && "$menu_session" == "desktop" ]]; then
                        menu_session="console"
                    fi
                fi
                ;;
            session)
                if selection="$(akaos_ui_menu \
                    "Select Startup Mode" "How should akaOS start on boot?" "$menu_session" \
                    "console" "Console TTY: Direct shell terminal login" \
                    "desktop" "Desktop GUI: Autostart graphical desktop")"; then
                    if [[ "$selection" == "desktop" && "$menu_desktop" == "none" ]]; then
                        akaos_ui_message "Configuration Error" "A desktop environment is required for graphical startup, but none is currently selected.\n\nPlease choose a desktop environment from the menu before selecting the Desktop GUI startup mode."
                    elif [[ "$selection" == "console" && "$menu_desktop" != "none" ]]; then
                        if akaos_ui_confirm "Warning: Session Conflict" "Notice: Desktop environment [$menu_desktop] is selected, but the startup mode is set to Console.\n\nThe desktop packages will be bundled into the image, but the system will boot into the text terminal login.\n\nDo you wish to continue with these settings?" "Continue" "Back"; then
                            menu_session="$selection"
                        fi
                    else
                        menu_session="$selection"
                    fi
                fi
                ;;
            test)
                if selection="$(akaos_ui_menu \
                    "Automated Test Mode" "Embed or run the automated test path." "$menu_test" \
                    "off" "Normal production build" \
                    "on" "Enable automated test mode")"; then
                    menu_test="$selection"
                fi
                ;;
            verbose)
                if selection="$(akaos_ui_menu \
                    "Verbose Boot Logging" "Choose the kernel boot-log mode." "$menu_verbose" \
                    "off" "Quiet graphical boot splash" \
                    "on" "Show verbose hardware initialization logs")"; then
                    menu_verbose="$selection"
                fi
                ;;
            deps)
                dependency_args=(--mode build --arch "$menu_arch" \
                    --firmware "$menu_firmware" --disk-type "$menu_disk")
                if [[ "$menu_test" == "on" ]]; then dependency_args+=(--test); fi
                if report="$(bash "$ROOT_DIR/tools/dependencies.sh" \
                    "${dependency_args[@]}" 2>&1)"; then
                    akaos_ui_message "Host Dependencies" "$report"
                else
                    akaos_ui_message "Missing Host Dependencies" "$report"
                fi
                ;;
            start)
                dependency_args=(--mode build --arch "$menu_arch" \
                    --firmware "$menu_firmware" --disk-type "$menu_disk")
                if [[ "$menu_test" == "on" ]]; then dependency_args+=(--test); fi
                if ! report="$(bash "$ROOT_DIR/tools/dependencies.sh" \
                    "${dependency_args[@]}" --quiet 2>&1)"; then
                    akaos_ui_message "Missing Host Dependencies" "$report"
                    continue
                fi
                local heavy_warning=""
                if [[ "$menu_desktop" != "none" || "$menu_software" == "all" || "$menu_software" == "desktop" || "$menu_software" == "manual" ]]; then
                    heavy_warning="\n\n[!] Build Notice: You have selected extensive desktop/software components ($menu_desktop / $menu_software).\nCompiling and packaging these components may require additional time depending on your CPU performance, available RAM, and storage speed."
                fi

                summary="Architecture  : $menu_arch
Firmware      : $menu_firmware
Format        : $menu_disk
Desktop       : $menu_desktop
Startup Mode  : $menu_session
Software      : $menu_software
Test Mode     : $menu_test
Verbose       : $menu_verbose$heavy_warning"
                if akaos_ui_confirm "Confirm akaOS Build" "$summary" "Start Build" "Back"; then
                    INTERACTIVE_BUILD_ARGS=(
                        --arch "$menu_arch"
                        --firmware "$menu_firmware"
                        --disk-type "$menu_disk"
                        --desktop "$menu_desktop"
                        --session-mode "$menu_session"
                        --software "$menu_software"
                    )
                    if [[ "$menu_test" == "on" ]]; then
                        INTERACTIVE_BUILD_ARGS+=(--test)
                    fi
                    if [[ "$menu_verbose" == "on" ]]; then
                        INTERACTIVE_BUILD_ARGS+=(--verbose)
                    fi
                    return 0
                fi
                ;;
            quit)
                exit 0
                ;;
        esac
    done
}

open_menu=0
if [[ $# -eq 0 && -t 0 && -t 1 ]]; then
    open_menu=1
elif [[ $# -eq 1 && "$1" == "--menu" ]]; then
    open_menu=1
fi
if [[ "$open_menu" -eq 1 ]]; then
    INTERACTIVE_BUILD_ARGS=()
    configure_build_interactively
    set -- "${INTERACTIVE_BUILD_ARGS[@]}"
fi

arch=x86_64
firmware=uefi
firmware_set=0
disk_type=auto
disk_type_set=0
target_desktop=none
session_mode=console
menu_software=all
menu_custom_packages=""
bios_test=0
bios_verbose=0
check_deps_only=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            if [[ $# -lt 2 ]]; then
                echo "--arch requires a target name (x86_64, x86_32, aarch64, arm32, riscv64, riscv32)" >&2
                usage >&2
                exit 2
            fi
            arch="$2"
            shift 2
            ;;
        --firmware)
            if [[ $# -lt 2 ]]; then
                echo "--firmware requires uefi, bios, or both" >&2
                exit 2
            fi
            requested_firmware="$2"
            if [[ "$requested_firmware" != "uefi" && "$requested_firmware" != "bios" && "$requested_firmware" != "both" ]]; then
                echo "Unsupported firmware '$requested_firmware' (expected uefi, bios, or both)" >&2
                exit 2
            fi
            firmware="$requested_firmware"
            firmware_set=1
            shift 2
            ;;
        --disk-type|--file-type|--format)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires fat32 or iso" >&2
                exit 2
            fi
            disk_type="$2"
            disk_type_set=1
            shift 2
            ;;
        --desktop|--desktop-env)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires xfce, lxde, openbox, aurora, lxqt, or none" >&2
                exit 2
            fi
            target_desktop="$2"
            shift 2
            ;;
        --software|--software-bundle)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires all, apps, desktop, libs, net, dev, manual, minimal, or none" >&2
                exit 2
            fi
            menu_software="$2"
            shift 2
            ;;
        --packages|--package-list)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires a list of package names" >&2
                exit 2
            fi
            menu_custom_packages="$2"
            menu_software="manual"
            shift 2
            ;;
        --session-mode)
            if [[ $# -lt 2 ]]; then
                echo "--session-mode requires console or desktop" >&2
                exit 2
            fi
            session_mode="$2"
            shift 2
            ;;
        --console)
            session_mode=console
            shift
            ;;
        --desktop-autostart)
            session_mode=desktop
            shift
            ;;


        --test)
            bios_test=1
            shift
            ;;
        --verbose)
            bios_verbose=1
            shift
            ;;
        --check-deps)
            check_deps_only=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option '$1'; use --help for usage" >&2
            exit 2
            ;;
    esac
done

case "$arch" in
    x86_64|x86_32|aarch64|arm32|riscv64|riscv32) ;;
    *)
        echo "Unsupported architecture '$arch'" >&2
        exit 2
        ;;
esac

case "$disk_type" in
    auto) ;;
    fat32|iso) ;;
    *)
        echo "Unsupported disk type '$disk_type' (expected fat32 or iso)" >&2
        exit 2
        ;;
esac

dependency_firmware="$firmware"
if [[ "$arch" == "x86_32" && "$firmware_set" -eq 0 ]]; then
    dependency_firmware=both
fi
dependency_disk="$disk_type"
if [[ "$dependency_disk" == "auto" ]]; then
    case "$dependency_firmware" in
        uefi) dependency_disk=fat32 ;;
        bios) dependency_disk=iso ;;
        both)
            if [[ "$arch" == "x86_32" ]]; then dependency_disk=iso
            else dependency_disk=fat32
            fi
            ;;
    esac
fi
dependency_args=(--mode build --arch "$arch" --firmware "$dependency_firmware" \
    --disk-type "$dependency_disk")
if [[ "$bios_test" -eq 1 ]]; then dependency_args+=(--test); fi
if [[ "$check_deps_only" -eq 1 ]]; then
    exec bash "$ROOT_DIR/tools/dependencies.sh" "${dependency_args[@]}"
fi
if ! bash "$ROOT_DIR/tools/dependencies.sh" "${dependency_args[@]}" --quiet; then
    exit 1
fi

if [[ "$arch" == "x86_32" ]]; then
    if [[ "$firmware_set" -eq 0 ]]; then
        firmware=both
    fi
    if [[ "$disk_type" == "auto" ]]; then
        case "$firmware" in
            uefi) disk_type=fat32 ;;
            bios|both) disk_type=iso ;;
        esac
    fi
    build_args=(--firmware "$firmware" --disk-type "$disk_type" --software "$menu_software" --desktop "$menu_desktop")
    if [[ "$bios_test" -eq 1 ]]; then
        build_args+=(--test)
    fi
    if [[ "$bios_verbose" -eq 1 ]]; then
        build_args+=(--verbose)
    fi
    exec bash "$ROOT_DIR/tools/build-x86_32.sh" "${build_args[@]}"
elif [[ "$arch" == "aarch64" ]]; then
    if [[ "$bios_test" -eq 1 ]]; then
        exec bash "$ROOT_DIR/tools/test-aarch64.sh"
    fi
    exec bash "$ROOT_DIR/tools/build-aarch64.sh"
elif [[ "$arch" == "arm32" ]]; then
    if [[ "$bios_test" -eq 1 ]]; then
        exec bash "$ROOT_DIR/tools/test-arm32.sh"
    fi
    exec bash "$ROOT_DIR/tools/build-arm32.sh"
elif [[ "$arch" == "riscv64" ]]; then
    if [[ "$bios_test" -eq 1 ]]; then
        exec bash "$ROOT_DIR/tools/test-riscv64.sh"
    fi
    exec bash "$ROOT_DIR/tools/build-riscv64.sh"
elif [[ "$arch" == "riscv32" ]]; then
    if [[ "$bios_test" -eq 1 ]]; then
        exec bash "$ROOT_DIR/tools/test-riscv32.sh"
    fi
    exec bash "$ROOT_DIR/tools/build-riscv32.sh"
fi

if [[ "$disk_type" == "auto" ]]; then
    case "$firmware" in
        uefi) disk_type=fat32 ;;
        bios) disk_type=iso ;;
        both) disk_type=fat32 ;;
    esac
fi

need_uefi_payload=0
need_fat32_image=0
need_iso_image=0
if [[ "$firmware" == "uefi" || "$firmware" == "both" || "$disk_type" == "iso" ]]; then
    need_uefi_payload=1
fi
if [[ "$disk_type" == "fat32" ]]; then
    need_fat32_image=1
fi
if [[ "$disk_type" == "iso" || "$firmware" == "bios" || "$firmware" == "both" ]]; then
    need_iso_image=1
fi
# A FAT32 artifact needs the UEFI payload even when an accompanying BIOS ISO
# is also requested. BIOS runs select the ISO; UEFI runs select the FAT32 disk.
if [[ "$need_fat32_image" -eq 1 ]]; then
    need_uefi_payload=1
fi

build_tools=(clang ld.lld objcopy readelf python3)
if [[ "$need_uefi_payload" -eq 1 ]]; then
    build_tools+=(lld-link mkfs.fat mmd mcopy truncate)
fi
for tool in "${build_tools[@]}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build dependency: $tool" >&2
        echo "On Ubuntu: sudo apt install clang lld mtools dosfstools" >&2
        exit 1
    fi
done

if ! python3 -c "import PIL" >/dev/null 2>&1; then
    echo "Missing build dependency: Python Pillow" >&2
    echo "On Ubuntu: sudo apt install python3-pil fonts-dejavu-core" >&2
    exit 1
fi

FONT_FILE="$(akaos_find_dejavu_font)"
GENERATED_DIR="$BUILD_DIR/generated"
mkdir -p "$GENERATED_DIR"
python3 "$ROOT_DIR/tools/generate_font.py" "$FONT_FILE" "$GENERATED_DIR/font_data.h"
python3 "$ROOT_DIR/tools/generate_vga_font.py" "$GENERATED_DIR/font8x16_data.h"

mkdir -p "$BUILD_DIR"
if [[ "$need_uefi_payload" -eq 1 ]]; then
    mkdir -p "$ESP_DIR/EFI/BOOT"
fi

echo "[BUILD] akaOS userspace"
BUILD_TIMESTAMP="${BUILD_TIMESTAMP:-$(date +%m%d%Y%H%M%S)}"
export BUILD_TIMESTAMP
USER_INCLUDE_DIR="$ROOT_DIR/userspace/libc/include"
USER_LIBC_SOURCES=(
    "$ROOT_DIR/userspace/libc/src/syscall.c"
    "$ROOT_DIR/userspace/libc/src/string.c"
    "$ROOT_DIR/userspace/libc/src/stdlib.c"
    "$ROOT_DIR/userspace/libc/src/malloc.c"
    "$ROOT_DIR/userspace/libc/src/math.c"
    "$ROOT_DIR/userspace/libc/src/fenv.c"
    "$ROOT_DIR/userspace/libc/src/setjmp.c"
    "$ROOT_DIR/userspace/libc/src/stdio.c"
    "$ROOT_DIR/userspace/libc/src/time.c"
    "$ROOT_DIR/userspace/libc/src/account.c"
    "$ROOT_DIR/userspace/libc/src/posix.c"
    "$ROOT_DIR/userspace/libc/src/pthread.c"
    "$ROOT_DIR/userspace/libc/src/atomic64.c"
    "$ROOT_DIR/userspace/libc/src/toolkit.c"
    "$ROOT_DIR/userspace/libc/src/dbus.c"
    "$ROOT_DIR/userspace/libc/src/dbus_codec.c"
    "$ROOT_DIR/userspace/libc/src/dlfcn.c"
    "$ROOT_DIR/userspace/libc/src/epoll.c"
    "$ROOT_DIR/userspace/libc/src/eventfd.c"
    "$ROOT_DIR/userspace/libc/src/timerfd.c"
    "$ROOT_DIR/userspace/libc/src/semaphore.c"
    "$ROOT_DIR/userspace/libc/src/cxxabi.c"
    "$ROOT_DIR/userspace/libc/src/crt_init.c"
    "$ROOT_DIR/userspace/libc/src/getopt.c"
    "$ROOT_DIR/userspace/libc/src/regex.c"
    "$ROOT_DIR/userspace/libc/src/syslog.c"
)
USER_LIBC_OBJECTS=()
for source in "${USER_LIBC_SOURCES[@]}"; do
    object="$BUILD_DIR/user-libc-$(basename "${source%.c}").o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -ffunction-sections -fdata-sections \
        -mno-red-zone -mcmodel=small \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    USER_LIBC_OBJECTS+=("$object")
done
llvm-ar rcs "$BUILD_DIR/libc.a" "${USER_LIBC_OBJECTS[@]}"
clang --target=x86_64-unknown-none-elf \
    -ffreestanding -fpie -mno-red-zone -mcmodel=small \
    -c "$ROOT_DIR/userspace/libc/crt/x86_64/crt0.S" \
    -o "$BUILD_DIR/user-crt0.o"
clang --target=x86_64-unknown-none-elf \
    -ffreestanding -fpie -mno-red-zone -mcmodel=small \
    -c "$ROOT_DIR/userspace/libc/crt/x86_64/crt0.S" \
    -o "$BUILD_DIR/user-dynamic-crt0.o"

build_user_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -ffunction-sections -fdata-sections \
        -mno-red-zone -mcmodel=small \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    ld.lld -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "$main_object" \
        --whole-archive "$BUILD_DIR/user-libc-crt_init.o" --no-whole-archive \
        "$BUILD_DIR/libc.a" \
        -o "$BUILD_DIR/$output"
}

build_xserver() {
    local name="xserver"
    local source_dir="$ROOT_DIR/userspace/bin/xserver"
    local output="XSERVER.ELF"
    local objects=()
    for unit in main client window events draw resource input setup xcore; do
        local object="$BUILD_DIR/user-$name-$unit.o"
        clang --target=x86_64-unknown-none-elf \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fpie \
            -mno-red-zone -mcmodel=small \
            -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
            -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" -I"$GENERATED_DIR" -I"$source_dir/include" \
            -c "$source_dir/$unit.c" -o "$object"
        objects+=("$object")
    done
    ld.lld -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "${objects[@]}" \
        --whole-archive "$BUILD_DIR/user-libc-crt_init.o" --no-whole-archive \
        "$BUILD_DIR/libc.a" \
        -o "$BUILD_DIR/$output"
}

build_bzip2() {
    local source_dir="$ROOT_DIR/userspace/bin/bzip2"
    local objects=()
    for unit in blocksort huffman crctable randtable compress decompress bzlib bzip2; do
        local object="$BUILD_DIR/bzip2-$unit.o"
        clang --target=x86_64-unknown-none-elf \
            -std=c17 -O2 -Wall -Wextra \
            -ffreestanding -fno-builtin -fno-stack-protector -fpie \
            -mno-red-zone -mcmodel=small \
            -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" -I"$source_dir" \
            -D_FILE_OFFSET_BITS=64 \
            -c "$source_dir/$unit.c" -o "$object"
        objects+=("$object")
    done
    ld.lld -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "${objects[@]}" \
        --whole-archive "$BUILD_DIR/user-libc-crt_init.o" --no-whole-archive \
        "$BUILD_DIR/libc.a" \
        -o "$BUILD_DIR/BZIP2.ELF"
}

build_user_cpp_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    local cxx_runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64"
    if [[ "${AKAOS_SKIP_CXX_RUNTIME_REFRESH:-0}" == "1" ]]; then
        for runtime_file in libc++.so.1 libc++abi.so.1 libunwind.so.1; do
            if [[ ! -s "$BUILD_DIR/$runtime_file" ]]; then
                echo "[ERROR] Cannot reuse missing C++ runtime: $BUILD_DIR/$runtime_file" >&2
                return 1
            fi
        done
        echo "[SKIP] LLVM C++ runtime refresh (reusing existing akaOS artifacts)"
    else
        bash "$ROOT_DIR/tools/build-libunwind.sh" --arch x86_64
    fi
    clang++ --target=x86_64-unknown-none-elf \
        -std=c++17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fstack-protector-strong -fpie \
        -fexceptions -frtti \
        -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE \
        -mno-red-zone -mcmodel=small \
        -I"$cxx_runtime_dir/include/c++/v1" -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    ld.lld -nostdlib -pie -static --eh-frame-hdr -z max-page-size=0x1000 \
        --undefined=__register_frame \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "${USER_LIBC_OBJECTS[@]}" "$main_object" \
        "$cxx_runtime_dir/libc++.a" "$cxx_runtime_dir/libc++abi.a" "$cxx_runtime_dir/libunwind.a" \
        -o "$BUILD_DIR/$output"
}

build_user_pie_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "${USER_LIBC_OBJECTS[@]}" "$main_object" \
        -o "$BUILD_DIR/$output"
}

build_account_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -ffunction-sections -fdata-sections \
        -mno-red-zone -mcmodel=small \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    ld.lld -nostdlib -pie -static --gc-sections -s -z max-page-size=0x1000 \
        -T "$ROOT_DIR/userspace/linker/user.ld" \
        "$BUILD_DIR/user-crt0.o" "$main_object" \
        --whole-archive "$BUILD_DIR/user-libc-crt_init.o" --no-whole-archive \
        "$BUILD_DIR/libc.a" \
        -o "$BUILD_DIR/$output"
}

build_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-answer.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -c "$ROOT_DIR/userspace/libc/tests/shared_answer.c" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname libakaos-demo.so --hash-style=gnu -z max-page-size=0x1000 \
        "$object" -o "$BUILD_DIR/$output"
}

build_shared_libc() {
    local output="$1"
    local shared_objects=()
    for source in "${USER_LIBC_SOURCES[@]}"; do
        local base="$(basename "$source" .c)"
        local object="$BUILD_DIR/shared-libc-$base.o"
        clang --target=x86_64-unknown-none-elf \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
            -mno-red-zone -mcmodel=small \
            -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
            -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
            -c "$source" -o "$object"
        shared_objects+=("$object")
    done
    local signal_object="$BUILD_DIR/shared-libc-signal.o"
    clang --target=x86_64-unknown-none-elf \
        -ffreestanding -fPIC -mno-red-zone -mcmodel=small \
        -c "$ROOT_DIR/userspace/libc/tests/shared_signal.S" \
        -o "$signal_object"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname "$output" \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        -Bsymbolic-functions \
        "${shared_objects[@]}" "$signal_object" -o "$BUILD_DIR/$output"
}

build_shared_libm() {
    local output="$1"
    local runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname "$output" \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        "$BUILD_DIR/shared-libc-math.o" \
        "$runtime_dir/libakaos-compiler-builtins.a" \
        "$BUILD_DIR/libc.so.6" -o "$BUILD_DIR/$output"
    ln -sf "$output" "$BUILD_DIR/libm.so"
}

build_cxx_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-cxx.o"
    local fini_object="$BUILD_DIR/shared-cxx-fini.o"
    clang++ --target=x86_64-unknown-none-elf \
        -std=c++17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -fno-exceptions -fno-rtti \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$ROOT_DIR/userspace/libc/tests/shared_cxx.cpp" -o "$object"
    clang --target=x86_64-unknown-none-elf -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$ROOT_DIR/userspace/libc/crt/cxx-dso-fini.c" -o "$fini_object"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname "$output" \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        "$object" "$fini_object" --no-as-needed "$BUILD_DIR/libc.so.6" \
        -o "$BUILD_DIR/$output"
}

build_shared_cxx_runtime() {
    local runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64"
    bash "$ROOT_DIR/tools/build-compiler-builtins.sh" --arch x86_64
    local compiler_builtins="$runtime_dir/libakaos-compiler-builtins.a"
    ld.lld -m elf_x86_64 -nostdlib -shared --eh-frame-hdr -soname libunwind.so.1 \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        --whole-archive "$runtime_dir/libunwind.a" --no-whole-archive \
        "$compiler_builtins" \
        "$BUILD_DIR/libc.so.6" -o "$BUILD_DIR/libunwind.so.1"
    ld.lld -m elf_x86_64 -nostdlib -shared --eh-frame-hdr -soname libc++abi.so.1 \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        --whole-archive "$runtime_dir/libc++abi.a" --no-whole-archive \
        "$BUILD_DIR/libunwind.so.1" "$BUILD_DIR/libc.so.6" \
        -o "$BUILD_DIR/libc++abi.so.1"
    ld.lld -m elf_x86_64 -nostdlib -shared --eh-frame-hdr -soname libc++.so.1 \
        --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
        --whole-archive "$runtime_dir/libc++.a" --no-whole-archive \
        "$compiler_builtins" "$BUILD_DIR/libc++abi.so.1" \
        "$BUILD_DIR/libunwind.so.1" "$BUILD_DIR/libc.so.6" \
        -o "$BUILD_DIR/libc++.so.1"
}

build_dynamic_cpp_program() {
    local source="$1"
    local output="$2"
    local runtime_dir="$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64"
    local object="$BUILD_DIR/user-cxx-dynamic-main.o"
    clang++ --target=x86_64-unknown-none-elf \
        -std=c++17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fstack-protector-strong -fpie \
        -fexceptions -frtti -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE \
        -mno-red-zone -mcmodel=small \
        -I"$runtime_dir/include/c++/v1" -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" --no-as-needed \
        "$BUILD_DIR/libc++.so.1" "$BUILD_DIR/libc++abi.so.1" \
        "$BUILD_DIR/libunwind.so.1" "$BUILD_DIR/libc.so.6" \
        -o "$BUILD_DIR/$output"
}

build_static_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-static.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -ftls-model=initial-exec \
        -mno-red-zone -mcmodel=small \
        -c "$ROOT_DIR/userspace/libc/tests/shared_static.c" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname libakaos-static.so \
        --hash-style=gnu -z max-page-size=0x1000 \
        "$object" -o "$BUILD_DIR/$output"
}

build_versioned_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-versioned.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -c "$ROOT_DIR/userspace/libc/tests/shared_versioned.c" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname libakaos-versioned.so \
        --hash-style=gnu -z max-page-size=0x1000 \
        --version-script "$ROOT_DIR/userspace/libc/tests/shared_versioned.map" \
        "$object" -o "$BUILD_DIR/$output"
}

build_dependent_shared_object() {
    local output="$1"
    local object="$BUILD_DIR/shared-dependent.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mno-red-zone -mcmodel=small \
        -c "$ROOT_DIR/userspace/libc/tests/shared_dependent.c" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname libakaos-dependent.so -z max-page-size=0x1000 \
        "$object" "$BUILD_DIR/libakaos-demo.so" "$BUILD_DIR/libakaos-versioned.so" \
        -o "$BUILD_DIR/$output"
}

build_loader_fanout_objects() {
    local provider_objects=()
    for index in 0 1 2 3 4; do
        local provider_object="$BUILD_DIR/loader-provider-$index.o"
        local provider_output="$BUILD_DIR/libakaos-loader-dep-$index.so"
        clang --target=x86_64-unknown-none-elf \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
            -mno-red-zone -mcmodel=small \
            -DAKAOS_LOADER_DEPENDENCY_INDEX=$index \
            -c "$ROOT_DIR/userspace/libc/tests/shared_loader_leaf.c" \
            -o "$provider_object"
        ld.lld -m elf_x86_64 -nostdlib -shared \
            -soname "libakaos-loader-dep-$index.so" \
            -z max-page-size=0x1000 "$provider_object" \
            -o "$provider_output"
        provider_objects+=("$provider_output")
    done
    local fanout_object="$BUILD_DIR/shared-loader-fanout.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mno-red-zone -mcmodel=small \
        -c "$ROOT_DIR/userspace/libc/tests/shared_loader_fanout.c" \
        -o "$fanout_object"
    ld.lld -m elf_x86_64 -nostdlib -shared \
        -soname libakaos-loader-fanout.so -z max-page-size=0x1000 \
        --no-undefined "$fanout_object" --no-as-needed \
        "${provider_objects[@]}" -o "$BUILD_DIR/libakaos-loader-fanout.so"
}

build_runtime_library() {
    local output="$1"
    local runtime_id="$2"
    # A library that the upstream builds find under the name of a real library
    # must not carry the compatibility surface, because the first library that
    # answers for an entry point wins.
    local source="${3:-$ROOT_DIR/userspace/libc/tests/desktop_runtime.c}"
    local object="$BUILD_DIR/runtime-${runtime_id}.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mno-red-zone -mcmodel=small \
        -DAKAOS_RUNTIME_ID="$runtime_id" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -shared -soname "$output" -z max-page-size=0x1000 \
        "$object" "$BUILD_DIR/runtime-io.o" -o "$BUILD_DIR/$output"
}

build_dynamic_program() {
    local name="$1"
    local source="$2"
    local output="$3"
    local main_object="$BUILD_DIR/user-$name-main.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$main_object"
    ld.lld -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-crt0.o" "$main_object" \
        --no-as-needed "$BUILD_DIR/libc.so.6" \
        "$BUILD_DIR/libakaos-demo.so" -o "$BUILD_DIR/$output"
}

build_upstream_glib_program() {
    local source="$ROOT_DIR/userspace/bin/glib-upstream-test/main.c"
    local object="$BUILD_DIR/glib-upstream-test.o"
    local output="$BUILD_DIR/GLIB_UPSTREAM_TEST.ELF"
    local upstream_cflags=(
        -I"$UPSTREAM_GLIB_BUILD" -I"$UPSTREAM_GLIB_SOURCE"
        -I"$UPSTREAM_GLIB_BUILD/glib" -I"$UPSTREAM_GLIB_SOURCE/glib"
        -I"$UPSTREAM_GLIB_BUILD/gobject" -I"$UPSTREAM_GLIB_SOURCE/gobject"
        -I"$UPSTREAM_GLIB_BUILD/gio" -I"$UPSTREAM_GLIB_SOURCE/gio"
        -I"$UPSTREAM_GLIB_BUILD/gmodule" -I"$UPSTREAM_GLIB_SOURCE/gmodule"
        -I"$UPSTREAM_GLIB_BUILD/subprojects/proxy-libintl-0.5"
        -I"$ROOT_DIR/build/upstream-sysroot/pcre2-10.49-x86_64/usr/include"
    )

    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        "${upstream_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"

    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed \
        "$UPSTREAM_GLIB_BUILD/gio/libgio-2.0.so.0.9000.0" \
        "$UPSTREAM_GLIB_BUILD/gobject/libgobject-2.0.so.0.9000.0" \
        "$UPSTREAM_GLIB_BUILD/gmodule/libgmodule-2.0.so.0.9000.0" \
        "$UPSTREAM_GLIB_BUILD/glib/libglib-2.0.so.0.9000.0" \
        "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_cairo_program() {
    local source="$ROOT_DIR/userspace/bin/cairo-upstream-test/main.c"
    local object="$BUILD_DIR/cairo-upstream-test.o"
    local output="$BUILD_DIR/CAIRO_UPSTREAM_TEST.ELF"
    local -a font_flags=()
    local -a font_libraries=()
    local -a gobject_flags=()
    local -a gobject_libraries=()
    if [[ -f "$UPSTREAM_CAIRO_BUILD/util/cairo-gobject/libcairo-gobject.so.2.11806.6" ]]; then
        gobject_flags=(
            -DAKAOS_CAIRO_WITH_GOBJECT=1
            -I"$BUILD_DIR/upstream-source/cairo-1.18.6/util/cairo-gobject"
            -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/include"
            -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/include/glib-2.0"
            -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/glib-2.0/include"
        )
        gobject_libraries=(
            "$UPSTREAM_CAIRO_BUILD/util/cairo-gobject/libcairo-gobject.so.2.11806.6"
            "$UPSTREAM_GLIB_BUILD/gobject/libgobject-2.0.so.0.9000.0"
            "$UPSTREAM_GLIB_BUILD/glib/libglib-2.0.so.0.9000.0"
        )
    fi
    if [[ "${UPSTREAM_CAIRO_FREETYPE_ENABLED:-0}" -eq 1 ]]; then
        font_flags=(
            -DAKAOS_CAIRO_WITH_FREETYPE=1
            -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include/freetype2"
            -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include"
        )
        font_libraries=(
            "$UPSTREAM_FREETYPE_BUILD/libfreetype.so.6.20.6"
            "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libfontconfig.so.1.17.0"
            "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libexpat.so.1.12.5"
        )
    fi
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        "${font_flags[@]}" \
        "${gobject_flags[@]}" \
        -I"$UPSTREAM_CAIRO_BUILD/src" \
        -I"$BUILD_DIR/upstream-source/cairo-1.18.6/src" \
        -I"$BUILD_DIR/upstream-sysroot/pixman-0.46.4-x86_64/usr/local/include/pixman-1" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed \
        "${gobject_libraries[@]}" \
        "$UPSTREAM_CAIRO_BUILD/src/libcairo.so.2.11806.6" \
        "$UPSTREAM_PIXMAN_BUILD/pixman/libpixman-1.so.0.46.4" \
        "$UPSTREAM_GLIB_BUILD/gobject/libgobject-2.0.so.0.9000.0" \
        "$UPSTREAM_GLIB_BUILD/glib/libglib-2.0.so.0.9000.0" \
        "${font_libraries[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" \
        -o "$output"
}

build_upstream_freetype_program() {
    local source="$ROOT_DIR/userspace/bin/freetype-upstream-test/main.c"
    local object="$BUILD_DIR/freetype-upstream-test.o"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include/freetype2" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed "$UPSTREAM_FREETYPE_BUILD/libfreetype.so.6.20.6" \
        "$BUILD_DIR/libc.so.6" -o "$BUILD_DIR/FREETYPE_UPSTREAM_TEST.ELF"
}

build_upstream_harfbuzz_program() {
    local source="$ROOT_DIR/userspace/bin/harfbuzz-upstream-test/main.c"
    local object="$BUILD_DIR/harfbuzz-upstream-test.o"
    local output="$BUILD_DIR/HARFBUZZ_UPSTREAM_TEST.ELF"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include/freetype2" \
        -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include/harfbuzz" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed \
        "$UPSTREAM_HARFBUZZ_BUILD/src/libharfbuzz-subset.so.0.61450.0" \
        "$UPSTREAM_HARFBUZZ_BUILD/src/libharfbuzz.so.0.61450.0" \
        "$UPSTREAM_FREETYPE_BUILD/libfreetype.so.6.20.6" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_fontconfig_program() {
    local source="$ROOT_DIR/userspace/bin/fontconfig-upstream-test/main.c"
    local object="$BUILD_DIR/fontconfig-upstream-test.o"
    local output="$BUILD_DIR/FONTCONFIG_UPSTREAM_TEST.ELF"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed \
        "$UPSTREAM_FONTCONFIG_BUILD/libfontconfig.so.1.17.0" \
        "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libexpat.so.1.12.5" \
        "$UPSTREAM_FREETYPE_BUILD/libfreetype.so.6.20.6" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_fribidi_program() {
    local source="$ROOT_DIR/userspace/bin/fribidi-upstream-test/main.c"
    local object="$BUILD_DIR/fribidi-upstream-test.o"
    local output="$BUILD_DIR/FRIBIDI_UPSTREAM_TEST.ELF"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed \
        "$UPSTREAM_FRIBIDI_BUILD/lib/libfribidi.so.0.4.0" \
        "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_pango_program() {
    local source="$ROOT_DIR/userspace/bin/pango-upstream-test/main.c"
    local object="$BUILD_DIR/pango-upstream-test.o"
    local output="$BUILD_DIR/PANGO_UPSTREAM_TEST.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags pangocairo pangoft2 fontconfig)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs pangocairo pangoft2 fontconfig)"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        "${pkg_libs[@]}" "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" \
        -o "$output"
}

build_upstream_graphene_program() {
    local source="$ROOT_DIR/userspace/bin/graphene-upstream-test/main.c"
    local object="$BUILD_DIR/graphene-upstream-test.o"
    local output="$BUILD_DIR/GRAPHENE_UPSTREAM_TEST.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags graphene-gobject-1.0)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs graphene-gobject-1.0)"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        "${pkg_libs[@]}" "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" \
        -o "$output"
}

build_upstream_gtk_program() {
    local source="$ROOT_DIR/userspace/bin/gtk-upstream-test/main.c"
    local wayland_source="$ROOT_DIR/userspace/bin/gtk-upstream-test/wayland-smoke.c"
    local object="$BUILD_DIR/gtk-upstream-test.o"
    local wayland_object="$BUILD_DIR/gtk-upstream-wayland-smoke.o"
    local protocol_source="$BUILD_DIR/gtk-wayland-test/xdg-shell-protocol.c"
    local protocol_header="$BUILD_DIR/gtk-wayland-test/xdg-shell-server-protocol.h"
    local protocol_object="$BUILD_DIR/gtk-xdg-shell-protocol.o"
    local protocol_xml="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
    local output="$BUILD_DIR/GTK_UPSTREAM_TEST.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local link_args=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags gtk4 wayland-client wayland-server)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs gtk4 wayland-client wayland-server)"
    for arg in "${pkg_libs[@]}"; do
        # pthread support is provided by akaOS libc, not a host compiler flag.
        [[ "$arg" == "-pthread" || "$arg" == "-lm" ]] || link_args+=("$arg")
    done
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    mkdir -p "$BUILD_DIR/gtk-wayland-test"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" server-header \
        "$protocol_xml" "$protocol_header"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" private-code \
        "$protocol_xml" "$protocol_source"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include" \
        -I"$BUILD_DIR/gtk-wayland-test" \
        -c "$wayland_source" -o "$wayland_object"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include" \
        -I"$BUILD_DIR/gtk-wayland-test" \
        -c "$protocol_source" -o "$protocol_object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" "$wayland_object" \
        "$protocol_object" \
        --no-as-needed "${link_args[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_gtk3_program() {
    local source="$ROOT_DIR/userspace/bin/gtk3-upstream-test/main.c"
    local object="$BUILD_DIR/gtk3-upstream-test.o"
    local output="$BUILD_DIR/GTK3_UPSTREAM_TEST.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local link_args=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags gtk+-3.0)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs gtk+-3.0)"
    for arg in "${pkg_libs[@]}"; do
        [[ "$arg" == "-pthread" || "$arg" == "-lm" ]] || link_args+=("$arg")
    done
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed "${link_args[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_gtk3_x11_program() {
    local source="$ROOT_DIR/userspace/bin/gtk3-upstream-test/x11-smoke.c"
    local object="$BUILD_DIR/gtk3-upstream-x11-test.o"
    local output="$BUILD_DIR/GTK3_X11_UPSTREAM_TEST.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local link_args=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags gtk+-3.0)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs gtk+-3.0)"
    for arg in "${pkg_libs[@]}"; do
        [[ "$arg" == "-pthread" || "$arg" == "-lm" ]] || link_args+=("$arg")
    done
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed "${link_args[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_x11_program() {
    local source="$ROOT_DIR/userspace/bin/x11-client/main.c"
    local object="$BUILD_DIR/x11-client.o"
    local output="$BUILD_DIR/X11_CLIENT.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local link_args=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags x11)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs x11)"
    for arg in "${pkg_libs[@]}"; do
        # pthread support is provided by akaOS libc, not a host compiler flag.
        [[ "$arg" == "-pthread" || "$arg" == "-lm" ]] || link_args+=("$arg")
    done
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed "${link_args[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_x11_wm_program() {
    local source="$ROOT_DIR/userspace/bin/x11-wm-client/main.c"
    local object="$BUILD_DIR/x11-wm-client.o"
    local output="$BUILD_DIR/X11_WM_CLIENT.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local link_args=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags x11)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs x11)"
    for arg in "${pkg_libs[@]}"; do
        [[ "$arg" == "-pthread" || "$arg" == "-lm" ]] || link_args+=("$arg")
    done
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed "${link_args[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_wayland_bridge_program() {
    local source="$ROOT_DIR/userspace/bin/wayland-bridge/main.c"
    local object="$BUILD_DIR/wayland-bridge.o"
    local protocol_dir="$BUILD_DIR/wayland-bridge-protocol"
    local protocol_xml="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
    local protocol_source="$protocol_dir/xdg-shell-protocol.c"
    local protocol_header="$protocol_dir/xdg-shell-server-protocol.h"
    local protocol_object="$protocol_dir/xdg-shell-protocol.o"
    local layer_protocol_xml="$ROOT_DIR/tools/wlr-layer-shell-unstable-v1.xml"
    local layer_protocol_source="$protocol_dir/wlr-layer-shell-protocol.c"
    local layer_protocol_header="$protocol_dir/wlr-layer-shell-server-protocol.h"
    local layer_protocol_object="$protocol_dir/wlr-layer-shell-protocol.o"
    local foreign_toplevel_protocol_xml="$ROOT_DIR/tools/wlr-foreign-toplevel-management-unstable-v1.xml"
    local foreign_toplevel_protocol_source="$protocol_dir/wlr-foreign-toplevel-management-protocol.c"
    local foreign_toplevel_protocol_header="$protocol_dir/wlr-foreign-toplevel-management-server-protocol.h"
    local foreign_toplevel_protocol_object="$protocol_dir/wlr-foreign-toplevel-management-protocol.o"
    local include_dir="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include"
    local output="$BUILD_DIR/WAYLAND_BRIDGE.ELF"
    if [[ ! -f "$protocol_xml" ]]; then
        echo "[ERROR] Wayland xdg-shell protocol XML is missing: $protocol_xml" >&2
        return 1
    fi
    if [[ ! -f "$layer_protocol_xml" ]]; then
        echo "[ERROR] Wayland layer-shell protocol XML is missing: $layer_protocol_xml" >&2
        return 1
    fi
    if [[ ! -f "$foreign_toplevel_protocol_xml" ]]; then
        echo "[ERROR] Wayland foreign-toplevel protocol XML is missing: $foreign_toplevel_protocol_xml" >&2
        return 1
    fi
    mkdir -p "$protocol_dir"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" server-header \
        "$protocol_xml" "$protocol_header"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" private-code \
        "$protocol_xml" "$protocol_source"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" server-header \
        "$layer_protocol_xml" "$layer_protocol_header"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" private-code \
        "$layer_protocol_xml" "$layer_protocol_source"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" server-header \
        "$foreign_toplevel_protocol_xml" "$foreign_toplevel_protocol_header"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" private-code \
        "$foreign_toplevel_protocol_xml" "$foreign_toplevel_protocol_source"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$include_dir" -I"$protocol_dir" \
        -c "$protocol_source" -o "$protocol_object"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$include_dir" -I"$protocol_dir" \
        -c "$layer_protocol_source" -o "$layer_protocol_object"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$include_dir" -I"$protocol_dir" \
        -c "$foreign_toplevel_protocol_source" -o "$foreign_toplevel_protocol_object"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$include_dir" -I"$protocol_dir" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" "$protocol_object" \
        "$layer_protocol_object" "$foreign_toplevel_protocol_object" \
        --no-as-needed \
        "$UPSTREAM_WAYLAND_BUILD/src/libwayland-server.so.0.26.0" \
        "$UPSTREAM_XKBCOMMON_BUILD/libxkbcommon.so.0.13.2" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_xkbcommon_program() {
    local source="$ROOT_DIR/userspace/bin/xkbcommon-upstream-test/main.c"
    local object="$BUILD_DIR/xkbcommon-upstream-test.o"
    local output="$BUILD_DIR/XKBCOMMON_UPSTREAM_TEST.ELF"
    local pkg_cflags=()
    local pkg_libs=()
    local link_args=()
    local pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_GRAPHICS_SYSROOT"
    )
    read -r -a pkg_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags xkbcommon)"
    read -r -a pkg_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs xkbcommon)"
    for arg in "${pkg_libs[@]}"; do
        [[ "$arg" == "-pthread" ]] || link_args+=("$arg")
    done
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${pkg_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$source" -o "$object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" \
        --no-as-needed "${link_args[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

build_upstream_wayland_program() {
    local source="$ROOT_DIR/userspace/bin/wayland-upstream-test/main.c"
    local object="$BUILD_DIR/wayland-upstream-test.o"
    local protocol_xml="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
    local protocol_dir="$BUILD_DIR/wayland-client-test"
    local protocol_header="$protocol_dir/xdg-shell-client-protocol.h"
    local protocol_source="$protocol_dir/xdg-shell-client-protocol.c"
    local protocol_object="$BUILD_DIR/wayland-upstream-xdg-shell.o"
    local foreign_protocol_xml="$ROOT_DIR/tools/wlr-foreign-toplevel-management-unstable-v1.xml"
    local foreign_protocol_header="$protocol_dir/wlr-foreign-toplevel-management-client-protocol.h"
    local foreign_protocol_source="$protocol_dir/wlr-foreign-toplevel-management-protocol.c"
    local foreign_protocol_object="$BUILD_DIR/wayland-upstream-foreign-toplevel.o"
    local output="$BUILD_DIR/WAYLAND_UPSTREAM_TEST.ELF"
    local include_dir="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/include"
    mkdir -p "$protocol_dir"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" client-header \
        "$protocol_xml" "$protocol_header"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" private-code \
        "$protocol_xml" "$protocol_source"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" client-header \
        "$foreign_protocol_xml" "$foreign_protocol_header"
    "$UPSTREAM_WAYLAND_HOST_PREFIX/bin/wayland-scanner" private-code \
        "$foreign_protocol_xml" "$foreign_protocol_source"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" -I"$include_dir" \
        -I"$protocol_dir" \
        -c "$source" -o "$object"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$include_dir" -I"$protocol_dir" \
        -c "$protocol_source" -o "$protocol_object"
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -I"$include_dir" -I"$protocol_dir" \
        -c "$foreign_protocol_source" -o "$foreign_protocol_object"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$object" "$protocol_object" \
        "$foreign_protocol_object" \
        --no-as-needed \
        "$UPSTREAM_WAYLAND_BUILD/src/libwayland-client.so.0.26.0" \
        "$UPSTREAM_WAYLAND_BUILD/src/libwayland-server.so.0.26.0" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" -o "$output"
}

clang --target=x86_64-unknown-none-elf \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
    -mno-red-zone -mcmodel=small \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/libc/tests/desktop_io.c" \
    -o "$BUILD_DIR/runtime-io.o"

build_user_program hello "$ROOT_DIR/userspace/bin/hello/main.c" HELLO.ELF
build_user_cpp_program cxxabi-test "$ROOT_DIR/userspace/bin/cxxabi-test/main.cpp" CXXABI_TEST.ELF
build_user_pie_program hello-pie "$ROOT_DIR/userspace/bin/hello/main.c" HELLO_PIE.ELF
build_user_program cat "$ROOT_DIR/userspace/bin/cat/main.c" CAT.ELF
build_user_program ls "$ROOT_DIR/userspace/bin/ls/main.c" LS.ELF
build_user_program grep "$ROOT_DIR/userspace/bin/grep/main.c" GREP.ELF
build_user_program head "$ROOT_DIR/userspace/bin/head/main.c" HEAD.ELF
build_user_program tail "$ROOT_DIR/userspace/bin/tail/main.c" TAIL.ELF
build_user_program wc "$ROOT_DIR/userspace/bin/wc/main.c" WC.ELF
build_user_program uname "$ROOT_DIR/userspace/bin/uname/main.c" UNAME.ELF
build_user_program akafetch "$ROOT_DIR/userspace/bin/akafetch/main.c" AKAFETCH.ELF
build_account_program whoami "$ROOT_DIR/userspace/bin/whoami/main.c" WHOAMI.ELF
build_user_program touch "$ROOT_DIR/userspace/bin/touch/main.c" TOUCH.ELF
build_user_program mkdir "$ROOT_DIR/userspace/bin/mkdir/main.c" MKDIR.ELF
build_user_program rm "$ROOT_DIR/userspace/bin/rm/main.c" RM.ELF
build_user_program cp "$ROOT_DIR/userspace/bin/cp/main.c" CP.ELF
build_user_program mv "$ROOT_DIR/userspace/bin/mv/main.c" MV.ELF
build_user_program pwd "$ROOT_DIR/userspace/bin/pwd/main.c" PWD.ELF
build_user_program echo "$ROOT_DIR/userspace/bin/echo/main.c" ECHO.ELF
build_user_program clear "$ROOT_DIR/userspace/bin/clear/main.c" CLEAR.ELF
build_user_program sync "$ROOT_DIR/userspace/bin/sync/main.c" SYNC.ELF
build_user_program chmod "$ROOT_DIR/userspace/bin/chmod/main.c" CHMOD.ELF
build_user_program chown "$ROOT_DIR/userspace/bin/chown/main.c" CHOWN.ELF
build_user_program ted "$ROOT_DIR/userspace/bin/ted/main.c" TED.ELF
build_user_program init "$ROOT_DIR/userspace/bin/init/main.c" INIT.ELF
build_account_program login "$ROOT_DIR/userspace/bin/login/main.c" LOGIN.ELF
build_account_program su "$ROOT_DIR/userspace/bin/su/main.c" SU.ELF
build_account_program passwd "$ROOT_DIR/userspace/bin/passwd/main.c" PASSWD.ELF
build_account_program sudo "$ROOT_DIR/userspace/bin/sudo/main.c" SUDO.ELF
build_user_program reboot "$ROOT_DIR/userspace/bin/reboot/main.c" REBOOT.ELF
build_user_program poweroff "$ROOT_DIR/userspace/bin/poweroff/main.c" POWEROFF.ELF
build_user_program lumen "$ROOT_DIR/userspace/bin/lumen/main.c" LUMEN.ELF
build_user_program palette "$ROOT_DIR/userspace/bin/palette/main.c" PALETTE.ELF
build_user_program aurora "$ROOT_DIR/userspace/bin/aurora/main.c" AURORA.ELF
build_user_program terminal "$ROOT_DIR/userspace/bin/terminal/main.c" TERMINAL.ELF
build_user_program notify "$ROOT_DIR/userspace/bin/notify/main.c" NOTIFY.ELF
build_user_program beep "$ROOT_DIR/userspace/bin/beep/main.c" BEEP.ELF
build_user_program ping "$ROOT_DIR/userspace/bin/ping/main.c" PING.ELF
build_user_program netstat "$ROOT_DIR/userspace/bin/netstat/main.c" NETSTAT.ELF
build_user_program tty "$ROOT_DIR/userspace/bin/tty/main.c" TTY.ELF
build_user_program stty "$ROOT_DIR/userspace/bin/stty/main.c" STTY.ELF
build_user_program ps "$ROOT_DIR/userspace/bin/ps/main.c" PS.ELF
build_account_program akash "$ROOT_DIR/userspace/bin/akash/main.c" AKASH.ELF
build_account_program bash "$ROOT_DIR/userspace/bin/bash/main.c" BASH.ELF
build_user_program nslookup "$ROOT_DIR/userspace/bin/nslookup/main.c" NSLOOKUP.ELF
build_user_program curl "$ROOT_DIR/userspace/bin/curl/main.c" CURL.ELF
build_user_program pkg "$ROOT_DIR/userspace/bin/pkg/main.c" PKG.ELF
build_user_program wget "$ROOT_DIR/userspace/bin/wget/main.c" WGET.ELF
build_user_program httpd "$ROOT_DIR/userspace/bin/httpd/main.c" HTTPD.ELF
build_user_program akaos-config "$ROOT_DIR/userspace/bin/akaos-config/main.c" AKAOS_CONFIG.ELF
build_user_program sysmon "$ROOT_DIR/userspace/bin/sysmon/main.c" SYSMON.ELF
build_user_program ip "$ROOT_DIR/userspace/bin/ip/main.c" IP.ELF
build_user_program mount "$ROOT_DIR/userspace/bin/mount/main.c" MOUNT.ELF
build_user_program umount "$ROOT_DIR/userspace/bin/umount/main.c" UMOUNT.ELF
build_user_program telnetd "$ROOT_DIR/userspace/bin/telnetd/main.c" TELNETD.ELF
build_user_program tftpd "$ROOT_DIR/userspace/bin/tftpd/main.c" TFTPD.ELF
build_user_program tftp "$ROOT_DIR/userspace/bin/tftp/main.c" TFTP.ELF
build_user_program desktop "$ROOT_DIR/userspace/bin/desktop/main.c" DESKTOP.ELF
build_user_program startlxqt "$ROOT_DIR/userspace/bin/startlxqt/main.c" STARTLXQT.ELF
build_user_program desktop-compat "$ROOT_DIR/userspace/bin/desktop-compat/main.c" DESKTOP_COMPAT.ELF
build_user_program notepad "$ROOT_DIR/userspace/bin/notepad/main.c" NOTEPAD.ELF
build_user_program taskmgr "$ROOT_DIR/userspace/bin/taskmgr/main.c" TASKMGR.ELF
build_user_program webviewer "$ROOT_DIR/userspace/bin/webviewer/main.c" WEBVIEWER.ELF
build_user_program status_cgi "$ROOT_DIR/userspace/bin/status_cgi/main.c" STATUS_CGI.ELF
build_user_program less "$ROOT_DIR/userspace/bin/less/main.c" LESS.ELF
build_user_program more "$ROOT_DIR/userspace/bin/more/main.c" MORE.ELF
build_user_program find "$ROOT_DIR/userspace/bin/find/main.c" FIND.ELF
build_user_program sort "$ROOT_DIR/userspace/bin/sort/main.c" SORT.ELF
build_user_program uniq "$ROOT_DIR/userspace/bin/uniq/main.c" UNIQ.ELF
build_user_program xargs "$ROOT_DIR/userspace/bin/xargs/main.c" XARGS.ELF
build_user_program test "$ROOT_DIR/userspace/bin/test/main.c" TEST.ELF
build_account_program sshd "$ROOT_DIR/userspace/bin/sshd/main.c" SSHD.ELF
build_account_program ssh "$ROOT_DIR/userspace/bin/ssh/main.c" SSH.ELF
build_user_program sleep "$ROOT_DIR/userspace/bin/sleep/main.c" SLEEP.ELF
build_user_program which "$ROOT_DIR/userspace/bin/which/main.c" WHICH.ELF
build_user_program env "$ROOT_DIR/userspace/bin/env/main.c" ENV.ELF
build_user_program cut "$ROOT_DIR/userspace/bin/cut/main.c" CUT.ELF
build_user_program tr "$ROOT_DIR/userspace/bin/tr/main.c" TR.ELF
build_user_program df "$ROOT_DIR/userspace/bin/df/main.c" DF.ELF
build_user_program du "$ROOT_DIR/userspace/bin/du/main.c" DU.ELF
build_user_program kill "$ROOT_DIR/userspace/bin/kill/main.c" KILL.ELF
build_user_program apropos "$ROOT_DIR/userspace/bin/apropos/main.c" APROPOS.ELF
build_user_program aspell "$ROOT_DIR/userspace/bin/aspell/main.c" ASPELL.ELF
build_user_program at "$ROOT_DIR/userspace/bin/at/main.c" AT.ELF
build_user_program awk "$ROOT_DIR/userspace/bin/awk/main.c" AWK.ELF
build_user_program basename "$ROOT_DIR/userspace/bin/basename/main.c" BASENAME.ELF
build_user_program base32 "$ROOT_DIR/userspace/bin/base32/main.c" BASE32.ELF
build_user_program base64 "$ROOT_DIR/userspace/bin/base64/main.c" BASE64.ELF
build_user_program bc "$ROOT_DIR/userspace/bin/bc/main.c" BC.ELF
build_user_program bg "$ROOT_DIR/userspace/bin/bg/main.c" BG.ELF
build_user_program fg "$ROOT_DIR/userspace/bin/fg/main.c" FG.ELF
build_user_program jobs "$ROOT_DIR/userspace/bin/jobs/main.c" JOBS.ELF
build_user_program bind "$ROOT_DIR/userspace/bin/bind/main.c" BIND.ELF
build_user_program break "$ROOT_DIR/userspace/bin/break/main.c" BREAK.ELF
build_user_program builtin "$ROOT_DIR/userspace/bin/builtin/main.c" BUILTIN.ELF
build_user_program cal "$ROOT_DIR/userspace/bin/cal/main.c" CAL.ELF
build_user_program caller "$ROOT_DIR/userspace/bin/caller/main.c" CALLER.ELF
build_user_program case "$ROOT_DIR/userspace/bin/case/main.c" CASE.ELF
build_user_program cd "$ROOT_DIR/userspace/bin/cd/main.c" CD.ELF
build_user_program cfdisk "$ROOT_DIR/userspace/bin/cfdisk/main.c" CFDISK.ELF
build_user_program chattr "$ROOT_DIR/userspace/bin/chattr/main.c" CHATTR.ELF
build_user_program chgrp "$ROOT_DIR/userspace/bin/chgrp/main.c" CHGRP.ELF
build_user_program chpasswd "$ROOT_DIR/userspace/bin/chpasswd/main.c" CHPASSWD.ELF
build_user_program chroot "$ROOT_DIR/userspace/bin/chroot/main.c" CHROOT.ELF
build_user_program chkconfig "$ROOT_DIR/userspace/bin/chkconfig/main.c" CHKCONFIG.ELF
build_user_program cksum "$ROOT_DIR/userspace/bin/cksum/main.c" CKSUM.ELF
build_user_program xmrig "$ROOT_DIR/userspace/bin/xmrig/main.c" XMRIG.ELF
build_user_program hyprland "$ROOT_DIR/userspace/bin/hyprland/main.c" HYPRLAND.ELF
build_user_program hyprctl "$ROOT_DIR/userspace/bin/hyprctl/main.c" HYPRCTL.ELF
build_user_program pkill "$ROOT_DIR/userspace/bin/pkill/main.c" PKILL.ELF
build_user_program uptime "$ROOT_DIR/userspace/bin/uptime/main.c" UPTIME.ELF
build_user_program free "$ROOT_DIR/userspace/bin/free/main.c" FREE.ELF
build_user_program watch "$ROOT_DIR/userspace/bin/watch/main.c" WATCH.ELF
build_user_program sed "$ROOT_DIR/userspace/bin/sed/main.c" SED.ELF
build_user_program diff "$ROOT_DIR/userspace/bin/diff/main.c" DIFF.ELF
build_user_program cmp "$ROOT_DIR/userspace/bin/cmp/main.c" CMP.ELF
build_user_program patch "$ROOT_DIR/userspace/bin/patch/main.c" PATCH.ELF
build_user_program tar "$ROOT_DIR/userspace/bin/tar/main.c" TAR.ELF
build_user_program gzip "$ROOT_DIR/userspace/bin/gzip/main.c" GZIP.ELF
build_user_program gunzip "$ROOT_DIR/userspace/bin/gunzip/main.c" GUNZIP.ELF
build_bzip2
build_user_program route "$ROOT_DIR/userspace/bin/route/main.c" ROUTE.ELF
build_user_program traceroute "$ROOT_DIR/userspace/bin/traceroute/main.c" TRACEROUTE.ELF
build_user_program dig "$ROOT_DIR/userspace/bin/dig/main.c" DIG.ELF
build_user_program arp "$ROOT_DIR/userspace/bin/arp/main.c" ARP.ELF
build_user_program lscpu "$ROOT_DIR/userspace/bin/lscpu/main.c" LSCPU.ELF
build_user_program lspci "$ROOT_DIR/userspace/bin/lspci/main.c" LSPCI.ELF
build_user_program gpuinfo "$ROOT_DIR/userspace/bin/gpuinfo/main.c" GPUINFO.ELF
build_user_program sndinfo "$ROOT_DIR/userspace/bin/sndinfo/main.c" SNDINFO.ELF
build_user_program acpi "$ROOT_DIR/userspace/bin/acpi/main.c" ACPI.ELF
build_user_program xsdisplay "$ROOT_DIR/userspace/bin/xsdisplay/main.c" XSDISPLAY.ELF
build_user_program dbus-daemon "$ROOT_DIR/userspace/bin/dbus-daemon/main.c" DBUS_DAEMON.ELF
build_user_program dltest "$ROOT_DIR/userspace/bin/dltest/main.c" DLTEST.ELF
build_user_program display-protocols "$ROOT_DIR/userspace/bin/display-protocols/main.c" DISPLAY_PROTOCOLS.ELF
build_xserver
build_user_program ld-akaos "$ROOT_DIR/userspace/bin/ld-akaos/main.c" LD_AKAOS.ELF
build_shared_object libakaos-demo.so
build_shared_libc libc.so.6
build_shared_cxx_runtime
build_shared_libm libm.so.6
build_dynamic_cpp_program "$ROOT_DIR/userspace/bin/cxx-dynamic-test/main.cpp" CXX_DYNAMIC_TEST.ELF
build_cxx_shared_object libakaos-cxx.so
build_static_shared_object libakaos-static.so
build_versioned_shared_object libakaos-versioned.so
build_dependent_shared_object libakaos-dependent.so
build_loader_fanout_objects
build_runtime_library libpthread.so 1 "$ROOT_DIR/userspace/libc/tests/desktop_runtime_thread.c"
build_dynamic_program dynamic-hello "$ROOT_DIR/userspace/bin/dynamic-hello/main.c" DYNAMIC_HELLO.ELF

UPSTREAM_GLIB_ENABLED=0
UPSTREAM_GRAPHENE_ENABLED=0
UPSTREAM_FREETYPE_ENABLED=0
UPSTREAM_CAIRO_ENABLED=0
UPSTREAM_CAIRO_FREETYPE_ENABLED=0
UPSTREAM_HARFBUZZ_ENABLED=0
UPSTREAM_FONTCONFIG_ENABLED=0
UPSTREAM_FRIBIDI_ENABLED=0
UPSTREAM_PANGO_ENABLED=0
UPSTREAM_WAYLAND_ENABLED=0
UPSTREAM_WAYLAND_PROTOCOLS_ENABLED=0
UPSTREAM_XKBCOMMON_ENABLED=0
UPSTREAM_WAYLAND_BRIDGE_ENABLED=0
UPSTREAM_GTK_ENABLED=0
UPSTREAM_GTK3_ENABLED=0
UPSTREAM_X11_ENABLED=0
UPSTREAM_QT_ENABLED=0

UPSTREAM_GLIB_INITRAMFS_ARGS=()
UPSTREAM_HARFBUZZ_INITRAMFS_ARGS=()
UPSTREAM_FONTCONFIG_INITRAMFS_ARGS=()
UPSTREAM_FRIBIDI_INITRAMFS_ARGS=()
UPSTREAM_PANGO_INITRAMFS_ARGS=()
UPSTREAM_GRAPHENE_INITRAMFS_ARGS=()
UPSTREAM_GTK_INITRAMFS_ARGS=()
UPSTREAM_GTK3_INITRAMFS_ARGS=()
UPSTREAM_WAYLAND_INITRAMFS_ARGS=()
UPSTREAM_QT_INITRAMFS_ARGS=()
UPSTREAM_LXQT_INITRAMFS_ARGS=()
UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS=()
UPSTREAM_DBUS_INITRAMFS_ARGS=()
UPSTREAM_QT_QUICK_INITRAMFS_ARGS=()
UPSTREAM_XKBCOMMON_INITRAMFS_ARGS=()
UPSTREAM_XKB_DATA_INITRAMFS_ARGS=()
UPSTREAM_CAIRO_INITRAMFS_ARGS=()
UPSTREAM_OPENBOX_INITRAMFS_ARGS=()
UPSTREAM_LXPANEL_INITRAMFS_ARGS=()
UPSTREAM_FREETYPE_INITRAMFS_ARGS=()
UPSTREAM_X11_INITRAMFS_ARGS=()
GLIB_SONAME_IMAGE="$BUILD_DIR/libglib.so"
GOBJECT_SONAME_IMAGE="$BUILD_DIR/libglib.so"
GIO_SONAME_IMAGE="$BUILD_DIR/libgio.so"

# Determine if upstream graphical desktop libraries (Wayland, GTK, Qt, Cairo, X11) are needed.
# If desktop is 'none' and software bundle is 'dev', 'net', or 'minimal', skip heavy upstream GUI compilation!
need_upstream_gui=1
if [[ "$menu_software" == "apps" ]]; then
    need_upstream_gui=0
    echo "[INFO] 'apps' software bundle selected (all applications without heavy upstream GUI libraries). Skipping heavy upstream GUI toolkits (Wayland, GTK, Qt, Cairo)."
elif [[ "$target_desktop" == "none" ]]; then
    if [[ "$menu_software" == "dev" || "$menu_software" == "net" || "$menu_software" == "minimal" || "$menu_software" == "none" ]]; then
        need_upstream_gui=0
        echo "[INFO] Headless/console profile selected ($menu_software bundle with no desktop). Skipping heavy upstream GUI toolkits (Wayland, GTK, Qt, Cairo)."
    fi
fi

if [[ "$need_upstream_gui" -eq 1 ]]; then
UPSTREAM_GLIB_ENABLED=0
if [[ -f "$UPSTREAM_GLIB_BUILD/gio/libgio-2.0.so.0.9000.0" &&
      -f "$UPSTREAM_GLIB_BUILD/gobject/libgobject-2.0.so.0.9000.0" &&
      -f "$UPSTREAM_GLIB_BUILD/gmodule/libgmodule-2.0.so.0.9000.0" &&
      -f "$UPSTREAM_GLIB_BUILD/glib/libglib-2.0.so.0.9000.0" &&
      -f "$UPSTREAM_GLIB_BUILD/subprojects/libffi-3.5.2/src/libffi.so.8.2.0" &&
      -f "$UPSTREAM_GLIB_BUILD/subprojects/proxy-libintl-0.5/libintl.so.8" &&
      -f "$UPSTREAM_GLIB_BUILD/subprojects/zlib-1.3.1/libz.so.1.3.1" &&
      -f "$ROOT_DIR/build/upstream-sysroot/pcre2-10.49-x86_64/usr/lib/libpcre2-8.so.0.16.1" &&
      -f "$UPSTREAM_GLIB_BUILD/build.ninja" &&
      -f "$UPSTREAM_GLIB_BUILD/meson-private/coredata.dat" ]]; then
    if [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" != "1" ]] &&
       ! command -v meson >/dev/null 2>&1; then
        echo "[ERROR] Meson is required to refresh the available upstream GLib build; run ./build.sh --check-deps" >&2
        exit 1
    fi
    echo "[BUILD] Refreshing upstream GLib/GObject/GIO against the current akaOS libc"
    run_upstream_refresh "GLib/GObject/GIO" \
        meson compile -C "$UPSTREAM_GLIB_BUILD" --jobs "$AKAOS_BUILD_JOBS"
    echo "[BUILD] Upstream GLib/GObject/GIO smoke application"
    build_upstream_glib_program
    UPSTREAM_GLIB_ENABLED=1
else
    echo "[INFO] Upstream GLib build artifacts are not complete; keeping akaOS compatibility libraries"
fi

UPSTREAM_GRAPHENE_ENABLED=0
if [[ "$UPSTREAM_GLIB_ENABLED" -eq 1 ]]; then
    echo "[BUILD] Refreshing upstream Graphene geometry library"
    run_upstream_refresh "Graphene" \
        bash "$ROOT_DIR/tools/build-upstream-graphene.sh"
    echo "[BUILD] Upstream Graphene vector/matrix ABI smoke application"
    build_upstream_graphene_program
    UPSTREAM_GRAPHENE_ENABLED=1
else
    echo "[INFO] Upstream Graphene requires the upstream GLib/GObject build"
fi

UPSTREAM_FREETYPE_ENABLED=0
if [[ -f "$UPSTREAM_FREETYPE_BUILD/build.ninja" &&
      -f "$UPSTREAM_FREETYPE_BUILD/meson-private/coredata.dat" &&
      -f "$UPSTREAM_FREETYPE_BUILD/libfreetype.so.6.20.6" ]]; then
    if [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" != "1" ]] &&
       ! command -v meson >/dev/null 2>&1; then
        echo "[ERROR] Meson is required to refresh the available upstream FreeType build; run ./build.sh --check-deps" >&2
        exit 1
    fi
    echo "[BUILD] Refreshing upstream FreeType against the current akaOS libc/libm"
    run_upstream_refresh "FreeType" \
        meson compile -C "$UPSTREAM_FREETYPE_BUILD" --jobs "$AKAOS_BUILD_JOBS"
    DESTDIR="$BUILD_DIR/upstream-sysroot/freetype-2.14.3-x86_64" \
        run_upstream_refresh "FreeType target install" \
        meson install -C "$UPSTREAM_FREETYPE_BUILD"
    DESTDIR="$UPSTREAM_GRAPHICS_SYSROOT" \
        run_upstream_refresh "FreeType graphics install" \
        meson install -C "$UPSTREAM_FREETYPE_BUILD"
    echo "[BUILD] Upstream FreeType glyph smoke application"
    build_upstream_freetype_program
    UPSTREAM_FREETYPE_ENABLED=1
else
    echo "[INFO] Upstream FreeType build artifacts are not complete"
fi

UPSTREAM_CAIRO_ENABLED=0
UPSTREAM_CAIRO_FREETYPE_ENABLED=0
UPSTREAM_CAIRO_BUILD="$UPSTREAM_CAIRO_IMAGE_BUILD"
if [[ "$UPSTREAM_FREETYPE_ENABLED" -eq 1 &&
      -f "$UPSTREAM_CAIRO_FREETYPE_BUILD/build.ninja" &&
      -f "$UPSTREAM_CAIRO_FREETYPE_BUILD/meson-private/coredata.dat" &&
      -f "$UPSTREAM_CAIRO_FREETYPE_BUILD/src/libcairo.so.2.11806.6" ]]; then
    UPSTREAM_CAIRO_BUILD="$UPSTREAM_CAIRO_FREETYPE_BUILD"
    UPSTREAM_CAIRO_FREETYPE_ENABLED=1
fi
if [[ -f "$UPSTREAM_PIXMAN_BUILD/build.ninja" &&
      -f "$UPSTREAM_PIXMAN_BUILD/meson-private/coredata.dat" &&
      -f "$UPSTREAM_PIXMAN_BUILD/pixman/libpixman-1.so.0.46.4" &&
      -f "$UPSTREAM_CAIRO_BUILD/build.ninja" &&
      -f "$UPSTREAM_CAIRO_BUILD/meson-private/coredata.dat" &&
      -f "$UPSTREAM_CAIRO_BUILD/src/libcairo.so.2.11806.6" ]]; then
    if [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" != "1" ]] &&
       ! command -v meson >/dev/null 2>&1; then
        echo "[ERROR] Meson is required to refresh the available upstream Cairo build; run ./build.sh --check-deps" >&2
        exit 1
    fi
    echo "[BUILD] Refreshing upstream Pixman and Cairo against the current akaOS libc/libm"
    run_upstream_refresh "Pixman" \
        meson compile -C "$UPSTREAM_PIXMAN_BUILD" --jobs "$AKAOS_BUILD_JOBS"
    DESTDIR="$BUILD_DIR/upstream-sysroot/pixman-0.46.4-x86_64" \
        run_upstream_refresh "Pixman target install" \
        meson install -C "$UPSTREAM_PIXMAN_BUILD"
    if [[ "$UPSTREAM_CAIRO_FREETYPE_ENABLED" -eq 1 ]]; then
        DESTDIR="$UPSTREAM_GRAPHICS_SYSROOT" \
            run_upstream_refresh "Pixman graphics install" \
            meson install -C "$UPSTREAM_PIXMAN_BUILD"
    fi
    run_upstream_refresh "Cairo" \
        meson compile -C "$UPSTREAM_CAIRO_BUILD" --jobs "$AKAOS_BUILD_JOBS"
    echo "[BUILD] Upstream Cairo/Pixman smoke application"
    build_upstream_cairo_program
    UPSTREAM_CAIRO_ENABLED=1
else
    echo "[INFO] Upstream Cairo/Pixman build artifacts are not complete"
fi

UPSTREAM_HARFBUZZ_ENABLED=0
if [[ "$UPSTREAM_FREETYPE_ENABLED" -eq 1 &&
      -f "$UPSTREAM_HARFBUZZ_BUILD/build.ninja" &&
      -f "$UPSTREAM_HARFBUZZ_BUILD/meson-private/coredata.dat" &&
      -f "$UPSTREAM_HARFBUZZ_BUILD/src/libharfbuzz.so.0.61450.0" ]]; then
    if [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" != "1" ]] &&
       ! command -v meson >/dev/null 2>&1; then
        echo "[ERROR] Meson is required to refresh the available upstream HarfBuzz build; run ./build.sh --check-deps" >&2
        exit 1
    fi
    echo "[BUILD] Refreshing upstream HarfBuzz against akaOS FreeType and libc++"
    run_upstream_refresh "HarfBuzz" \
        meson compile -C "$UPSTREAM_HARFBUZZ_BUILD" --jobs "$AKAOS_BUILD_JOBS"
    DESTDIR="$UPSTREAM_GRAPHICS_SYSROOT" \
        run_upstream_refresh "HarfBuzz install" \
        meson install -C "$UPSTREAM_HARFBUZZ_BUILD"
    echo "[BUILD] Upstream HarfBuzz/FreeType shaping smoke application"
    build_upstream_harfbuzz_program
    UPSTREAM_HARFBUZZ_ENABLED=1
else
    echo "[INFO] Upstream HarfBuzz build artifacts are not complete"
fi

UPSTREAM_FONTCONFIG_ENABLED=0
if [[ -f "$UPSTREAM_FONTCONFIG_BUILD/build.ninja" &&
      -f "$UPSTREAM_FONTCONFIG_BUILD/meson-private/coredata.dat" &&
      -f "$UPSTREAM_FONTCONFIG_BUILD/libfontconfig.so.1.17.0" &&
      -f "$UPSTREAM_EXPAT_BUILD/libexpat.so.1.12.5" ]]; then
    echo "[BUILD] Refreshing upstream Fontconfig/Expat against akaOS FreeType and libc"
    run_upstream_refresh "Fontconfig and Expat" \
        bash "$ROOT_DIR/tools/build-upstream-fontconfig.sh"
    echo "[BUILD] Upstream Fontconfig font-discovery smoke application"
    build_upstream_fontconfig_program
    UPSTREAM_FONTCONFIG_ENABLED=1
else
    echo "[INFO] Upstream Fontconfig build artifacts are not complete"
fi

if [[ "$UPSTREAM_FREETYPE_ENABLED" -eq 1 && "$UPSTREAM_FONTCONFIG_ENABLED" -eq 1 ]]; then
    echo "[BUILD] Enabling upstream Cairo Fontconfig/FreeType font backend for Pango"
    run_upstream_refresh "Cairo Fontconfig/FreeType backend" \
        bash "$ROOT_DIR/tools/build-upstream-cairo-freetype.sh"
    UPSTREAM_CAIRO_BUILD="$UPSTREAM_CAIRO_FREETYPE_BUILD"
    UPSTREAM_CAIRO_FREETYPE_ENABLED=1
    UPSTREAM_CAIRO_ENABLED=1
    echo "[BUILD] Refreshing upstream Cairo glyph/rendering smoke application"
    build_upstream_cairo_program
fi

UPSTREAM_FRIBIDI_ENABLED=0
if [[ -f "$UPSTREAM_FRIBIDI_BUILD/build.ninja" &&
      -f "$UPSTREAM_FRIBIDI_BUILD/meson-private/coredata.dat" &&
      -f "$UPSTREAM_FRIBIDI_BUILD/lib/libfribidi.so.0.4.0" ]]; then
    echo "[BUILD] Refreshing upstream FriBidi against the current akaOS libc"
    run_upstream_refresh "FriBidi" \
        bash "$ROOT_DIR/tools/build-upstream-fribidi.sh"
    echo "[BUILD] Upstream FriBidi mixed-script bidi smoke application"
    build_upstream_fribidi_program
    UPSTREAM_FRIBIDI_ENABLED=1
else
    echo "[INFO] Upstream FriBidi build artifacts are not complete"
fi

UPSTREAM_PANGO_ENABLED=0
if [[ "$UPSTREAM_GLIB_ENABLED" -eq 1 &&
      "$UPSTREAM_CAIRO_FREETYPE_ENABLED" -eq 1 &&
      "$UPSTREAM_FONTCONFIG_ENABLED" -eq 1 &&
      "$UPSTREAM_FRIBIDI_ENABLED" -eq 1 &&
      "$UPSTREAM_HARFBUZZ_ENABLED" -eq 1 &&
      "$UPSTREAM_FREETYPE_ENABLED" -eq 1 ]]; then
    echo "[BUILD] Refreshing upstream Pango against the current akaOS text stack"
    run_upstream_refresh "Pango" \
        bash "$ROOT_DIR/tools/build-upstream-pango.sh"
    echo "[BUILD] Upstream Pango layout/rendering smoke application"
    build_upstream_pango_program
    UPSTREAM_PANGO_ENABLED=1
else
    echo "[INFO] Upstream Pango build artifacts are not complete"
fi

UPSTREAM_WAYLAND_ENABLED=0
if [[ -f "$UPSTREAM_WAYLAND_BUILD/src/libwayland-client.so.0.26.0" &&
      -f "$UPSTREAM_WAYLAND_BUILD/src/libwayland-server.so.0.26.0" &&
      -f "$UPSTREAM_WAYLAND_BUILD/cursor/libwayland-cursor.so.0.26.0" &&
      -f "$UPSTREAM_WAYLAND_BUILD/egl/libwayland-egl.so.1.26.0" ]]; then
    run_upstream_refresh "Wayland 1.26.0" \
        bash "$ROOT_DIR/tools/build-upstream-wayland.sh"
    echo "[BUILD] Upstream Wayland client/server and SHM protocol smoke application"
    build_upstream_wayland_program
    UPSTREAM_WAYLAND_ENABLED=1
else
    echo "[BUILD] Upstream Wayland 1.26.0 prerequisites"
    bash "$ROOT_DIR/tools/build-upstream-wayland.sh"
    echo "[BUILD] Upstream Wayland client/server and SHM protocol smoke application"
    build_upstream_wayland_program
    UPSTREAM_WAYLAND_ENABLED=1
fi

UPSTREAM_WAYLAND_PROTOCOLS_ENABLED=0
if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/pkgconfig/wayland-protocols.pc" &&
      -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" ]]; then
    run_upstream_refresh "Wayland protocols 1.46" \
        bash "$ROOT_DIR/tools/build-upstream-wayland-protocols.sh"
else
    echo "[BUILD] Upstream Wayland protocol definitions required by GTK's native Wayland backend"
    bash "$ROOT_DIR/tools/build-upstream-wayland-protocols.sh"
fi
UPSTREAM_WAYLAND_PROTOCOLS_ENABLED=1

UPSTREAM_XKBCOMMON_ENABLED=0
if [[ -f "$UPSTREAM_XKBCOMMON_BUILD/libxkbcommon.so.0.13.2" &&
      -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/xkbcommon.pc" ]]; then
    run_upstream_refresh "xkbcommon 1.13.2" \
        bash "$ROOT_DIR/tools/build-upstream-xkbcommon.sh"
else
    echo "[BUILD] Upstream xkbcommon keyboard keymap library"
    bash "$ROOT_DIR/tools/build-upstream-xkbcommon.sh"
fi
echo "[BUILD] Upstream xkbcommon keymap/modifier smoke application"
build_upstream_xkbcommon_program
UPSTREAM_XKBCOMMON_ENABLED=1

UPSTREAM_WAYLAND_BRIDGE_ENABLED=0
if [[ "$UPSTREAM_WAYLAND_ENABLED" -eq 1 &&
      "$UPSTREAM_WAYLAND_PROTOCOLS_ENABLED" -eq 1 &&
      "$UPSTREAM_XKBCOMMON_ENABLED" -eq 1 ]]; then
    echo "[BUILD] Lumen's upstream Wayland-to-framebuffer bridge"
    build_upstream_wayland_bridge_program
    UPSTREAM_WAYLAND_BRIDGE_ENABLED=1
fi

UPSTREAM_GTK_ENABLED=0
if [[ "$UPSTREAM_PANGO_ENABLED" -eq 1 &&
      "$UPSTREAM_WAYLAND_ENABLED" -eq 1 &&
      "$UPSTREAM_WAYLAND_PROTOCOLS_ENABLED" -eq 1 &&
      "$UPSTREAM_XKBCOMMON_ENABLED" -eq 1 ]]; then
    echo "[BUILD] Upstream GTK 4 with its native Wayland backend"
    if [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" == "1" &&
          -f "$UPSTREAM_GTK_BUILD/.akaos-wayland-enabled" ]]; then
        run_upstream_refresh "GTK 4.22.5 Wayland backend" \
            env AKAOS_GTK_BUILD_JOBS="${AKAOS_GTK_BUILD_JOBS:-4}" \
            bash "$ROOT_DIR/tools/build-upstream-gtk4.sh"
    else
        env AKAOS_GTK_BUILD_JOBS="${AKAOS_GTK_BUILD_JOBS:-4}" \
            bash "$ROOT_DIR/tools/build-upstream-gtk4.sh"
    fi
    echo "[BUILD] Upstream GTK 4 library/GObject smoke application"
    build_upstream_gtk_program
    UPSTREAM_GTK_ENABLED=1
else
    echo "[INFO] GTK Wayland backend prerequisites are not complete"
fi

UPSTREAM_GTK3_ENABLED=0
if [[ "$UPSTREAM_GTK_ENABLED" -eq 1 ]]; then
    echo "[BUILD] Upstream GTK 3.24.52 Wayland backend for LXDE compatibility"
    if [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" != "1" ||
          ! -f "$UPSTREAM_GTK3_BUILD/.akaos-wayland-enabled" ]]; then
        env AKAOS_GTK3_BUILD_JOBS="${AKAOS_GTK3_BUILD_JOBS:-4}" \
            bash "$ROOT_DIR/tools/build-upstream-gtk3.sh"
    else
        echo "[SKIP] Upstream GTK 3.24.52 refresh (reusing installed build artifacts)"
    fi
    echo "[BUILD] GTK 3 upstream Wayland window/layout smoke application"
    build_upstream_gtk3_program
    echo "[BUILD] GTK 3 upstream X11 window/layout smoke application"
    build_upstream_gtk3_x11_program
    UPSTREAM_GTK3_ENABLED=1
fi

UPSTREAM_X11_ENABLED=0
if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libX11.so.6" ]]; then
    echo "[BUILD] Upstream X11 client smoke application"
    build_upstream_x11_program
    echo "[BUILD] Upstream X11 window manager takeover smoke application"
    build_upstream_x11_wm_program
    UPSTREAM_X11_ENABLED=1
else
    echo "[INFO] Upstream X11 client libraries are not built"
fi

# The upstream Openbox window manager is the window manager of the LXDE
# desktop environment.
if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libX11.so.6" ]]; then
    echo "[BUILD] Upstream Openbox window manager"
    bash "$ROOT_DIR/tools/build-upstream-openbox.sh"
fi

fi
build_runtime_library libglib.so 2
build_runtime_library libgio.so 3
build_runtime_library libgtk.so 4
build_runtime_library libgtk-4.so 5
build_runtime_library libQt6Core.so 6
build_runtime_library libQt6Gui.so 7
build_runtime_library libQt6Widgets.so 8
for image in HELLO.ELF HELLO_PIE.ELF CAT.ELF LS.ELF GREP.ELF HEAD.ELF TAIL.ELF WC.ELF UNAME.ELF AKAFETCH.ELF WHOAMI.ELF TOUCH.ELF MKDIR.ELF RM.ELF CP.ELF MV.ELF PWD.ELF ECHO.ELF CLEAR.ELF SYNC.ELF CHMOD.ELF CHOWN.ELF TED.ELF INIT.ELF LOGIN.ELF SU.ELF PASSWD.ELF REBOOT.ELF POWEROFF.ELF LUMEN.ELF PALETTE.ELF AURORA.ELF TERMINAL.ELF BEEP.ELF PING.ELF NETSTAT.ELF TTY.ELF STTY.ELF PS.ELF AKASH.ELF NSLOOKUP.ELF CURL.ELF PKG.ELF WGET.ELF HTTPD.ELF AKAOS_CONFIG.ELF SYSMON.ELF IP.ELF MOUNT.ELF UMOUNT.ELF TELNETD.ELF TFTPD.ELF TFTP.ELF DESKTOP.ELF DESKTOP_COMPAT.ELF NOTEPAD.ELF TASKMGR.ELF WEBVIEWER.ELF STATUS_CGI.ELF LESS.ELF MORE.ELF FIND.ELF SORT.ELF UNIQ.ELF XARGS.ELF TEST.ELF SSHD.ELF SSH.ELF SLEEP.ELF WHICH.ELF ENV.ELF CUT.ELF TR.ELF DF.ELF DU.ELF KILL.ELF PKILL.ELF UPTIME.ELF FREE.ELF WATCH.ELF SED.ELF DIFF.ELF CMP.ELF PATCH.ELF TAR.ELF GZIP.ELF GUNZIP.ELF ROUTE.ELF TRACEROUTE.ELF DIG.ELF ARP.ELF LSCPU.ELF LSPCI.ELF GPUINFO.ELF SNDINFO.ELF ACPI.ELF DLTEST.ELF DISPLAY_PROTOCOLS.ELF; do
    # ET_DYN hello-pie legitimately carries relative relocations; the kernel
    # applies those at its fixed native load base. Reject only relocation
    # kinds outside that bounded ABI instead of rejecting the section itself.
    if readelf -rW "$BUILD_DIR/$image" |
        awk '$1 ~ /^[0-9A-Fa-f]+$/ && $3 != "R_X86_64_RELATIVE" { bad = 1 }
             END { exit bad }'; then
        :
    else
        echo "Userspace image contains unsupported relocations: $image" >&2
        exit 1
    fi
done

GLIB_SONAME_IMAGE="$BUILD_DIR/libglib.so"
GOBJECT_SONAME_IMAGE="$BUILD_DIR/libglib.so"
GIO_SONAME_IMAGE="$BUILD_DIR/libgio.so"
UPSTREAM_GLIB_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_GLIB_ENABLED" -eq 1 ]]; then
    GLIB_SONAME_IMAGE="$UPSTREAM_GLIB_BUILD/glib/libglib-2.0.so.0.9000.0"
    GOBJECT_SONAME_IMAGE="$UPSTREAM_GLIB_BUILD/gobject/libgobject-2.0.so.0.9000.0"
    GIO_SONAME_IMAGE="$UPSTREAM_GLIB_BUILD/gio/libgio-2.0.so.0.9000.0"
    UPSTREAM_GLIB_INITRAMFS_ARGS=(
        --file "/bin/glib-upstream-test:$BUILD_DIR/GLIB_UPSTREAM_TEST.ELF:exec"
        --data "/lib/.upstream-glib:enabled:0444"
        --file "/lib/libgmodule-2.0.so.0:$UPSTREAM_GLIB_BUILD/gmodule/libgmodule-2.0.so.0.9000.0:0444"
        --file "/lib/libgthread-2.0.so.0:$UPSTREAM_GLIB_BUILD/gthread/libgthread-2.0.so.0.9000.0:0444"
        --file "/lib/libffi.so.8:$UPSTREAM_GLIB_BUILD/subprojects/libffi-3.5.2/src/libffi.so.8.2.0:0444"
        --file "/lib/libintl.so.8:$UPSTREAM_GLIB_BUILD/subprojects/proxy-libintl-0.5/libintl.so.8:0444"
        --file "/lib/libz.so.1:$UPSTREAM_GLIB_BUILD/subprojects/zlib-1.3.1/libz.so.1.3.1:0444"
        --file "/lib/libpcre2-8.so.0:$ROOT_DIR/build/upstream-sysroot/pcre2-10.49-x86_64/usr/lib/libpcre2-8.so.0.16.1:0444"
    )
fi
UPSTREAM_HARFBUZZ_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_HARFBUZZ_ENABLED" -eq 1 ]]; then
    UPSTREAM_HARFBUZZ_INITRAMFS_ARGS=(
        --file "/bin/harfbuzz-upstream-test:$BUILD_DIR/HARFBUZZ_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libharfbuzz.so.0:$UPSTREAM_HARFBUZZ_BUILD/src/libharfbuzz.so.0.61450.0:0444"
        --file "/lib/libharfbuzz-subset.so.0:$UPSTREAM_HARFBUZZ_BUILD/src/libharfbuzz-subset.so.0.61450.0:0444"
    )
fi
UPSTREAM_FONTCONFIG_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_FONTCONFIG_ENABLED" -eq 1 ]]; then
    FONTCONFIG_DTD="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/xml/fontconfig/fonts.dtd"
    UPSTREAM_FONTCONFIG_INITRAMFS_ARGS=(
        --file "/bin/fontconfig-upstream-test:$BUILD_DIR/FONTCONFIG_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libfontconfig.so.1:$UPSTREAM_FONTCONFIG_BUILD/libfontconfig.so.1.17.0:0444"
        --file "/lib/libexpat.so.1:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libexpat.so.1.12.5:0444"
        --dir "/etc/fonts"
        --file "/etc/fonts/fonts.conf:$UPSTREAM_GRAPHICS_SYSROOT/etc/fonts/fonts.conf:0444"
        --dir "/etc/fonts/conf.d"
        --dir "/usr/share/xml"
        --dir "/usr/share/xml/fontconfig"
        --file "/usr/share/xml/fontconfig/fonts.dtd:$FONTCONFIG_DTD:0444"
        --dir "/usr/local"
        --dir "/usr/local/share"
        --dir "/usr/local/share/xml"
        --dir "/usr/local/share/xml/fontconfig"
        --file "/usr/local/share/xml/fontconfig/fonts.dtd:$FONTCONFIG_DTD:0444"
        --dir "/tmp/fontconfig-cache"
    )
    for config_link in "$UPSTREAM_GRAPHICS_SYSROOT"/etc/fonts/conf.d/*.conf; do
        [[ -e "$config_link" ]] || continue
        config_target="$(readlink -f "$config_link")"
        config_name="${config_link##*/}"
        UPSTREAM_FONTCONFIG_INITRAMFS_ARGS+=(
            --file "/etc/fonts/conf.d/$config_name:$config_target:0444"
        )
    done
fi
UPSTREAM_FRIBIDI_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_FRIBIDI_ENABLED" -eq 1 ]]; then
    UPSTREAM_FRIBIDI_INITRAMFS_ARGS=(
        --file "/bin/fribidi-upstream-test:$BUILD_DIR/FRIBIDI_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libfribidi.so.0:$UPSTREAM_FRIBIDI_BUILD/lib/libfribidi.so.0.4.0:0444"
        --dir "/usr/share/licenses/FriBidi"
        --file "/usr/share/licenses/FriBidi/COPYING:$UPSTREAM_FRIBIDI_SOURCE/COPYING:0444"
    )
fi
UPSTREAM_PANGO_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_PANGO_ENABLED" -eq 1 ]]; then
    # The libraries are staged from the sysroot they were installed into, so
    # the image does not depend on where the build tree happens to live.
    UPSTREAM_PANGO_LIBPANGO="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libpango-1.0.so.0.*' -print | sort -V | tail -n 1)"
    UPSTREAM_PANGO_LIBPANGOFT2="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libpangoft2-1.0.so.0.*' -print | sort -V | tail -n 1)"
    UPSTREAM_PANGO_LIBPANGOCAIRO="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libpangocairo-1.0.so.0.*' -print | sort -V | tail -n 1)"
    if [[ -z "$UPSTREAM_PANGO_LIBPANGO" || -z "$UPSTREAM_PANGO_LIBPANGOFT2" ||
          -z "$UPSTREAM_PANGO_LIBPANGOCAIRO" ]]; then
        echo "[ERROR] Upstream Pango shared libraries are missing from the graphics sysroot" >&2
        exit 1
    fi
    UPSTREAM_PANGO_INITRAMFS_ARGS=(
        --file "/bin/pango-upstream-test:$BUILD_DIR/PANGO_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libpango-1.0.so.0:$UPSTREAM_PANGO_LIBPANGO:0444"
        --file "/lib/libpangoft2-1.0.so.0:$UPSTREAM_PANGO_LIBPANGOFT2:0444"
        --file "/lib/libpangocairo-1.0.so.0:$UPSTREAM_PANGO_LIBPANGOCAIRO:0444"
        --dir "/usr/share/licenses/Pango"
        --file "/usr/share/licenses/Pango/COPYING:$UPSTREAM_PANGO_SOURCE/COPYING:0444"
    )
fi
UPSTREAM_GRAPHENE_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_GRAPHENE_ENABLED" -eq 1 ]]; then
    UPSTREAM_GRAPHENE_INITRAMFS_ARGS=(
        --file "/bin/graphene-upstream-test:$BUILD_DIR/GRAPHENE_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libgraphene-1.0.so.0:$UPSTREAM_GRAPHENE_BUILD/src/libgraphene-1.0.so.0.1000.8:0444"
        --dir "/usr/share/licenses/Graphene"
        --file "/usr/share/licenses/Graphene/LICENSE.txt:$UPSTREAM_GRAPHENE_SOURCE/LICENSE.txt:0444"
    )
fi
UPSTREAM_GTK_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_GTK_ENABLED" -eq 1 ]]; then
    UPSTREAM_GTK_INITRAMFS_ARGS=(
        --file "/bin/gtk-upstream-test:$BUILD_DIR/GTK_UPSTREAM_TEST.ELF:exec"
        --dir "/lib/.upstream-gtk"
        --file "/lib/.upstream-gtk/libgtk-4.so.1:$UPSTREAM_GTK_BUILD/gtk/libgtk-4.so.1.2200.5:0444"
        --file "/lib/libgdk_pixbuf-2.0.so.0:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libgdk_pixbuf-2.0.so.0.4303.0:0444"
        --file "/lib/libepoxy.so.0:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libepoxy.so.0.0.0:0444"
        --file "/lib/libpng16.so.16:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libpng16.so.16.49.0:0444"
        --file "/lib/libtiff.so.6:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libtiff.so.6.1.0:0444"
        --file "/lib/libjpeg.so.8.3.2:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libjpeg.so.8.3.2:0444"
        --dir "/usr/local/share/gtk-4.0"
    )
fi
UPSTREAM_GTK3_INITRAMFS_ARGS=()
if [[ "${UPSTREAM_GTK3_ENABLED:-0}" -eq 1 ]]; then
    GTK3_CURSOR_SOURCE="/usr/share/icons/whiteglass/cursors"
    if [[ ! -d "$GTK3_CURSOR_SOURCE" ]]; then
        GTK3_CURSOR_PACKAGE_DIR="$BUILD_DIR/upstream-source/xcursor-themes"
        GTK3_CURSOR_SOURCE="$GTK3_CURSOR_PACKAGE_DIR/root/usr/share/icons/whiteglass/cursors"
        if [[ ! -d "$GTK3_CURSOR_SOURCE" ]]; then
            if ! command -v apt-get >/dev/null 2>&1 ||
               ! command -v dpkg-deb >/dev/null 2>&1; then
                echo "[ERROR] Upstream GTK 3 needs the Whiteglass Xcursor theme" >&2
                echo "On Ubuntu: sudo apt install xcursor-themes" >&2
                exit 1
            fi
            mkdir -p "$GTK3_CURSOR_PACKAGE_DIR"
            (cd "$GTK3_CURSOR_PACKAGE_DIR" && apt-get download xcursor-themes)
            dpkg-deb -x "$GTK3_CURSOR_PACKAGE_DIR"/xcursor-themes_*.deb \
                "$GTK3_CURSOR_PACKAGE_DIR/root"
        fi
    fi
    if [[ ! -d "$GTK3_CURSOR_SOURCE" ]]; then
        echo "[ERROR] Whiteglass Xcursor theme assets are missing" >&2
        exit 1
    fi
    GTK3_LIBGTK="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libgtk-3.so.0.*' -print | sort -V | tail -n 1)"
    GTK3_LIBGDK="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libgdk-3.so.0.*' -print | sort -V | tail -n 1)"
    GTK3_LIBATK="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libatk-1.0.so.0.*' -print | sort -V | tail -n 1)"
    if [[ -z "$GTK3_LIBGTK" || -z "$GTK3_LIBGDK" || -z "$GTK3_LIBATK" ]]; then
        echo "[ERROR] Upstream GTK 3 shared libraries are missing from the graphics sysroot" >&2
        exit 1
    fi
    UPSTREAM_GTK3_INITRAMFS_ARGS=(
        --file "/bin/gtk3-upstream-test:$BUILD_DIR/GTK3_UPSTREAM_TEST.ELF:exec"
        --file "/bin/gtk3-x11-upstream-test:$BUILD_DIR/GTK3_X11_UPSTREAM_TEST.ELF:exec"
        --dir "/lib/.upstream-gtk3"
        --file "/lib/.upstream-gtk3/libgtk-3.so.0:$GTK3_LIBGTK:0444"
        --file "/lib/.upstream-gtk3/libgdk-3.so.0:$GTK3_LIBGDK:0444"
        --file "/lib/.upstream-gtk3/libatk-1.0.so.0:$GTK3_LIBATK:0444"
        --dir "/usr/share/icons"
        --dir "/usr/share/icons/default"
        --dir "/usr/share/icons/default/cursors"
        --file "/usr/share/icons/default/index.theme:$ROOT_DIR/userspace/etc/icons/default/index.theme:0444"
    )
    # The X11 backend of GTK 3 links against the accessibility bridge, which
    # at-spi2-core provides, so the bridge and the message bus library it
    # needs have to travel with the upstream GTK 3 runtime set.
    GTK3_ATK_BRIDGE="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libatk-bridge-2.0.so.0.0.0"
    GTK3_ATSPI="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libatspi.so.0.0.1"
    if [[ -f "$GTK3_ATK_BRIDGE" && -f "$GTK3_ATSPI" ]]; then
        UPSTREAM_GTK3_INITRAMFS_ARGS+=(
            --file "/lib/.upstream-gtk3/libatk-bridge-2.0.so.0:$GTK3_ATK_BRIDGE:0444"
            --file "/lib/.upstream-gtk3/libatspi.so.0:$GTK3_ATSPI:0444"
        )
    else
        echo "[ERROR] The upstream accessibility bridge is missing from the graphics sysroot" >&2
        echo "Build upstream at-spi2-core before the GTK 3 X11 backend." >&2
        exit 1
    fi
    GTK3_DBUS_LIBRARY="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libdbus-1.so.3.38.3"
    if [[ -f "$GTK3_DBUS_LIBRARY" ]]; then
        UPSTREAM_GTK3_INITRAMFS_ARGS+=(
            --file "/lib/libdbus-1.so.3:$GTK3_DBUS_LIBRARY:0444"
        )
    fi
    cursor_files=(
        "left_ptr"
    )
    for cursor_name in "${cursor_files[@]}"; do
        cursor_file="$GTK3_CURSOR_SOURCE/$cursor_name"
        if [[ ! -f "$cursor_file" ]]; then
            echo "[ERROR] Missing Whiteglass cursor asset: $cursor_file" >&2
            exit 1
        fi
        UPSTREAM_GTK3_INITRAMFS_ARGS+=(
            --file "/usr/share/icons/default/cursors/$cursor_name:$cursor_file:0444"
        )
    done
    cursor_aliases=(
        "nw-resize:top_left_corner"
        "n-resize:top_side"
        "ne-resize:top_right_corner"
        "w-resize:left_side"
        "e-resize:right_side"
        "sw-resize:bottom_left_corner"
        "s-resize:bottom_side"
        "se-resize:bottom_right_corner"
    )
    for cursor_alias in "${cursor_aliases[@]}"; do
        cursor_name="${cursor_alias%%:*}"
        cursor_source_name="${cursor_alias#*:}"
        cursor_file="$GTK3_CURSOR_SOURCE/$cursor_source_name"
        if [[ ! -f "$cursor_file" ]]; then
            echo "[ERROR] Missing Whiteglass cursor source: $cursor_file" >&2
            exit 1
        fi
        UPSTREAM_GTK3_INITRAMFS_ARGS+=(
            --file "/usr/share/icons/default/cursors/$cursor_name:$cursor_file:0444"
        )
    done
fi
UPSTREAM_WAYLAND_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_WAYLAND_ENABLED" -eq 1 ]]; then
    UPSTREAM_WAYLAND_INITRAMFS_ARGS=(
        --file "/bin/wayland-upstream-test:$BUILD_DIR/WAYLAND_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libwayland-client.so.0:$UPSTREAM_WAYLAND_BUILD/src/libwayland-client.so.0.26.0:0444"
        --file "/lib/libwayland-server.so.0:$UPSTREAM_WAYLAND_BUILD/src/libwayland-server.so.0.26.0:0444"
        --file "/lib/libwayland-cursor.so.0:$UPSTREAM_WAYLAND_BUILD/cursor/libwayland-cursor.so.0.26.0:0444"
        --file "/lib/libwayland-egl.so.1:$UPSTREAM_WAYLAND_BUILD/egl/libwayland-egl.so.1.26.0:0444"
    )
    if [[ "${UPSTREAM_WAYLAND_BRIDGE_ENABLED:-0}" -eq 1 ]]; then
        UPSTREAM_WAYLAND_INITRAMFS_ARGS+=(
            --file "/bin/wayland-bridge:$BUILD_DIR/WAYLAND_BRIDGE.ELF:exec"
        )
    fi
fi
UPSTREAM_DBUS_LIBRARY="$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libdbus-1.so.3.38.3"
UPSTREAM_DBUS_UPDATE_ENV="$BUILD_DIR/DBUS_UPDATE_ACTIVATION_ENVIRONMENT.ELF"
if [[ -f "$UPSTREAM_QT_PREFIX/lib/libQt6DBus.so.6.12.0" ]]; then
    if [[ ! -f "$UPSTREAM_DBUS_LIBRARY" || ! -f "$UPSTREAM_DBUS_UPDATE_ENV" ]]; then
        echo "[BUILD] Upstream libdbus and activation-environment tool required by QtDBus/LXQt"
        "$ROOT_DIR/tools/build-upstream-libdbus.sh"
    elif [[ "${AKAOS_SKIP_UPSTREAM_REFRESH:-0}" != "1" ]]; then
        echo "[BUILD] Refreshing upstream libdbus for QtDBus"
        run_upstream_refresh "libdbus" \
            bash "$ROOT_DIR/tools/build-upstream-libdbus.sh"
    fi
fi

UPSTREAM_QT_ENABLED=0
UPSTREAM_QT_INITRAMFS_ARGS=()
UPSTREAM_LXQT_INITRAMFS_ARGS=()
UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS=()
UPSTREAM_DBUS_INITRAMFS_ARGS=()
if [[ -f "$UPSTREAM_DBUS_UPDATE_ENV" ]]; then
    UPSTREAM_DBUS_INITRAMFS_ARGS+=(
        --file "/bin/dbus-update-activation-environment:$UPSTREAM_DBUS_UPDATE_ENV:exec"
    )
fi
if [[ -f "$UPSTREAM_QT_PREFIX/lib/libQt6Core.so.6.12.0" &&
      -f "$UPSTREAM_QT_PREFIX/lib/libQt6DBus.so.6.12.0" &&
      -f "$UPSTREAM_QT_PREFIX/lib/libQt6Concurrent.so.6.12.0" &&
      -f "$UPSTREAM_QT_PREFIX/lib/libQt6Gui.so.6.12.0" &&
      -f "$UPSTREAM_QT_PREFIX/lib/libQt6Widgets.so.6.12.0" &&
      -f "$UPSTREAM_QT_PREFIX/lib/libQt6WaylandClient.so.6.12.0" &&
      -f "$UPSTREAM_QT_PREFIX/plugins/platforms/libqwayland.so" &&
      -f "$UPSTREAM_QT_PREFIX/plugins/wayland-shell-integration/libxdg-shell.so" &&
      "$UPSTREAM_WAYLAND_ENABLED" -eq 1 ]]; then
    UPSTREAM_QT_ENABLED=1
    echo "[BUILD] Upstream Qt 6.12.0 Wayland client runtime"
    UPSTREAM_QT_INITRAMFS_ARGS=(
        --dir "/usr/local/lib"
        --dir "/usr/local/plugins"
        --dir "/usr/local/plugins/platforms"
        --dir "/usr/local/plugins/wayland-shell-integration"
        --dir "/usr/local/plugins/wayland-decoration-client"
        --data "/usr/local/lib/.upstream-qtbase:enabled:0444"
    )
    for qt_library in Qt6Core Qt6DBus Qt6Concurrent Qt6Gui Qt6Widgets Qt6WaylandClient; do
        qt_library_source="$UPSTREAM_QT_PREFIX/lib/lib${qt_library}.so.6.12.0"
        UPSTREAM_QT_INITRAMFS_ARGS+=(
            --file "/usr/local/lib/lib${qt_library}.so:$qt_library_source:0444"
            --file "/usr/local/lib/lib${qt_library}.so.6:$qt_library_source:0444"
            --file "/usr/local/lib/lib${qt_library}.so.6.12.0:$qt_library_source:0444"
        )
    done
    if [[ -f "$UPSTREAM_DBUS_LIBRARY" ]]; then
        for dbus_library_name in libdbus-1.so libdbus-1.so.3 libdbus-1.so.3.38.3; do
            UPSTREAM_QT_INITRAMFS_ARGS+=(
                --file "/usr/local/lib/$dbus_library_name:$UPSTREAM_DBUS_LIBRARY:0444"
            )
        done
    fi
    UPSTREAM_QT_INITRAMFS_ARGS+=(
        --file "/usr/local/plugins/platforms/libqwayland.so:$UPSTREAM_QT_PREFIX/plugins/platforms/libqwayland.so:0444"
        --file "/usr/local/plugins/wayland-shell-integration/libxdg-shell.so:$UPSTREAM_QT_PREFIX/plugins/wayland-shell-integration/libxdg-shell.so:0444"
    )
    if [[ "${AKAOS_QT_LOADER_TRACE:-0}" == "1" ]]; then
        UPSTREAM_QT_INITRAMFS_ARGS+=(
            --data "/etc/akaos-qt-loader-trace:enabled:0444"
            --data "/etc/akaos-dbus-trace:enabled:0444"
        )
    fi
    if [[ "${AKAOS_DBUS_VERBOSE:-0}" == "1" ]]; then
        UPSTREAM_QT_INITRAMFS_ARGS+=(
            --data "/etc/akaos-dbus-client-trace:enabled:0444"
        )
    fi
    if [[ -f "$UPSTREAM_QT_PREFIX/plugins/wayland-decoration-client/libbradient.so" ]]; then
        UPSTREAM_QT_INITRAMFS_ARGS+=(
            --file "/usr/local/plugins/wayland-decoration-client/libbradient.so:$UPSTREAM_QT_PREFIX/plugins/wayland-decoration-client/libbradient.so:0444"
        )
    fi
    if [[ -f "$UPSTREAM_LXQT_PREFIX/lib/libQt6Xml.so.6.12.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/libQt6Svg.so.6.12.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/libQt6SvgWidgets.so.6.12.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/libQt6Xdg.so.4.4.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/libQt6XdgIconLoader.so.4.4.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/libKF6WindowSystem.so.6.31.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/libsysstat-qt6.so.1.1.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/libdbusmenu-lxqt.so.0.4.0" &&
          -f "$UPSTREAM_LXQT_PREFIX/bin/qtxdg-mat" &&
          -f "$UPSTREAM_LXQT_PREFIX/lib/plugins/kf6/kwindowsystem/KF6WindowSystemKWaylandPlugin.so" &&
          -f "$UPSTREAM_LXQT_PREFIX/plugins/iconengines/libQt6XdgIconPlugin.so" &&
          -f "$UPSTREAM_LXQT_PREFIX/plugins/iconengines/libqsvgicon.so" &&
          -f "$UPSTREAM_LXQT_PREFIX/plugins/imageformats/libqsvg.so" &&
          -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxqt-qtxdg.conf" &&
          -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/qtxdg.conf" &&
          -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/menus/lxqt-applications.menu" &&
          -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/share/desktop-directories/lxqt-internet-and-network.directory" ]]; then
        echo "[BUILD] Upstream QtSvg and LXQt XDG runtime libraries"
        UPSTREAM_LXQT_INITRAMFS_ARGS+=(
            --dir "/usr/local/bin"
            --dir "/usr/local/plugins/iconengines"
            --dir "/usr/local/plugins/imageformats"
            --dir "/usr/local/lib/plugins"
            --dir "/usr/local/lib/plugins/kf6"
            --dir "/usr/local/lib/plugins/kf6/kwindowsystem"
            --dir "/usr/local/lib/plugins/wayland-shell-integration"
            --dir "/etc/xdg"
            --dir "/etc/xdg/menus"
            --dir "/usr/share/desktop-directories"
            --file "/usr/local/bin/qtxdg-mat:$UPSTREAM_LXQT_PREFIX/bin/qtxdg-mat:exec"
            --file "/usr/local/lib/plugins/kf6/kwindowsystem/KF6WindowSystemKWaylandPlugin.so:$UPSTREAM_LXQT_PREFIX/lib/plugins/kf6/kwindowsystem/KF6WindowSystemKWaylandPlugin.so:0444"
            --file "/usr/local/plugins/iconengines/libQt6XdgIconPlugin.so:$UPSTREAM_LXQT_PREFIX/plugins/iconengines/libQt6XdgIconPlugin.so:0444"
            --file "/usr/local/plugins/iconengines/libqsvgicon.so:$UPSTREAM_LXQT_PREFIX/plugins/iconengines/libqsvgicon.so:0444"
            --file "/usr/local/plugins/imageformats/libqsvg.so:$UPSTREAM_LXQT_PREFIX/plugins/imageformats/libqsvg.so:0444"
            --file "/etc/xdg/lxqt-qtxdg.conf:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxqt-qtxdg.conf:0444"
            --file "/etc/xdg/qtxdg.conf:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/qtxdg.conf:0444"
        )
        for lxqt_library in \
            "Qt6Xml:6.12.0:6" \
            "Qt6Svg:6.12.0:6" \
            "Qt6SvgWidgets:6.12.0:6" \
            "Qt6Xdg:4.4.0:4" \
            "Qt6XdgIconLoader:4.4.0:4" \
            "KF6WindowSystem:6.31.0:6" \
            "LayerShellQtInterface:6.0.0:6" \
            "lxqt:2.4.0:2" \
            "sysstat-qt6:1.1.0:1" \
            "dbusmenu-lxqt:0.4.0:0"; do
            IFS=: read -r library_name library_version library_soname <<<"$lxqt_library"
            library_source="$UPSTREAM_LXQT_PREFIX/lib/lib${library_name}.so.${library_version}"
            UPSTREAM_LXQT_INITRAMFS_ARGS+=(
                --file "/usr/local/lib/lib${library_name}.so:$library_source:0444"
                --file "/usr/local/lib/lib${library_name}.so.${library_soname}:$library_source:0444"
                --file "/usr/local/lib/lib${library_name}.so.${library_version}:$library_source:0444"
            )
        done
        for menu_file in "$UPSTREAM_GRAPHICS_SYSROOT"/etc/xdg/menus/*.menu; do
            if [[ -f "$menu_file" ]]; then
                UPSTREAM_LXQT_INITRAMFS_ARGS+=(
                    --file "/etc/xdg/menus/${menu_file##*/}:$menu_file:0444"
                )
            fi
        done
        for desktop_directory in "$UPSTREAM_GRAPHICS_SYSROOT"/usr/share/desktop-directories/*.directory; do
            if [[ -f "$desktop_directory" ]]; then
                UPSTREAM_LXQT_INITRAMFS_ARGS+=(
                    --file "/usr/share/desktop-directories/${desktop_directory##*/}:$desktop_directory:0444"
                )
            fi
        done
        if [[ -f "$UPSTREAM_LXQT_PREFIX/bin/lxqt-session" &&
              -f "$UPSTREAM_LXQT_PREFIX/bin/lxqt-leave" &&
              -f "$UPSTREAM_LXQT_PREFIX/bin/startlxqt" &&
              -f "$UPSTREAM_LXQT_PREFIX/lib/plugins/wayland-shell-integration/liblayer-shell.so" &&
              -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/xsessions/lxqt.desktop" ]]; then
            echo "[BUILD] Upstream LXQt Wayland session and leave dialog"
            UPSTREAM_LXQT_INITRAMFS_ARGS+=(
                --dir "/etc/xdg/autostart"
                --dir "/usr/local/share"
                --dir "/usr/local/share/applications"
                --dir "/usr/local/share/lxqt"
                --dir "/usr/local/share/xsessions"
                --dir "/usr/share"
                --dir "/usr/share/lxqt"
                --dir "/usr/share/lxqt/translations"
                --dir "/usr/share/lxqt/translations/liblxqt"
                --dir "/usr/share/lxqt/translations/lxqt-session"
                --dir "/usr/share/lxqt/translations/lxqt-leave"
                --file "/usr/local/bin/lxqt-session:$UPSTREAM_LXQT_PREFIX/bin/lxqt-session:exec"
                --file "/usr/local/bin/lxqt-leave:$UPSTREAM_LXQT_PREFIX/bin/lxqt-leave:exec"
                --file "/usr/local/bin/startlxqt.upstream:$UPSTREAM_LXQT_PREFIX/bin/startlxqt:0444"
                --file "/usr/local/bin/startlxqt:$BUILD_DIR/STARTLXQT.ELF:exec"
                --file "/bin/lxqt-session:$UPSTREAM_LXQT_PREFIX/bin/lxqt-session:exec"
                --file "/bin/lxqt-leave:$UPSTREAM_LXQT_PREFIX/bin/lxqt-leave:exec"
                --file "/bin/startlxqt:$BUILD_DIR/STARTLXQT.ELF:exec"
                --file "/usr/local/lib/plugins/wayland-shell-integration/liblayer-shell.so:$UPSTREAM_LXQT_PREFIX/lib/plugins/wayland-shell-integration/liblayer-shell.so:0444"
                --file "/usr/local/plugins/wayland-shell-integration/liblayer-shell.so:$UPSTREAM_LXQT_PREFIX/lib/plugins/wayland-shell-integration/liblayer-shell.so:0444"
                --file "/usr/local/share/xsessions/lxqt.desktop:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/xsessions/lxqt.desktop:0444"
            )
            for lxqt_config in "$UPSTREAM_LXQT_PREFIX"/share/lxqt/*.conf; do
                if [[ -f "$lxqt_config" ]]; then
                    UPSTREAM_LXQT_INITRAMFS_ARGS+=(
                        --file "/usr/local/share/lxqt/${lxqt_config##*/}:$lxqt_config:0444"
                    )
                fi
            done
            for lxqt_desktop_file in "$UPSTREAM_LXQT_PREFIX"/share/applications/lxqt-*.desktop; do
                if [[ -f "$lxqt_desktop_file" ]]; then
                    UPSTREAM_LXQT_INITRAMFS_ARGS+=(
                        --file "/usr/local/share/applications/${lxqt_desktop_file##*/}:$lxqt_desktop_file:0444"
                    )
                fi
            done
            for translation_group in liblxqt lxqt-session lxqt-leave; do
                for translation_file in "$UPSTREAM_GRAPHICS_SYSROOT"/usr/share/lxqt/translations/"$translation_group"/*.qm; do
                    if [[ -f "$translation_file" ]]; then
                        UPSTREAM_LXQT_INITRAMFS_ARGS+=(
                            --file "/usr/share/lxqt/translations/$translation_group/${translation_file##*/}:$translation_file:0444"
                        )
                    fi
                done
            done
        else
            echo "[INFO] LXQt session runtime is not fully staged; build LayerShellQt and lxqt-session"
        fi
        if [[ -f "$UPSTREAM_LXQT_PREFIX/bin/lxqt-panel" &&
              -d "$UPSTREAM_LXQT_PREFIX/lib/lxqt-panel" &&
              -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxqt/panel.conf" &&
              -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/autostart/lxqt-panel.desktop" &&
              -d "$UPSTREAM_LXQT_PREFIX/share/lxqt/lxqt-panel" &&
              -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/share/applications/lxqt-panel.desktop" &&
              -d "$UPSTREAM_GRAPHICS_SYSROOT/usr/share/lxqt/translations/lxqt-panel" &&
              -f "$UPSTREAM_QT_PREFIX/lib/libQt6Concurrent.so.6.12.0" ]]; then
            echo "[BUILD] Upstream LXQt Wayland panel, plugins, and status notifier"
            UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS+=(
                --dir "/root"
                --dir "/root/.config"
                --dir "/root/.config/lxqt"
                --dir "/etc/xdg/lxqt"
                --dir "/etc/xdg/autostart"
                --dir "/usr/local/bin"
                --dir "/usr/local/lib/lxqt-panel"
                --dir "/usr/local/share/lxqt/lxqt-panel"
                --dir "/usr/share/applications"
                --dir "/usr/share/lxqt/translations/lxqt-panel"
                --file "/usr/local/bin/lxqt-panel:$UPSTREAM_LXQT_PREFIX/bin/lxqt-panel:exec"
                --file "/bin/lxqt-panel:$UPSTREAM_LXQT_PREFIX/bin/lxqt-panel:exec"
                --file "/root/.config/lxqt/panel.conf:$ROOT_DIR/config/lxqt-panel-user.conf:0444"
                --file "/etc/xdg/lxqt/panel.conf:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxqt/panel.conf:0444"
                --file "/etc/xdg/autostart/lxqt-panel.desktop:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/autostart/lxqt-panel.desktop:0444"
                --file "/usr/share/applications/lxqt-panel.desktop:$UPSTREAM_GRAPHICS_SYSROOT/usr/share/applications/lxqt-panel.desktop:0444"
            )
            while IFS= read -r panel_directory; do
                panel_relative="/usr/local${panel_directory#"$UPSTREAM_LXQT_PREFIX"}"
                [[ "$panel_directory" == "$UPSTREAM_LXQT_PREFIX/lib/lxqt-panel" ]] && continue
                UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS+=(--dir "$panel_relative")
            done < <(find -L "$UPSTREAM_LXQT_PREFIX/lib/lxqt-panel" -type d -print | sort)
            while IFS= read -r panel_file; do
                panel_relative="/usr/local${panel_file#"$UPSTREAM_LXQT_PREFIX"}"
                UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS+=(
                    --file "$panel_relative:$panel_file:0444"
                )
            done < <(find -L "$UPSTREAM_LXQT_PREFIX/lib/lxqt-panel" -type f -print | sort)
            for globalkeys_library in liblxqt-globalkeys liblxqt-globalkeys-ui; do
                globalkeys_source="$UPSTREAM_LXQT_PREFIX/lib/${globalkeys_library}.so.2.4.0"
                if [[ -f "$globalkeys_source" ]]; then
                    UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS+=(
                        --file "/usr/local/lib/${globalkeys_library}.so:$globalkeys_source:0444"
                        --file "/usr/local/lib/${globalkeys_library}.so.2:$globalkeys_source:0444"
                        --file "/usr/local/lib/${globalkeys_library}.so.2.4.0:$globalkeys_source:0444"
                    )
                fi
            done
            while IFS= read -r panel_desktop; do
                panel_relative="/usr/local${panel_desktop#"$UPSTREAM_LXQT_PREFIX"}"
                UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS+=(
                    --file "$panel_relative:$panel_desktop:0444"
                )
            done < <(find "$UPSTREAM_LXQT_PREFIX/share/lxqt/lxqt-panel" -type f -name '*.desktop' -print | sort)
            while IFS= read -r panel_translation_directory; do
                [[ "$panel_translation_directory" == \
                   "$UPSTREAM_GRAPHICS_SYSROOT/usr/share/lxqt/translations/lxqt-panel" ]] && continue
                panel_relative="${panel_translation_directory#"$UPSTREAM_GRAPHICS_SYSROOT"}"
                UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS+=(--dir "$panel_relative")
            done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/share/lxqt/translations/lxqt-panel" -type d -print | sort)
            while IFS= read -r panel_translation; do
                panel_relative="${panel_translation#"$UPSTREAM_GRAPHICS_SYSROOT"}"
                UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS+=(
                    --file "$panel_relative:$panel_translation:0444"
                )
            done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/share/lxqt/translations/lxqt-panel" -type f -name '*.qm' -print | sort)
        else
            echo "[INFO] Upstream LXQt panel runtime is not fully staged"
        fi
    else
        echo "[INFO] LXQt runtime foundations not staged; build QtSvg and the upstream libqtxdg dependencies first"
    fi
    if [[ -n "$UPSTREAM_QT_BUILD_DIR" &&
          -n "$UPSTREAM_QT_HOST_PREFIX" &&
          -f "$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        qt_smoke_build_dir="$BUILD_DIR/upstream-build/qt-wayland-smoke/x86_64-akaos"
        echo "[BUILD] Qt Wayland native-window smoke application"
        cmake -S "$ROOT_DIR/userspace/bin/qt-wayland-smoke" \
            -B "$qt_smoke_build_dir" \
            -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
            -DCMAKE_BUILD_TYPE=Release \
            -DAKAOS_PROJECT_ROOT="$UPSTREAM_QT_TARGET_ROOT" \
            -DAKAOS_DYNAMIC_CRT_OBJECT="$UPSTREAM_QT_TARGET_ROOT/build/user-dynamic-crt0.o" \
            -DQT_HOST_PATH="$UPSTREAM_QT_HOST_PREFIX" \
            -DQt6_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6" \
            -DQt6BuildInternals_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6BuildInternals" \
            -DQt6Core_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6Core" \
            -DQt6Gui_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6Gui" \
            -DQt6Widgets_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6Widgets" \
            -DQt6DBus_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6DBus"
        cmake --build "$qt_smoke_build_dir" \
            --parallel "$AKAOS_BUILD_JOBS"
        cp "$qt_smoke_build_dir/output/qtwayland-smoke" \
            "$BUILD_DIR/QT_WAYLAND_SMOKE.ELF"
        UPSTREAM_QT_INITRAMFS_ARGS+=(
            --file "/bin/qtwayland-smoke:$BUILD_DIR/QT_WAYLAND_SMOKE.ELF:exec"
        )
    else
        echo "[INFO] Qt runtime staged; set AKAOS_QT_BUILD_DIR and AKAOS_QT_HOST_PREFIX to build the live QPA smoke client"
    fi
    if [[ -n "$UPSTREAM_QT_BUILD_DIR" &&
          -n "$UPSTREAM_QT_HOST_PREFIX" &&
          -n "$UPSTREAM_QT_DECLARATIVE_BUILD_DIR" &&
          -n "$UPSTREAM_QT_HOST_QML_TOOLS_DIR" &&
          -f "$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib/cmake/Qt6Quick/Qt6QuickConfig.cmake" ]]; then
        qt_quick_smoke_build_dir="$BUILD_DIR/upstream-build/qtquick-wayland-smoke/x86_64-akaos"
        echo "[BUILD] Qt Quick Wayland software-renderer smoke application"
        cmake -S "$ROOT_DIR/userspace/bin/qtquick-wayland-smoke" \
            -B "$qt_quick_smoke_build_dir" \
            -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
            -DCMAKE_BUILD_TYPE=Release \
            -DAKAOS_PROJECT_ROOT="$UPSTREAM_QT_TARGET_ROOT" \
            -DAKAOS_DYNAMIC_CRT_OBJECT="$UPSTREAM_QT_TARGET_ROOT/build/user-dynamic-crt0.o" \
            -DQT_HOST_PATH="$UPSTREAM_QT_HOST_PREFIX" \
            -DQt6_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6" \
            -DQt6BuildInternals_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6BuildInternals" \
            -DQt6Core_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6Core" \
            -DQt6DBus_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6DBus" \
            -DQt6Gui_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6Gui" \
            -DQt6Network_DIR="$UPSTREAM_QT_BUILD_DIR/lib/cmake/Qt6Network" \
            -DQt6Qml_DIR="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib/cmake/Qt6Qml" \
            -DQt6QmlIntegration_DIR="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib/cmake/Qt6QmlIntegration" \
            -DQt6QmlMeta_DIR="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib/cmake/Qt6QmlMeta" \
            -DQt6QmlModels_DIR="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib/cmake/Qt6QmlModels" \
            -DQt6QmlWorkerScript_DIR="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib/cmake/Qt6QmlWorkerScript" \
            -DQt6QmlTools_DIR="$UPSTREAM_QT_HOST_QML_TOOLS_DIR" \
            -DQt6ShaderToolsTools_DIR="$UPSTREAM_QT_HOST_PREFIX/lib/cmake/Qt6ShaderToolsTools" \
            -DQt6Quick_DIR="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib/cmake/Qt6Quick" \
            -DCMAKE_PREFIX_PATH="$UPSTREAM_QT_BUILD_DIR;$UPSTREAM_QT_DECLARATIVE_BUILD_DIR"
        cmake --build "$qt_quick_smoke_build_dir" \
            --parallel "$AKAOS_BUILD_JOBS"
        cp "$qt_quick_smoke_build_dir/output/qtquick-wayland-smoke" \
            "$BUILD_DIR/QT_QUICK_WAYLAND_SMOKE.ELF"
        UPSTREAM_QT_INITRAMFS_ARGS+=(
            --file "/bin/qtquick-wayland-smoke:$BUILD_DIR/QT_QUICK_WAYLAND_SMOKE.ELF:exec"
        )
    elif [[ -n "$UPSTREAM_QT_DECLARATIVE_BUILD_DIR" ]]; then
        echo "[INFO] Qt Quick smoke build skipped; set AKAOS_QT_BUILD_DIR and AKAOS_QT_HOST_PREFIX"
    fi
else
    echo "[INFO] Upstream Qt Wayland runtime is not staged; keeping compatibility DSOs"
fi
UPSTREAM_QT_QUICK_INITRAMFS_ARGS=()
if [[ -n "$UPSTREAM_QT_DECLARATIVE_BUILD_DIR" ]]; then
    if [[ "$UPSTREAM_QT_ENABLED" -ne 1 ]]; then
        echo "[ERROR] Qt Declarative staging requires the upstream Qt Wayland runtime" >&2
        exit 1
    fi
    qt_declarative_lib_dir="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/lib"
    for qt_library in Qt6Qml Qt6QmlMeta Qt6QmlModels \
        Qt6QmlWorkerScript Qt6Quick; do
        qt_library_source="$qt_declarative_lib_dir/lib${qt_library}.so.6.12.0"
        if [[ ! -f "$qt_library_source" ]]; then
            echo "[ERROR] Missing upstream Qt Quick runtime library: $qt_library_source" >&2
            exit 1
        fi
        UPSTREAM_QT_QUICK_INITRAMFS_ARGS+=(
            --file "/usr/local/lib/lib${qt_library}.so:$qt_library_source:0444"
            --file "/usr/local/lib/lib${qt_library}.so.6:$qt_library_source:0444"
            --file "/usr/local/lib/lib${qt_library}.so.6.12.0:$qt_library_source:0444"
        )
    done
    qt_network_source="$UPSTREAM_QT_BUILD_DIR/lib/libQt6Network.so.6.12.0"
    if [[ ! -f "$qt_network_source" ]]; then
        echo "[ERROR] Qt Quick requires the upstream Qt Network runtime: $qt_network_source" >&2
        exit 1
    fi
    UPSTREAM_QT_QUICK_INITRAMFS_ARGS+=(
        --file "/usr/local/lib/libQt6Network.so:$qt_network_source:0444"
        --file "/usr/local/lib/libQt6Network.so.6:$qt_network_source:0444"
        --file "/usr/local/lib/libQt6Network.so.6.12.0:$qt_network_source:0444"
    )

    qt_qml_import_root="$UPSTREAM_QT_DECLARATIVE_BUILD_DIR/qml"
    if [[ ! -d "$qt_qml_import_root" ]]; then
        echo "[ERROR] Missing upstream Qt QML import tree: $qt_qml_import_root" >&2
        exit 1
    fi
    echo "[BUILD] Upstream Qt QML/Quick runtime and import modules"
    qt_qml_modules=(QML QtCore QtNetwork QtQml QtQml/Models \
        QtQml/WorkerScript QtQuick QtQuick/Window)
    UPSTREAM_QT_QUICK_INITRAMFS_ARGS+=(--dir "/usr/local/qml")
    for qt_qml_module in "${qt_qml_modules[@]}"; do
        qt_qml_module_root="$qt_qml_import_root/$qt_qml_module"
        [[ -d "$qt_qml_module_root" ]] || continue
        qt_qml_target_dir="/usr/local/qml"
        IFS='/' read -r -a qt_qml_parts <<< "$qt_qml_module"
        for qt_qml_part in "${qt_qml_parts[@]}"; do
            qt_qml_target_dir+="/$qt_qml_part"
            UPSTREAM_QT_QUICK_INITRAMFS_ARGS+=(--dir "$qt_qml_target_dir")
        done
        while IFS= read -r -d '' qt_qml_file; do
            qt_qml_relative="${qt_qml_file#"$qt_qml_import_root"/}"
            qt_qml_name="${qt_qml_relative##*/}"
            if [[ "${#qt_qml_name}" -gt 31 ]]; then
                echo "[ERROR] Qt QML component name exceeds akaOS VFS limit: $qt_qml_name" >&2
                exit 1
            fi
            UPSTREAM_QT_QUICK_INITRAMFS_ARGS+=(
                --file "/usr/local/qml/$qt_qml_relative:$qt_qml_file:0444"
            )
        done < <(find "$qt_qml_module_root" -maxdepth 1 -type f \
            \( -name '*.qml' -o -name '*.js' -o -name '*.so' \
               -o -name 'qmldir' \) -print0)
    done

    if [[ -f "$BUILD_DIR/QT_QUICK_WAYLAND_SMOKE.ELF" ]]; then
        UPSTREAM_QT_QUICK_INITRAMFS_ARGS+=(
            --file "/bin/qtquick-wayland-smoke:$BUILD_DIR/QT_QUICK_WAYLAND_SMOKE.ELF:exec"
        )
    fi
fi
UPSTREAM_XKBCOMMON_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_XKBCOMMON_ENABLED" -eq 1 ]]; then
    UPSTREAM_XKBCOMMON_INITRAMFS_ARGS=(
        --file "/bin/xkbcommon-upstream-test:$BUILD_DIR/XKBCOMMON_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libxkbcommon.so.0:$UPSTREAM_XKBCOMMON_BUILD/libxkbcommon.so.0.13.2:0444"
        --dir "/usr/share/licenses/xkbcommon"
        --file "/usr/share/licenses/xkbcommon/LICENSE:$UPSTREAM_XKBCOMMON_SOURCE/LICENSE:0444"
    )
fi
UPSTREAM_XKB_DATA_INITRAMFS_ARGS=()
if [[ "${UPSTREAM_WAYLAND_BRIDGE_ENABLED:-0}" -eq 1 ]]; then
    XKB_DATA_SOURCE="${AKAOS_XKB_DATA_SOURCE:-/usr/share/X11/xkb}"
    XKB_DATA_COPYRIGHT="${AKAOS_XKB_DATA_COPYRIGHT:-/usr/share/doc/xkb-data/copyright}"
    if [[ ! -f "$XKB_DATA_SOURCE/rules/evdev" ||
          ! -f "$XKB_DATA_SOURCE/symbols/us" ]]; then
        echo "[ERROR] Missing xkb-data rules under $XKB_DATA_SOURCE (Ubuntu package: xkb-data)" >&2
        exit 1
    fi
    UPSTREAM_XKB_DATA_INITRAMFS_ARGS+=(
        --dir "/usr/share/X11"
        --dir "/usr/share/X11/xkb"
        --dir "/usr/share/licenses/xkeyboard-config"
    )
    while IFS= read -r -d '' xkb_entry; do
        xkb_relative="${xkb_entry#"$XKB_DATA_SOURCE"/}"
        if [[ -d "$xkb_entry" ]]; then
            UPSTREAM_XKB_DATA_INITRAMFS_ARGS+=(
                --dir "/usr/share/X11/xkb/$xkb_relative"
            )
        elif [[ -f "$xkb_entry" ]]; then
            # xkbcommon consumes the rules, keycodes, types, compatibility and
            # symbols files. The large XML catalogue files are desktop UI
            # metadata, not keymap compiler inputs, and exceed the VFS per-file
            # cap; keep them out of this first keyboard-runtime image.
            [[ "$xkb_relative" == *.xml ]] && continue
            xkb_size="$(stat -c '%s' "$xkb_entry")"
            if (( xkb_size > 131072 )); then
                echo "[ERROR] xkbcommon input exceeds the VFS file cap: $xkb_entry" >&2
                exit 1
            fi
            UPSTREAM_XKB_DATA_INITRAMFS_ARGS+=(
                --file "/usr/share/X11/xkb/$xkb_relative:$xkb_entry:0444"
            )
        fi
    done < <(find -L "$XKB_DATA_SOURCE" -mindepth 1 -print0 | sort -z)
    if [[ -f "$XKB_DATA_COPYRIGHT" ]]; then
        UPSTREAM_XKB_DATA_INITRAMFS_ARGS+=(
            --file "/usr/share/licenses/xkeyboard-config/copyright:$XKB_DATA_COPYRIGHT:0444"
        )
    fi
    if command -v dpkg-query >/dev/null 2>&1; then
        XKB_DATA_VERSION="$(dpkg-query -W -f='${Version}' xkb-data 2>/dev/null || true)"
    else
        XKB_DATA_VERSION="host-provided"
    fi
    echo "[BUILD] Packaging xkb-data $XKB_DATA_VERSION for Wayland keyboard clients"
fi
UPSTREAM_CAIRO_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_CAIRO_ENABLED" -eq 1 ]]; then
    # The libraries are staged from the sysroot they were installed into, so
    # the image always carries the Cairo that the other packages were built
    # against, including the Xlib surface backend that GTK 3 needs.
    UPSTREAM_CAIRO_LIBRARY="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libcairo.so.2.*' -print | sort -V | tail -n 1)"
    UPSTREAM_CAIRO_SCRIPT="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libcairo-script-interpreter.so.2.*' -print | sort -V | tail -n 1)"
    UPSTREAM_CAIRO_GOBJECT="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
        -maxdepth 1 -type f -name 'libcairo-gobject.so.2.*' -print | sort -V | tail -n 1)"
    if [[ -z "$UPSTREAM_CAIRO_LIBRARY" ]]; then
        echo "[ERROR] The upstream Cairo shared library is missing from the graphics sysroot" >&2
        exit 1
    fi
    UPSTREAM_CAIRO_INITRAMFS_ARGS=(
        --file "/bin/cairo-upstream-test:$BUILD_DIR/CAIRO_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libcairo.so.2:$UPSTREAM_CAIRO_LIBRARY:0444"
        --file "/lib/libpixman-1.so.0:$UPSTREAM_PIXMAN_BUILD/pixman/libpixman-1.so.0.46.4:0444"
        --file "/lib/libz.so.1:$UPSTREAM_GRAPHICS_SYSROOT/usr/lib/libz.so.1.3.1:0444"
    )
    if [[ -n "$UPSTREAM_CAIRO_SCRIPT" ]]; then
        UPSTREAM_CAIRO_INITRAMFS_ARGS+=(
            --file "/lib/libcairo-script-interpreter.so.2:$UPSTREAM_CAIRO_SCRIPT:0444"
        )
    fi
    if [[ -n "$UPSTREAM_CAIRO_GOBJECT" ]]; then
        UPSTREAM_CAIRO_INITRAMFS_ARGS+=(
            --file "/lib/libcairo-gobject.so.2:$UPSTREAM_CAIRO_GOBJECT:0444"
        )
    fi
fi
UPSTREAM_OPENBOX_INITRAMFS_ARGS=()
if [[ "$need_upstream_gui" -eq 1 ]]; then
if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/openbox" ]]; then
    echo "[BUILD] Staging the upstream Openbox window manager"
    # The window manager and the libraries it needs travel in the local
    # prefix, which is where the dynamic loader looks for them.
    UPSTREAM_OPENBOX_INITRAMFS_ARGS=(
        --dir "/usr/local/etc"
        --dir "/usr/local/etc/xdg"
        --dir "/usr/local/etc/xdg/openbox"
        --dir "/usr/local/etc/openbox"
        --file "/usr/local/bin/openbox:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/openbox:exec"
        --file "/usr/local/etc/xdg/openbox/rc.xml:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/etc/xdg/openbox/rc.xml:0444"
        --file "/usr/local/etc/xdg/openbox/menu.xml:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/etc/xdg/openbox/menu.xml:0444"
        --file "/usr/local/etc/openbox/rc.xml:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/etc/xdg/openbox/rc.xml:0444"
        --file "/usr/local/etc/openbox/menu.xml:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/etc/xdg/openbox/menu.xml:0444"
    )
    for openbox_library in libobt.so.2 libobrender.so.32 libxml2.so.2 \
        libpangoxft-1.0.so.0 libpango-1.0.so.0 libXft.so.2 libglib-2.0.so.0; do
        library_path="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
            -maxdepth 1 -type f -name "${openbox_library}.*" -print | sort -V | tail -n 1)"
        [[ -n "$library_path" ]] || continue
        UPSTREAM_OPENBOX_INITRAMFS_ARGS+=(
            --file "/usr/local/lib/$openbox_library:$library_path:0444"
        )
    done
    if [[ -d "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/themes" ]]; then
        while IFS= read -r theme_dir; do
            relative_path="${theme_dir#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/"}"
            UPSTREAM_OPENBOX_INITRAMFS_ARGS+=(
                --dir "/usr/local/share/$relative_path"
            )
        done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/themes" -type d | sort)
        while IFS= read -r theme_file; do
            relative_path="${theme_file#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/"}"
            UPSTREAM_OPENBOX_INITRAMFS_ARGS+=(
                --file "/usr/local/share/$relative_path:$theme_file:0444"
            )
        done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/themes" -type f | sort)
    fi
fi

UPSTREAM_LXPANEL_INITRAMFS_ARGS=()
if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxpanel" ]]; then
    echo "[BUILD] Staging the upstream LXDE panel and its runtime libraries"
    UPSTREAM_LXPANEL_INITRAMFS_ARGS=(
        --dir "/etc"
        --dir "/etc/xdg"
        --dir "/etc/xdg/menus"
        --dir "/usr/share/desktop-directories"
        --dir "/usr/local/share/desktop-directories"
        --dir "/etc/xdg/lxpanel"
        --dir "/etc/xdg/lxpanel/default"
        --dir "/etc/xdg/lxpanel/default/panels"
        --dir "/etc/xdg/lxpanel/two_panels"
        --dir "/etc/xdg/lxpanel/two_panels/panels"
        --dir "/usr/local/share/lxpanel"
        --dir "/usr/local/share/lxpanel/images"
        --dir "/usr/local/share/lxpanel/ui"
        --dir "/usr/local/share/libfm"
        --dir "/usr/local/share/libfm/images"
        --dir "/usr/local/share/libfm/ui"
        --dir "/usr/local/lib/lxpanel"
        --dir "/usr/local/lib/lxpanel/plugins"
        --dir "/usr/local/lib/libfm"
        --dir "/usr/local/lib/libfm/modules"
        --dir "/usr/local/libexec"
        --dir "/usr/local/libexec/menu-cache"
        --file "/usr/local/libexec/menu-cache/menu-cached:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/libexec/menu-cache/menu-cached:exec"
        --file "/usr/local/libexec/menu-cache/menu-cache-gen:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/libexec/menu-cache/menu-cache-gen:exec"
        --file "/usr/local/bin/lxpanel:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxpanel:exec"
        --file "/usr/local/bin/lxpanelctl:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxpanelctl:exec"
        --file "/lib/liblxpanel.so.0:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/lxpanel/liblxpanel.so.0.0.0:0444"
        --file "/etc/xdg/lxpanel/default/config:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/default/config:0444"
        --file "/etc/xdg/lxpanel/default/panels/panel:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/default/panels/panel:0444"
        --file "/etc/xdg/lxpanel/two_panels/config:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/two_panels/config:0444"
        --file "/etc/xdg/lxpanel/two_panels/panels/bottom:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/two_panels/panels/bottom:0444"
        --file "/etc/xdg/lxpanel/two_panels/panels/top:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/two_panels/panels/top:0444"
    )
    while IFS= read -r lxpanel_plugin; do
        plugin_name="${lxpanel_plugin##*/}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/lib/lxpanel/plugins/$plugin_name:$lxpanel_plugin:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/lxpanel/plugins" -maxdepth 1 -type f -name '*.so' -print 2>/dev/null | sort)
    while IFS= read -r fm_module; do
        mod_name="${fm_module##*/}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/lib/libfm/modules/$mod_name:$fm_module:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libfm/modules" -maxdepth 1 -type f -name '*.so' -print 2>/dev/null | sort)
    for lxpanel_library in libwnck-3.so.0 libfm-gtk3.so.4 libfm.so.4 \
        libfm-extra.so.4 libmenu-cache.so.3 libkeybinder-3.0.so.0; do
        library_path="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib" \
            -maxdepth 1 -type f -name "${lxpanel_library}.*" -print | sort -V | tail -n 1)"
        [[ -n "$library_path" ]] || continue
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/lib/$lxpanel_library:$library_path:0444"
            --file "/usr/local/lib/$lxpanel_library:$library_path:0444"
        )
    done
    while IFS= read -r data_file; do
        relative_path="${data_file#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/"}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/$relative_path:$data_file:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/lxpanel/images" \
                  "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/lxpanel/ui" \
                  "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/libfm" \
                  -maxdepth 1 -type f | sort)
    while IFS= read -r data_file; do
        relative_path="${data_file#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/"}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/$relative_path:$data_file:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/libfm/images" \
                  "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/libfm/ui" \
                  -maxdepth 1 -type f | sort)
    if [[ -d "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/menus" ]]; then
        for menu_file in "$UPSTREAM_GRAPHICS_SYSROOT"/etc/xdg/menus/*.menu; do
            [[ -f "$menu_file" ]] || continue
            UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
                --file "/etc/xdg/menus/${menu_file##*/}:$menu_file:0444"
            )
        done
    fi
    if [[ -d "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/desktop-directories" ]]; then
        for dir_file in "$UPSTREAM_GRAPHICS_SYSROOT"/usr/local/share/desktop-directories/*.directory; do
            [[ -f "$dir_file" ]] || continue
            UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
                --file "/usr/share/desktop-directories/${dir_file##*/}:$dir_file:0444"
                --file "/usr/local/share/desktop-directories/${dir_file##*/}:$dir_file:0444"
            )
        done
    fi
fi

if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/pcmanfm" ]]; then
    echo "[BUILD] Staging upstream PCManFM file manager"
    UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
        --file "/usr/local/bin/pcmanfm:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/pcmanfm:exec"
        --dir "/etc/xdg/pcmanfm"
        --dir "/etc/xdg/pcmanfm/default"
        --dir "/etc/xdg/pcmanfm/LXDE"
        --dir "/usr/local/share/pcmanfm"
        --dir "/usr/local/share/pcmanfm/ui"
    )
    if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/pcmanfm/default/pcmanfm.conf" ]]; then
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/etc/xdg/pcmanfm/default/pcmanfm.conf:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/pcmanfm/default/pcmanfm.conf:0444"
        )
    fi
    if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/pcmanfm/LXDE/pcmanfm.conf" ]]; then
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/etc/xdg/pcmanfm/LXDE/pcmanfm.conf:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/pcmanfm/LXDE/pcmanfm.conf:0444"
        )
    fi
    while IFS= read -r ui_file; do
        relative_path="${ui_file#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/"}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/$relative_path:$ui_file:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/pcmanfm/ui" -maxdepth 1 -type f 2>/dev/null | sort)
fi

if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/startlxde" ]]; then
    echo "[BUILD] Staging upstream LXDE common session scripts & config"
    UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
        --file "/usr/local/bin/startlxde:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/startlxde:exec"
        --file "/usr/local/bin/openbox-lxde:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/openbox-lxde:exec"
        --file "/usr/local/bin/lxde-logout:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxde-logout:exec"
        --dir "/etc/xdg/lxsession"
        --dir "/etc/xdg/lxsession/LXDE"
        --dir "/etc/xdg/openbox/LXDE"
        --dir "/etc/xdg/lxpanel/LXDE"
        --dir "/etc/xdg/lxpanel/LXDE/panels"
        --dir "/usr/local/share/lxde"
        --dir "/usr/local/share/lxde/images"
        --dir "/usr/local/share/lxde/wallpapers"
        --dir "/usr/local/share/xsessions"
    )
    if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxsession/LXDE/autostart" ]]; then
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/etc/xdg/lxsession/LXDE/autostart:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxsession/LXDE/autostart:0444"
            --file "/etc/xdg/lxsession/LXDE/desktop.conf:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxsession/LXDE/desktop.conf:0444"
        )
    fi
    if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/openbox/LXDE/rc.xml" ]]; then
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/etc/xdg/openbox/LXDE/rc.xml:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/openbox/LXDE/rc.xml:0444"
            --file "/etc/xdg/openbox/LXDE/menu.xml:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/openbox/LXDE/menu.xml:0444"
        )
    fi
    if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/LXDE/config" ]]; then
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/etc/xdg/lxpanel/LXDE/config:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/LXDE/config:0444"
            --file "/etc/xdg/lxpanel/LXDE/panels/panel:$UPSTREAM_GRAPHICS_SYSROOT/etc/xdg/lxpanel/LXDE/panels/panel:0444"
        )
    fi
    while IFS= read -r share_file; do
        relative_path="${share_file#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/"}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/$relative_path:$share_file:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/lxde" "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/xsessions" -type f 2>/dev/null | sort)
fi

if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession" ]]; then
    echo "[BUILD] Staging upstream LXSession binaries"
    UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
        --file "/usr/local/bin/lxsession:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession:exec"
        --file "/usr/local/bin/lxsettings-daemon:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsettings-daemon:exec"
        --file "/usr/local/bin/lxsession-xdg-autostart:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession-xdg-autostart:exec"
        --file "/usr/local/bin/lxsession-logout:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession-logout:exec"
        --file "/usr/local/bin/lxsession-edit:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession-edit:exec"
        --file "/usr/local/bin/lxsession-db:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession-db:exec"
        --file "/usr/local/bin/lxlock:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxlock:exec"
        --file "/usr/local/bin/lxsession-default:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession-default:exec"
        --file "/usr/local/bin/lxsession-default-terminal:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/lxsession-default-terminal:exec"
        --file "/usr/local/libexec/lxsession-xsettings:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/libexec/lxsession-xsettings:exec"
        --dir "/usr/local/share/lxsession"
        --dir "/usr/local/share/lxsession/ui"
        --dir "/usr/local/share/lxsession/images"
    )
    while IFS= read -r share_file; do
        relative_path="${share_file#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/"}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/$relative_path:$share_file:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/lxsession" -type f 2>/dev/null | sort)
fi

if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/xfwm4" ]]; then
    echo "[BUILD] Staging upstream XFCE components & xfwm4"
    UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
        --file "/usr/local/bin/xfwm4:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/xfwm4:exec"
        --dir "/usr/local/share/xfwm4"
        --dir "/usr/local/share/themes"
    )
    for xfce_library in libxfce4util.so.7 libxfconf-0.so.3 libxfce4ui-2.so.0 libxfce4kbd-private-3.so.0; do
        library_path="$(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib"             -maxdepth 1 -type f -name "${xfce_library}.*" -print | sort -V | tail -n 1)"
        [[ -n "$library_path" ]] || continue
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/lib/$xfce_library:$library_path:0444"
            --file "/usr/local/lib/$xfce_library:$library_path:0444"
        )
    done
    if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/xfconf-query" ]]; then
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/bin/xfconf-query:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/bin/xfconf-query:exec"
            --file "/usr/local/lib/xfce4/xfconf/xfconfd:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/xfce4/xfconf/xfconfd:exec"
            --dir "/usr/local/lib/xfce4"
            --dir "/usr/local/lib/xfce4/xfconf"
            --dir "/usr/local/share/dbus-1"
            --dir "/usr/local/share/dbus-1/services"
        )
        if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/dbus-1/services/org.xfce.Xfconf.service" ]]; then
            UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
                --file "/usr/local/share/dbus-1/services/org.xfce.Xfconf.service:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/dbus-1/services/org.xfce.Xfconf.service:0444"
            )
        fi
    fi
    while IFS= read -r share_file; do
        relative_path="${share_file#"$UPSTREAM_GRAPHICS_SYSROOT/usr/local/"}"
        UPSTREAM_LXPANEL_INITRAMFS_ARGS+=(
            --file "/usr/local/$relative_path:$share_file:0444"
        )
    done < <(find "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/xfwm4" "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/share/themes" -type f 2>/dev/null | sort)
fi

UPSTREAM_FREETYPE_INITRAMFS_ARGS=()
if [[ "$UPSTREAM_FREETYPE_ENABLED" -eq 1 ]]; then
    UPSTREAM_FREETYPE_INITRAMFS_ARGS=(
        --file "/bin/freetype-upstream-test:$BUILD_DIR/FREETYPE_UPSTREAM_TEST.ELF:exec"
        --file "/lib/libfreetype.so.6:$UPSTREAM_FREETYPE_BUILD/libfreetype.so.6.20.6:0444"
        --dir "/usr"
        --dir "/usr/share"
        --dir "/usr/share/fonts"
        --dir "/usr/share/fonts/truetype"
        --dir "/usr/share/fonts/truetype/akaos"
        --file "/usr/share/fonts/truetype/akaos/default.ttf:$FONT_FILE:0444"
        --dir "/usr/local"
        --dir "/usr/local/lib"
        --dir "/usr/local/lib/fonts"
        --file "/usr/local/lib/fonts/DejaVuSans.ttf:$FONT_FILE:0444"
        --dir "/usr/share/licenses"
        --dir "/usr/share/licenses/DejaVu"
        --file "/usr/share/licenses/DejaVu/LICENSE.txt:$ROOT_DIR/userspace/licenses/DejaVuSans-LICENSE.txt:0444"
    )
fi

UPSTREAM_X11_INITRAMFS_ARGS=()
if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/libX11.so.6" ]]; then
    echo "[BUILD] Staging the upstream X11 client libraries"
    for x11_library in \
        libX11.so.6 libXext.so.6 libXRes.so.1 libXrender.so.1 libXfixes.so.3 \
        libXdamage.so.1 libXcomposite.so.1 libXi.so.6 libXrandr.so.2 \
        libXcursor.so.1 libXtst.so.6 libxcb.so.1 libXau.so.6 \
        libXdmcp.so.6 libXft.so.2 libXinerama.so.1 ; do
        if [[ -f "$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/$x11_library" ]]; then
            UPSTREAM_X11_INITRAMFS_ARGS+=(
                --file "/lib/$x11_library:$UPSTREAM_GRAPHICS_SYSROOT/usr/local/lib/$x11_library:0444"
            )
        fi
    done
    if [[ -f "$BUILD_DIR/X11_CLIENT.ELF" ]]; then
        UPSTREAM_X11_INITRAMFS_ARGS+=(
            --file "/bin/x11-client:$BUILD_DIR/X11_CLIENT.ELF:exec"
        )
    fi
    if [[ -f "$BUILD_DIR/X11_WM_CLIENT.ELF" ]]; then
        UPSTREAM_X11_INITRAMFS_ARGS+=(
            --file "/bin/x11-wm-client:$BUILD_DIR/X11_WM_CLIENT.ELF:exec"
        )
    fi
fi


fi
echo "[BUILD] akaOS initramfs archive (Desktop: $target_desktop, Session: $session_mode, Software: $menu_software)"

# Synthesize /etc/akaos.conf based on user selection
autostart_val=0
if [[ "$target_desktop" != "none" && "$session_mode" == "desktop" ]]; then
    autostart_val=1
fi
cat > "$BUILD_DIR/akaos.conf" <<EOF
# akaOS System Configuration
HOSTNAME=akaOS
HTTPD_ENABLED=0
HTTPD_PORT=80
TELNETD_ENABLED=0
TELNETD_PORT=23
TFTPD_ENABLED=0
TFTPD_PORT=69
SSHD_ENABLED=0
SSHD_PORT=22
AUDIO_BELL=1
AUTOSTART_DESKTOP=$autostart_val
DESKTOP_ENV=$target_desktop
DESKTOP_THEME=dark
EOF

python3 "$ROOT_DIR/tools/mkinitramfs.py" "$BUILD_DIR/initramfs.img" \
    --exclude-long-vfs-names \
    --dir "/bin" \
    --dir "/etc" \
    --dir "/root:0700" \
    --dir "/home" \
    --dir "/home/user:0755" \
    --dir "/mnt" \
    --dir "/run" \
    --dir "/run/dbus" \
    --dir "/run/user" \
    --dir "/run/user/0:0700" \
    --dir "/tmp" \
    --file "/bin/hello:$BUILD_DIR/HELLO.ELF:exec" \
    --file "/bin/cxxabi-test:$BUILD_DIR/CXXABI_TEST.ELF:exec" \
    --file "/bin/cxx-dynamic-test:$BUILD_DIR/CXX_DYNAMIC_TEST.ELF:exec" \
    --file "/bin/dynamic-hello:$BUILD_DIR/DYNAMIC_HELLO.ELF:exec" \
    --file "/bin/hello-pie:$BUILD_DIR/HELLO_PIE.ELF:exec" \
    --file "/bin/hello-pi:$BUILD_DIR/HELLO_PIE.ELF:exec" \
    --file "/bin/cat:$BUILD_DIR/CAT.ELF:exec" \
    --file "/bin/ls:$BUILD_DIR/LS.ELF:exec" \
    --file "/bin/grep:$BUILD_DIR/GREP.ELF:exec" \
    --file "/bin/head:$BUILD_DIR/HEAD.ELF:exec" \
    --file "/bin/tail:$BUILD_DIR/TAIL.ELF:exec" \
    --file "/bin/wc:$BUILD_DIR/WC.ELF:exec" \
    --file "/bin/uname:$BUILD_DIR/UNAME.ELF:exec" \
    --file "/bin/akafetch:$BUILD_DIR/AKAFETCH.ELF:exec" \
    --file "/bin/whoami:$BUILD_DIR/WHOAMI.ELF:exec" \
    --file "/bin/touch:$BUILD_DIR/TOUCH.ELF:exec" \
    --file "/bin/mkdir:$BUILD_DIR/MKDIR.ELF:exec" \
    --file "/bin/rm:$BUILD_DIR/RM.ELF:exec" \
    --file "/bin/cp:$BUILD_DIR/CP.ELF:exec" \
    --file "/bin/mv:$BUILD_DIR/MV.ELF:exec" \
    --file "/bin/chmod:$BUILD_DIR/CHMOD.ELF:exec" \
    --file "/bin/chown:$BUILD_DIR/CHOWN.ELF:exec" \
    --file "/bin/ted:$BUILD_DIR/TED.ELF:exec" \
    --file "/bin/pwd:$BUILD_DIR/PWD.ELF:exec" \
    --file "/bin/echo:$BUILD_DIR/ECHO.ELF:exec" \
    --file "/bin/clear:$BUILD_DIR/CLEAR.ELF:exec" \
    --file "/bin/sync:$BUILD_DIR/SYNC.ELF:exec" \
    --file "/bin/init:$BUILD_DIR/INIT.ELF:exec" \
    --file "/bin/login:$BUILD_DIR/LOGIN.ELF:4755" \
    --file "/bin/su:$BUILD_DIR/SU.ELF:4755" \
    --file "/bin/passwd:$BUILD_DIR/PASSWD.ELF:4755" \
    --file "/bin/sudo:$BUILD_DIR/SUDO.ELF:4755" \
    --file "/bin/reboot:$BUILD_DIR/REBOOT.ELF:exec" \
    --file "/bin/poweroff:$BUILD_DIR/POWEROFF.ELF:exec" \
    --file "/bin/lumen:$BUILD_DIR/LUMEN.ELF:exec" \
    --file "/bin/palette:$BUILD_DIR/PALETTE.ELF:exec" \
    --file "/bin/aurora:$BUILD_DIR/AURORA.ELF:exec" \
    --file "/bin/terminal:$BUILD_DIR/TERMINAL.ELF:exec" \
    --file "/bin/notify:$BUILD_DIR/NOTIFY.ELF:exec" \
    --file "/bin/beep:$BUILD_DIR/BEEP.ELF:exec" \
    --file "/bin/ping:$BUILD_DIR/PING.ELF:exec" \
    --file "/bin/netstat:$BUILD_DIR/NETSTAT.ELF:exec" \
    --file "/bin/tty:$BUILD_DIR/TTY.ELF:exec" \
    --file "/bin/stty:$BUILD_DIR/STTY.ELF:exec" \
    --file "/bin/ps:$BUILD_DIR/PS.ELF:exec" \
    --file "/bin/akash:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/bash:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/sh:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/rbash:$BUILD_DIR/AKASH.ELF:exec" \
    --file "/bin/nslookup:$BUILD_DIR/NSLOOKUP.ELF:exec" \
    --file "/bin/curl:$BUILD_DIR/CURL.ELF:exec" \
    --file "/bin/pkg:$BUILD_DIR/PKG.ELF:exec" \
    --file "/bin/wget:$BUILD_DIR/WGET.ELF:exec" \
    --file "/bin/httpd:$BUILD_DIR/HTTPD.ELF:exec" \
    --file "/bin/akaos-config:$BUILD_DIR/AKAOS_CONFIG.ELF:exec" \
    --file "/bin/sysmon:$BUILD_DIR/SYSMON.ELF:exec" \
    --file "/bin/ip:$BUILD_DIR/IP.ELF:exec" \
    --file "/bin/mount:$BUILD_DIR/MOUNT.ELF:exec" \
    --file "/bin/umount:$BUILD_DIR/UMOUNT.ELF:exec" \
    --file "/bin/telnetd:$BUILD_DIR/TELNETD.ELF:exec" \
    --file "/bin/tftpd:$BUILD_DIR/TFTPD.ELF:exec" \
    --file "/bin/tftp:$BUILD_DIR/TFTP.ELF:exec" \
    --file "/bin/desktop:$BUILD_DIR/DESKTOP.ELF:exec" \
    --file "/bin/startlxde:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/openbox:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/pcmanfm:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/startlxqt:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/lxqt-panel:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/pcmanfm-qt:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/gnome-shell:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/mutter:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/gnome-session:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/plasma:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/kwin:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/startplasma:$BUILD_DIR/DESKTOP_COMPAT.ELF:exec" \
    --file "/bin/notepad:$BUILD_DIR/NOTEPAD.ELF:exec" \
    --file "/bin/taskmgr:$BUILD_DIR/TASKMGR.ELF:exec" \
    --file "/bin/webviewer:$BUILD_DIR/WEBVIEWER.ELF:exec" \
    --file "/bin/status_cgi:$BUILD_DIR/STATUS_CGI.ELF:exec" \
    --file "/bin/less:$BUILD_DIR/LESS.ELF:exec" \
    --file "/bin/more:$BUILD_DIR/MORE.ELF:exec" \
    --file "/bin/find:$BUILD_DIR/FIND.ELF:exec" \
    --file "/bin/sort:$BUILD_DIR/SORT.ELF:exec" \
    --file "/bin/uniq:$BUILD_DIR/UNIQ.ELF:exec" \
    --file "/bin/xargs:$BUILD_DIR/XARGS.ELF:exec" \
    --file "/bin/test:$BUILD_DIR/TEST.ELF:exec" \
    --file "/bin/[:$BUILD_DIR/TEST.ELF:exec" \
    --file "/bin/sshd:$BUILD_DIR/SSHD.ELF:exec" \
    --file "/bin/ssh:$BUILD_DIR/SSH.ELF:exec" \
    --file "/bin/sleep:$BUILD_DIR/SLEEP.ELF:exec" \
    --file "/bin/which:$BUILD_DIR/WHICH.ELF:exec" \
    --file "/bin/env:$BUILD_DIR/ENV.ELF:exec" \
    --file "/bin/cut:$BUILD_DIR/CUT.ELF:exec" \
    --file "/bin/tr:$BUILD_DIR/TR.ELF:exec" \
    --file "/bin/df:$BUILD_DIR/DF.ELF:exec" \
    --file "/bin/du:$BUILD_DIR/DU.ELF:exec" \
    --file "/bin/kill:$BUILD_DIR/KILL.ELF:exec" \
    --file "/bin/apropos:$BUILD_DIR/APROPOS.ELF:exec" \
    --file "/bin/aspell:$BUILD_DIR/ASPELL.ELF:exec" \
    --file "/bin/at:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/atq:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/atrm:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/batch:$BUILD_DIR/AT.ELF:exec" \
    --file "/bin/awk:$BUILD_DIR/AWK.ELF:exec" \
    --file "/bin/gawk:$BUILD_DIR/AWK.ELF:exec" \
    --file "/bin/basename:$BUILD_DIR/BASENAME.ELF:exec" \
    --file "/bin/base32:$BUILD_DIR/BASE32.ELF:exec" \
    --file "/bin/base64:$BUILD_DIR/BASE64.ELF:exec" \
    --file "/bin/bc:$BUILD_DIR/BC.ELF:exec" \
    --file "/bin/bg:$BUILD_DIR/BG.ELF:exec" \
    --file "/bin/fg:$BUILD_DIR/FG.ELF:exec" \
    --file "/bin/jobs:$BUILD_DIR/JOBS.ELF:exec" \
    --file "/bin/bind:$BUILD_DIR/BIND.ELF:exec" \
    --file "/bin/break:$BUILD_DIR/BREAK.ELF:exec" \
    --file "/bin/builtin:$BUILD_DIR/BUILTIN.ELF:exec" \
    --file "/bin/cal:$BUILD_DIR/CAL.ELF:exec" \
    --file "/bin/ncal:$BUILD_DIR/CAL.ELF:exec" \
    --file "/bin/caller:$BUILD_DIR/CALLER.ELF:exec" \
    --file "/bin/case:$BUILD_DIR/CASE.ELF:exec" \
    --file "/bin/cd:$BUILD_DIR/CD.ELF:exec" \
    --file "/bin/cfdisk:$BUILD_DIR/CFDISK.ELF:exec" \
    --file "/bin/chattr:$BUILD_DIR/CHATTR.ELF:exec" \
    --file "/bin/chgrp:$BUILD_DIR/CHGRP.ELF:exec" \
    --file "/bin/chpasswd:$BUILD_DIR/CHPASSWD.ELF:exec" \
    --file "/bin/chroot:$BUILD_DIR/CHROOT.ELF:exec" \
    --file "/bin/chkconfig:$BUILD_DIR/CHKCONFIG.ELF:exec" \
    --file "/bin/cksum:$BUILD_DIR/CKSUM.ELF:exec" \
    --file "/bin/xmrig:$BUILD_DIR/XMRIG.ELF:exec" \
    --file "/bin/hyprland:$BUILD_DIR/HYPRLAND.ELF:exec" \
    --file "/bin/hyprctl:$BUILD_DIR/HYPRCTL.ELF:exec" \
    --file "/usr/local/bin/hyprland:$BUILD_DIR/HYPRLAND.ELF:exec" \
    --file "/usr/local/bin/hyprctl:$BUILD_DIR/HYPRCTL.ELF:exec" \
    --dir "/var/spool" \
    --dir "/var/spool/at" \
    --dir "/var/spool/at/spool" \
    --file "/bin/pkill:$BUILD_DIR/PKILL.ELF:exec" \
    --file "/bin/uptime:$BUILD_DIR/UPTIME.ELF:exec" \
    --file "/bin/free:$BUILD_DIR/FREE.ELF:exec" \
    --file "/bin/watch:$BUILD_DIR/WATCH.ELF:exec" \
    --file "/bin/sed:$BUILD_DIR/SED.ELF:exec" \
    --file "/bin/diff:$BUILD_DIR/DIFF.ELF:exec" \
    --file "/bin/cmp:$BUILD_DIR/CMP.ELF:exec" \
    --file "/bin/patch:$BUILD_DIR/PATCH.ELF:exec" \
    --file "/bin/tar:$BUILD_DIR/TAR.ELF:exec" \
    --file "/bin/gzip:$BUILD_DIR/GZIP.ELF:exec" \
    --file "/bin/gunzip:$BUILD_DIR/GUNZIP.ELF:exec" \
    --file "/bin/bzip2:$BUILD_DIR/BZIP2.ELF:exec" \
    --file "/bin/bunzip2:$BUILD_DIR/BZIP2.ELF:exec" \
    --file "/bin/bzcat:$BUILD_DIR/BZIP2.ELF:exec" \
    --file "/bin/route:$BUILD_DIR/ROUTE.ELF:exec" \
    --file "/bin/traceroute:$BUILD_DIR/TRACEROUTE.ELF:exec" \
    --file "/bin/dig:$BUILD_DIR/DIG.ELF:exec" \
    --file "/bin/arp:$BUILD_DIR/ARP.ELF:exec" \
    --file "/bin/lscpu:$BUILD_DIR/LSCPU.ELF:exec" \
    --file "/bin/lspci:$BUILD_DIR/LSPCI.ELF:exec" \
    --file "/bin/gpuinfo:$BUILD_DIR/GPUINFO.ELF:exec" \
    --file "/bin/sndinfo:$BUILD_DIR/SNDINFO.ELF:exec" \
    --file "/bin/acpi:$BUILD_DIR/ACPI.ELF:exec" \
    --file "/bin/xsdisplay:$BUILD_DIR/XSDISPLAY.ELF:exec" \
    --file "/bin/dbus-daemon:$BUILD_DIR/DBUS_DAEMON.ELF:exec" \
    --file "/bin/dltest:$BUILD_DIR/DLTEST.ELF:exec" \
    --file "/bin/display-protocols:$BUILD_DIR/DISPLAY_PROTOCOLS.ELF:exec" \
    --file "/bin/xserver:$BUILD_DIR/XSERVER.ELF:exec" \
    --file "/bin/x11-server:$BUILD_DIR/DISPLAY_PROTOCOLS.ELF:exec" \
    --file "/bin/wayland-compositor:$BUILD_DIR/DISPLAY_PROTOCOLS.ELF:exec" \
    --dir "/lib" \
    --file "/lib/ld-akaos.so:$BUILD_DIR/LD_AKAOS.ELF:exec" \
    --file "/lib/libc.so.6:$BUILD_DIR/libc.so.6:0444" \
    --file "/lib/libm.so.6:$BUILD_DIR/libm.so.6:0444" \
    --file "/lib/libc.so:$BUILD_DIR/libc.so.6:0444" \
    --file "/lib/libc++.so.1:$BUILD_DIR/libc++.so.1:0444" \
    --file "/lib/libc++.so:$BUILD_DIR/libc++.so.1:0444" \
    --file "/lib/libc++abi.so.1:$BUILD_DIR/libc++abi.so.1:0444" \
    --file "/lib/libc++abi.so:$BUILD_DIR/libc++abi.so.1:0444" \
    --file "/lib/libunwind.so.1:$BUILD_DIR/libunwind.so.1:0444" \
    --file "/lib/libunwind.so:$BUILD_DIR/libunwind.so.1:0444" \
    --file "/lib/libakaos-cxx.so:$BUILD_DIR/libakaos-cxx.so:0444" \
    --file "/lib/libakaos-demo.so:$BUILD_DIR/libakaos-demo.so:0444" \
    --file "/lib/libakaos-static.so:$BUILD_DIR/libakaos-static.so:0444" \
    --file "/lib/libakaos-versioned.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-dependent.so:$BUILD_DIR/libakaos-dependent.so:0444" \
    --file "/lib/libakaos-loader-fanout.so:$BUILD_DIR/libakaos-loader-fanout.so:0444" \
    --file "/lib/libakaos-loader-dep-0.so:$BUILD_DIR/libakaos-loader-dep-0.so:0444" \
    --file "/lib/libakaos-loader-dep-1.so:$BUILD_DIR/libakaos-loader-dep-1.so:0444" \
    --file "/lib/libakaos-loader-dep-2.so:$BUILD_DIR/libakaos-loader-dep-2.so:0444" \
    --file "/lib/libakaos-loader-dep-3.so:$BUILD_DIR/libakaos-loader-dep-3.so:0444" \
    --file "/lib/libakaos-loader-dep-4.so:$BUILD_DIR/libakaos-loader-dep-4.so:0444" \
    "${UPSTREAM_GLIB_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_HARFBUZZ_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_CAIRO_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_FREETYPE_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_FONTCONFIG_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_FRIBIDI_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_PANGO_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_GRAPHENE_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_GTK_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_GTK3_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_WAYLAND_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_QT_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_X11_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_OPENBOX_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_LXPANEL_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_DBUS_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_LXQT_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_LXQT_PANEL_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_QT_QUICK_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_XKBCOMMON_INITRAMFS_ARGS[@]}" \
    "${UPSTREAM_XKB_DATA_INITRAMFS_ARGS[@]}" \
    --file "/lib/libakaos-capacity-00.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-01.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-02.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-03.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-04.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-05.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-06.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-07.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-08.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-09.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-10.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libakaos-capacity-11.so:$BUILD_DIR/libakaos-versioned.so:0444" \
    --file "/lib/libpthread.so:$BUILD_DIR/libpthread.so:0444" \
    --file "/lib/libpthread.so.0:$BUILD_DIR/libpthread.so:0444" \
    --file "/lib/libglib.so:$BUILD_DIR/libglib.so:0444" \
    --file "/lib/libglib-2.0.so.0:$GLIB_SONAME_IMAGE:0444" \
    --file "/lib/libgobject-2.0.so.0:$GOBJECT_SONAME_IMAGE:0444" \
    --file "/lib/libgio.so:$BUILD_DIR/libgio.so:0444" \
    --file "/lib/libgio-2.0.so.0:$GIO_SONAME_IMAGE:0444" \
    --file "/lib/libgtk.so:$BUILD_DIR/libgtk.so:0444" \
    --file "/lib/libgtk-3.so.0:$BUILD_DIR/libgtk.so:0444" \
    --file "/lib/libgdk-3.so.0:$BUILD_DIR/libgtk.so:0444" \
    --file "/lib/libgtk-4.so:$BUILD_DIR/libgtk-4.so:0444" \
    --file "/lib/libgtk-4.so.1:$BUILD_DIR/libgtk-4.so:0444" \
    --file "/lib/libgdk-4.so.1:$BUILD_DIR/libgtk-4.so:0444" \
    --file "/lib/libQt6Core.so:$BUILD_DIR/libQt6Core.so:0444" \
    --file "/lib/libQt6Core.so.6:$BUILD_DIR/libQt6Core.so:0444" \
    --file "/lib/libQt6Gui.so:$BUILD_DIR/libQt6Gui.so:0444" \
    --file "/lib/libQt6Gui.so.6:$BUILD_DIR/libQt6Gui.so:0444" \
    --file "/lib/libQt6Widgets.so:$BUILD_DIR/libQt6Widgets.so:0444" \
    --file "/lib/libQt6Widgets.so.6:$BUILD_DIR/libQt6Widgets.so:0444" \
        --file "/etc/test_all_181_commands.sh:$ROOT_DIR/userspace/etc/test_all_181_commands.sh:0755" \
--file "/etc/passwd:$ROOT_DIR/userspace/etc/passwd:0644" \
    --file "/etc/group:$ROOT_DIR/userspace/etc/group:0644" \
    --file "/etc/sudoers:$ROOT_DIR/userspace/etc/sudoers:0440" \
    --file "/etc/shadow:$ROOT_DIR/userspace/etc/shadow:0600" \
    --file "/etc/akaos.conf:$BUILD_DIR/akaos.conf:0644" \
    --file "/etc/pkg.conf:$ROOT_DIR/userspace/etc/pkg.conf:0644" \
    --file "/etc/dbus-services.conf:$ROOT_DIR/userspace/etc/dbus-services.conf:0644" \
    --file "/etc/resolv.conf:$ROOT_DIR/userspace/etc/resolv.conf:0644" \
    --file "/etc/fstab:$ROOT_DIR/userspace/etc/fstab:0644" \
    --file "/etc/aurora-apps:$ROOT_DIR/userspace/etc/aurora-apps:0644" \
    --file "/etc/os-release:$ROOT_DIR/userspace/etc/os-release:0644" \
    --file "/etc/hosts:$ROOT_DIR/userspace/etc/hosts:0644" \
    --file "/etc/hostname:$ROOT_DIR/userspace/etc/hostname:0644" \
    --file "/etc/shells:$ROOT_DIR/userspace/etc/shells:0644" \
    --file "/etc/profile:$ROOT_DIR/userspace/etc/profile:0644" \
    --dir "/var" \
    --dir "/var/lib" \
    --dir "/var/lib/pkg" \
    --dir "/var/cache" \
    --dir "/var/cache/pkg" \
    --dir "/var/www" \
    --dir "/var/www/cgi-bin" \
    --file "/var/www/cgi-bin/status:$BUILD_DIR/STATUS_CGI.ELF:exec" \
    --file "/var/www/index.html:$ROOT_DIR/userspace/var/www/index.html:0644" \
    --data "/etc/motd:akaOS XSCore initramfs\n" \
    --data "/etc/akaos-desktop-compat:shim:0444" \
    --dir "/usr/share/man" \
    --file "/usr/share/man/whatis:$ROOT_DIR/userspace/share/man/whatis:0644" \
    --dir "/usr/share/aspell" \
    --file "/usr/share/aspell/en.dict:$ROOT_DIR/userspace/share/aspell/en.dict:0644" \
    --file "/usr/share/aspell/en_US.dict:$ROOT_DIR/userspace/share/aspell/en_US.dict:0644"
(cd "$BUILD_DIR" && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    initramfs.img initramfs_archive.o)

echo "[BUILD] XSCore kernel"

XSCORE_C_SOURCES=(
    "$ROOT_DIR/xscore/kernel/main.c"
    "$ROOT_DIR/xscore/kernel/sched.c"
    "$ROOT_DIR/xscore/kernel/thread.c"
    "$ROOT_DIR/xscore/kernel/file.c"
    "$ROOT_DIR/xscore/kernel/random.c"
    "$ROOT_DIR/xscore/kernel/tty.c"
    "$ROOT_DIR/xscore/kernel/input.c"
    "$ROOT_DIR/xscore/kernel/fb_console.c"
    "$ROOT_DIR/xscore/kernel/process.c"
    "$ROOT_DIR/xscore/kernel/vfs.c"
    "$ROOT_DIR/xscore/kernel/ext2_bridge.c"
    "$ROOT_DIR/xscore/kernel/time.c"
    "$ROOT_DIR/xscore/kernel/bufcache.c"
    "$ROOT_DIR/xscore/kernel/initramfs.c"
    "$ROOT_DIR/xscore/kernel/syscall.c"
    "$ROOT_DIR/xscore/kernel/usercopy.c"
    "$ROOT_DIR/xscore/kernel/user.c"
    "$ROOT_DIR/xscore/kernel/page_cache.c"
    "$ROOT_DIR/xscore/kernel/audio.c"
    "$ROOT_DIR/xscore/kernel/audio_linux.c"
    "$ROOT_DIR/xscore/kernel/net.c"
    "$ROOT_DIR/xscore/kernel/net_core.c"
    "$ROOT_DIR/xscore/kernel/net_virtio_bridge.c"
    "$ROOT_DIR/xscore/kernel/socket.c"
    "$ROOT_DIR/xscore/kernel/virtio_net.c"
    "$ROOT_DIR/xscore/kernel/pci.c"
    "$ROOT_DIR/xscore/kernel/cpu.c"
    "$ROOT_DIR/xscore/kernel/gpu.c"
    "$ROOT_DIR/xscore/kernel/nvme.c"
    "$ROOT_DIR/xscore/kernel/audio_drivers.c"
    "$ROOT_DIR/xscore/kernel/virtio_blk.c"
    "$ROOT_DIR/xscore/kernel/bios.c"
    "$ROOT_DIR/xscore/kernel/acpi.c"
    "$ROOT_DIR/xscore/arch/x86_64/tables.c"
    "$ROOT_DIR/xscore/arch/x86_64/stack.c"
    "$ROOT_DIR/xscore/arch/x86_64/apic.c"
    "$ROOT_DIR/xscore/arch/x86_64/timer.c"
    "$ROOT_DIR/xscore/arch/x86_64/smp.c"
    "$ROOT_DIR/xscore/memory/physical.c"
    "$ROOT_DIR/xscore/memory/virtual.c"
    "$ROOT_DIR/xscore/memory/heap.c"
)
XSCORE_OBJECTS=()
for source in "${XSCORE_C_SOURCES[@]}"; do
    object="$BUILD_DIR/$(basename "${source%.c}").o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
        -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387 \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -I"$INCLUDE_DIR" -I"$ROOT_DIR/xscore/include" -I"$GENERATED_DIR" \
        -c "$source" -o "$object"
    XSCORE_OBJECTS+=("$object")
done

clang --target=x86_64-unknown-none-elf \
    -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
    -c "$ROOT_DIR/xscore/arch/x86_64/entry.S" -o "$BUILD_DIR/entry.o"
XSCORE_OBJECTS+=("$BUILD_DIR/entry.o")

clang --target=x86_64-unknown-none-elf \
    -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
    -c "$ROOT_DIR/xscore/arch/x86_64/smp_trampoline.S" -o "$BUILD_DIR/smp_trampoline.o"
XSCORE_OBJECTS+=("$BUILD_DIR/smp_trampoline.o")

XSCORE_OBJECTS+=("$BUILD_DIR/initramfs_archive.o")

ld.lld -nostdlib -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/xscore/arch/x86_64/linker.ld" \
    "${XSCORE_OBJECTS[@]}" -o "$BUILD_DIR/XSCORE.ELF"

# The BIOS Multiboot2 image enters through the 32-bit protected-mode bridge
# in entry.S. Keep a separately linked ELF so the UEFI image retains its
# normal SysV x86-64 xscore_entry point.
ld.lld -nostdlib -static -z max-page-size=0x1000 \
    -e xscore_multiboot_entry \
    -T "$ROOT_DIR/xscore/arch/x86_64/linker.ld" \
    "${XSCORE_OBJECTS[@]}" -o "$BUILD_DIR/XSCORE-BIOS.ELF"

if [[ "$need_uefi_payload" -eq 1 ]]; then
    echo "[BUILD] akaOS UEFI loader"
    clang --target=x86_64-pc-win32-coff \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fshort-wchar -fno-stack-protector -mno-red-zone \
        -I"$INCLUDE_DIR" -I"$GENERATED_DIR" \
        -c "$ROOT_DIR/boot/uefi/loader.c" -o "$BUILD_DIR/loader.obj"

    lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib \
        /machine:x64 /out:"$ESP_DIR/EFI/BOOT/BOOTX64.EFI" "$BUILD_DIR/loader.obj"

    cp "$BUILD_DIR/XSCORE.ELF" "$ESP_DIR/XSCORE.ELF"

fi

if [[ "$need_fat32_image" -eq 1 ]]; then
    echo "[IMAGE] FAT32 UEFI boot disk"
    kernel_bytes=$(stat -c '%s' "$BUILD_DIR/XSCORE.ELF")
    fat32_headroom_bytes=$((64 * 1024 * 1024))
    fat32_disk_mib=$(((kernel_bytes + fat32_headroom_bytes + 1024 * 1024 - 1) / (1024 * 1024)))
    if (( fat32_disk_mib < 128 )); then
        fat32_disk_mib=128
    fi
    echo "[IMAGE] FAT32 size: ${fat32_disk_mib} MiB (kernel plus 64 MiB headroom)"
    # Build a standard UEFI GPT partitioned disk image with an active EFI System Partition (ESP)
    # This guarantees 100% plug-and-play boot compatibility across Real Hardware PCs, Laptops, VirtualBox, VMware, and QEMU.
    gpt_total_mib=$((fat32_disk_mib + 4))
    gpt_part_end=$((gpt_total_mib - 2))
    truncate -s "${gpt_total_mib}M" "$BUILD_DIR/akaos.img"
    if command -v parted >/dev/null 2>&1; then
        parted -s "$BUILD_DIR/akaos.img" mklabel gpt
        parted -s "$BUILD_DIR/akaos.img" mkpart ESP fat32 1MiB "${gpt_part_end}MiB"
        parted -s "$BUILD_DIR/akaos.img" set 1 esp on
        mformat -i "$BUILD_DIR/akaos.img@@1M" -F -v AKAOS :: >/dev/null 2>&1 || mkfs.fat -F 32 -n AKAOS "$BUILD_DIR/akaos.img" >/dev/null
        mmd -i "$BUILD_DIR/akaos.img@@1M" ::/EFI ::/EFI/BOOT 2>/dev/null || mmd -i "$BUILD_DIR/akaos.img" ::/EFI ::/EFI/BOOT
        mcopy -o -i "$BUILD_DIR/akaos.img@@1M" "$ESP_DIR/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || mcopy -o -i "$BUILD_DIR/akaos.img" "$ESP_DIR/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
        mcopy -o -i "$BUILD_DIR/akaos.img@@1M" "$BUILD_DIR/XSCORE.ELF" ::/XSCORE.ELF 2>/dev/null || mcopy -o -i "$BUILD_DIR/akaos.img" "$BUILD_DIR/XSCORE.ELF" ::/XSCORE.ELF
        if [[ "$bios_test" -eq 1 ]]; then
            : > "$BUILD_DIR/AKAOS.TEST"
            mcopy -o -i "$BUILD_DIR/akaos.img@@1M" "$BUILD_DIR/AKAOS.TEST" ::/AKAOS.TEST 2>/dev/null || mcopy -o -i "$BUILD_DIR/akaos.img" "$BUILD_DIR/AKAOS.TEST" ::/AKAOS.TEST
        fi
        if [[ "$bios_verbose" -eq 1 ]]; then
            : > "$BUILD_DIR/VERBOSE"
            mcopy -o -i "$BUILD_DIR/akaos.img@@1M" "$BUILD_DIR/VERBOSE" ::/VERBOSE 2>/dev/null || mcopy -o -i "$BUILD_DIR/akaos.img" "$BUILD_DIR/VERBOSE" ::/VERBOSE
        fi
    else
        mkfs.fat -F 32 -n AKAOS "$BUILD_DIR/akaos.img" >/dev/null
        mmd -i "$BUILD_DIR/akaos.img" ::/EFI ::/EFI/BOOT
        mcopy -o -i "$BUILD_DIR/akaos.img" "$ESP_DIR/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
        mcopy -o -i "$BUILD_DIR/akaos.img" "$BUILD_DIR/XSCORE.ELF" ::/XSCORE.ELF
        if [[ "$bios_test" -eq 1 ]]; then
            : > "$BUILD_DIR/AKAOS.TEST"
            mcopy -o -i "$BUILD_DIR/akaos.img" "$BUILD_DIR/AKAOS.TEST" ::/AKAOS.TEST
        fi
        if [[ "$bios_verbose" -eq 1 ]]; then
            : > "$BUILD_DIR/VERBOSE"
            mcopy -o -i "$BUILD_DIR/akaos.img" "$BUILD_DIR/VERBOSE" ::/VERBOSE
        fi
    fi

    echo "[DONE] $BUILD_DIR/akaos.img"
    if command -v VBoxManage.exe >/dev/null 2>&1 || [[ -x "/mnt/c/Program Files/Oracle/VirtualBox/VBoxManage.exe" ]]; then
        local vbox_bin="VBoxManage.exe"
        if [[ ! -x "$(command -v VBoxManage.exe 2>/dev/null)" ]]; then
            vbox_bin="/mnt/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"
        fi
        rm -f "$BUILD_DIR/akaos.vdi"
        "$vbox_bin" convertfromraw "$BUILD_DIR/akaos.img" "$BUILD_DIR/akaos.vdi" --format VDI >/dev/null 2>&1 || true
        if [[ -f "$BUILD_DIR/akaos.vdi" ]]; then
            echo "[DONE] $BUILD_DIR/akaos.vdi (VirtualBox native disk)"
        fi
    fi
fi

if [[ "$need_iso_image" -eq 1 ]]; then
    echo "[BUILD] akaOS dual-firmware ISO"
    bios_args=(--no-build)
    if [[ "$bios_test" -eq 1 ]]; then
        bios_args+=(--test)
    fi
    if [[ "$bios_verbose" -eq 1 ]]; then
        bios_args+=(--verbose)
    fi
    bash "$ROOT_DIR/tools/build-bios.sh" "${bios_args[@]}"
fi
