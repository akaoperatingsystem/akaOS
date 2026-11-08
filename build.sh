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
build_user_program weston "$ROOT_DIR/userspace/bin/weston/main.c" WESTON.ELFb u i l d _ u s e r _ p r o g r a m   s w a y   " $ R O O T _ D I R / u s e r s p a c e / b i n / s w a y / m a i n . c "   S W A Y . E L F 
 b u i l d _ u s e r _ p r o g r a m   r i v e r   " $ R O O T _ D I R / u s e r s p a c e / b i n / r i v e r / m a i n . c "   R I V E R . E L F 
 b u i l d _ u s e r _ p r o g r a m   n i r i   " $ R O O T _ D I R / u s e r s p a c e / b i n / n i r i / m a i n . c "   N I R I . E L F 
 b u i l d _ u s e r _ p r o g r a m   w a y f i r e   " $ R O O T _ D I R / u s e r s p a c e / b i n / w a y f i r e / m a i n . c "   W A Y F I R E . E L F 
 b u i l d _ u s e r _ p r o g r a m   l a b w c   " $ R O O T _ D I R / u s e r s p a c e / b i n / l a b w c / m a i n . c "   L A B W C . E L F 
 b u i l d _ u s e r _ p r o g r a m   w e s t o n   " $ R O O T _ D I R / u s e r s p a c e / b i n / w e s t o n / m a i n . c "   W E S T O N . E L F 
 b u i l d _ u s e r _ p r o g r a m   q t i l e   " $ R O O T _ D I R / u s e r s p a c e / b i n / q t i l e / m a i n . c "   Q T I L E . E L F 
 b u i l d _ u s e r _ p r o g r a m   a w e s o m e   " $ R O O T _ D I R / u s e r s p a c e / b i n / a w e s o m e / m a i n . c "   A W E S O M E . E L F 
 b u i l d _ u s e r _ p r o g r a m   x m o n a d   " $ R O O T _ D I R / u s e r s p a c e / b i n / x m o n a d / m a i n . c "   X M O N A D . E L F 
 b u i l d _ u s e r _ p r o g r a m   h e r b s t l u f t w m   " $ R O O T _ D I R / u s e r s p a c e / b i n / h e r b s t l u f t w m / m a i n . c "   H E R B S T L U F T W M . E L F 
 b u i l d _ u s e r _ p r o g r a m   s p e c t r w m   " $ R O O T _ D I R / u s e r s p a c e / b i n / s p e c t r w m / m a i n . c "   S P E C T R W M . E L F 
 b u i l d _ u s e r _ p r o g r a m   f l u x b o x   " $ R O O T _ D I R / u s e r s p a c e / b i n / f l u x b o x / m a i n . c "   F L U X B O X . E L F 
 b u i l d _ u s e r _ p r o g r a m   i c e w m   " $ R O O T _ D I R / u s e r s p a c e / b i n / i c e w m / m a i n . c "   I C E W M . E L F 
 b u i l d _ u s e r _ p r o g r a m   j w m   " $ R O O T _ D I R / u s e r s p a c e / b i n / j w m / m a i n . c "   J W M . E L F 
 b u i l d _ u s e r _ p r o g r a m   w m a k e r   " $ R O O T _ D I R / u s e r s p a c e / b i n / w m a k e r / m a i n . c "   W M A K E R . E L F 
 b u i l d _ u s e r _ p r o g r a m   f v w m   " $ R O O T _ D I R / u s e r s p a c e / b i n / f v w m / m a i n . c "   F V W M . E L F 
 b u i l d _ u s e r _ p r o g r a m   b u d g i e   " $ R O O T _ D I R / u s e r s p a c e / b i n / b u d g i e / m a i n . c "   B U D G I E . E L F 
 b u i l d _ u s e r _ p r o g r a m   c o s m i c   " $ R O O T _ D I R / u s e r s p a c e / b i n / c o s m i c / m a i n . c "   C O S M I C . E L F 
 b u i l d _ u s e r _ p r o g r a m   d e e p i n   " $ R O O T _ D I R / u s e r s p a c e / b i n / d e e p i n / m a i n . c "   D E E P I N . E L F 
 b u i l d _ u s e r _ p r o g r a m   p a n t h e o n   " $ R O O T _ D I R / u s e r s p a c e / b i n / p a n t h e o n / m a i n . c "   P A N T H E O N . E L F 
 b u i l d _ u s e r _ p r o g r a m   u k u i   " $ R O O T _ D I R / u s e r s p a c e / b i n / u k u i / m a i n . c "   U K U I . E L F 
 b u i l d _ u s e r _ p r o g r a m   e n l i g h t e n m e n t   " $ R O O T _ D I R / u s e r s p a c e / b i n / e n l i g h t e n m e n t / m a i n . c "   E N L I G H T E N M E N T . E L F 
  
 