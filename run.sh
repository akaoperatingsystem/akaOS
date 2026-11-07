#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

usage() {
    cat <<'EOF'
Usage: ./run.sh [options] [-- QEMU_OPTIONS...]

Run without options in an interactive terminal to open the enhanced configuration menu.

Target Architecture & Firmware:
  --arch NAME                  Target CPU architecture:
                                 x86_64  : 64-bit x86 PC (UEFI / SeaBIOS)
                                 x86_32  : 32-bit x86 PC (UEFI FAT32 / BIOS Hybrid ISO)
                                 aarch64 : 64-bit ARM (QEMU virt / direct kernel)
                                 arm32   : 32-bit ARM (ARMv7-A QEMU virt)
                                 riscv64 : 64-bit RISC-V (RV64GC QEMU virt)
                                 riscv32 : 32-bit RISC-V (RV32GC QEMU virt)
  --firmware NAME              Boot firmware: uefi or bios (default depends on arch)
  --disk-type, --file-type NAME Boot media format: fat32 or iso (x86 media support UEFI/BIOS)

Desktop & Session Options:
  --desktop, --desktop-env NAME
                               Desktop environment to launch in QEMU:
                                 xfce    : XFCE 4.18 (Full desktop session, panel, xfwm4)
                                 lxde    : LXDE Suite (Openbox, LXPanel, PCManFM)
                                 openbox : Standalone Openbox 3.6.1 Window Manager
                                 aurora  : akaOS native compositor & resident desktop
                                 lxqt    : Lightweight Qt6 Desktop Environment
                                 none    : None (No desktop environment / Headless console)
  --session-mode NAME          Default boot session mode:
                                 console : Direct tty1 shell login
                                 desktop : Autostart graphical desktop environment

Software Package Bundle:
  --software, --software-bundle NAME
                               Software bundle selection:
                                 all     : Full suite (All 124+ tools, GUIs, Net, Dev, Audio, WMs)
                                 desktop : Desktop Essentials (GUIs, Editors, Tools, Panels)
                                 net     : Network & Servers (wget, curl, httpd, sshd, telnetd, tftpd)
                                 dev     : Developer & Diagnostics (akafetch, pkg, lscpu, lspci, gpuinfo)
                                 minimal : Minimal Base (Core Unix utilities & akash shell only)
                                 none    : None (Barebones kernel bootstrap only)

Display & Interface Options:
  --display-mode NAME          auto, gui, or headless
  --gui                        Directly open a QEMU graphical display window
  --headless, --console        Run purely over serial console in the terminal

Hardware Device & Emulation Flags:
  --device, --vga NAME         Video display device:
                                 virtio  : VirtIO-GPU hardware accelerator device (default)
                                 std     : Standard QEMU VESA/VGA display adapter
                                 cirrus  : Legacy Cirrus Logic GD5446 display adapter
                                 bochs   : Bochs VBE display adapter
                                 ramfb   : Firmware linear RAM framebuffer
  --net NAME                   Network device backend:
                                 virtio  : VirtIO Network adapter (default)
                                 e1000   : Intel 82540EM Gigabit Ethernet adapter
                                 rtl8139 : Realtek RTL8139 Fast Ethernet adapter
                                 none    : Disable network emulation
  --smp CORES                  Number of virtual CPU cores (default: 1 or 2)
  --mem, --memory SIZE         Guest memory limit (e.g. 256M, 512M, 1G, 2G)
  --kvm, --accel               Enable host hardware virtualization acceleration (KVM/WHPX)
  --list-software              Display comprehensive directory of all native and ported software
  --check-deps                 Check selected host dependencies and exit
  --menu                       Open the interactive configuration menu
  -h, --help                   Display this comprehensive help manual

Examples:
  ./run.sh                                       # Interactive TUI configuration menu
  ./run.sh --arch x86_64 --firmware uefi --gui
  ./run.sh --arch x86_64 --device virtio --smp 4 --mem 1G --gui
  ./run.sh --arch x86_64 --console               # Headless console terminal run
  ./run.sh --arch aarch64 --gui
  ./run.sh --arch riscv64 --console
EOF
}

configure_run_interactively() {
    source "$ROOT_DIR/tools/interactive-ui.sh"
    akaos_ui_require_terminal || exit 2

    local menu_arch=x86_64
    local menu_firmware=uefi
    local menu_disk=fat32
    local menu_desktop=none
    local menu_session=console
    local menu_display=auto
    local menu_vga=virtio
    local menu_smp="${AKAOS_SMP_CORES:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
    local menu_mem=512M
    local action selection summary report
    local -a dependency_args

    while true; do
        if ! action="$(akaos_ui_menu \
            "akaOS Run Configuration" \
            "Select any option to change it, then select Start akaOS." \
            "start" \
            "arch" "Architecture    : [$menu_arch]" \
            "firmware" "Firmware        : [$menu_firmware]" \
            "disk" "File / Disk Type: [$menu_disk]" \
            "desktop" "Desktop Env     : [$menu_desktop]" \
            "session" "Startup Mode    : [$menu_session]" \
            "software" "Software Suite  : [$menu_software]" \
            "display" "Display Window  : [$menu_display]" \
            "vga" "Graphics Device : [$menu_vga]" \
            "smp" "CPU Cores       : [$menu_smp vCPUs]" \
            "mem" "RAM Memory      : [$menu_mem]" \
            "deps" "Check Host Dependencies" \
            "start" "--> Start akaOS QEMU <--" \
            "quit" "Cancel / Exit")"; then
            exit 0
        fi

        case "$action" in
            software)
                if selection="$(akaos_ui_menu \
                    "Software Packages" "Select software bundle for QEMU:" "$menu_software" \
                    "all" "Full Suite (All 124+ Tools, GUIs, Net, Dev, Audio, WMs)" \
                    "apps" "All Apps (All 124+ tools without heavy upstream GUI libs)" \
                    "desktop" "Desktop Essentials (GUIs, Editors, Panels, Tools)" \
                    "net" "Network & Servers (wget, curl, httpd, sshd, telnetd)" \
                    "dev" "Developer & Diagnostics (akafetch, pkg, lscpu, lspci)" \
                    "manual" "Manual Package Selector (Choose any packages individually)" \
                    "minimal" "Minimal Base (Core Unix utilities & shell only)" \
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
                        menu_display="auto"
                    fi
                fi
                ;;
            session)
                if selection="$(akaos_ui_menu \
                    "Select Startup Mode" "How should akaOS start on boot?" "$menu_session" \
                    "console" "Console TTY: Shell login terminal" \
                    "desktop" "Desktop GUI: Autostart graphical desktop")"; then
                    if [[ "$selection" == "desktop" && "$menu_desktop" == "none" ]]; then
                        akaos_ui_message "Configuration Error" "A desktop environment is required for graphical startup, but none is currently selected.\n\nPlease choose a desktop environment from the menu before selecting the Desktop GUI startup mode."
                    elif [[ "$selection" == "console" && "$menu_desktop" != "none" ]]; then
                        if akaos_ui_confirm "Warning: Session Conflict" "Notice: Desktop environment [$menu_desktop] is selected, but the startup mode is set to Console.\n\nQEMU will boot into the text terminal login rather than launching the graphical desktop.\n\nDo you wish to continue?" "Continue" "Back"; then
                            menu_session="$selection"
                            menu_display=headless
                        fi
                    else
                        menu_session="$selection"
                        if [[ "$menu_session" == "console" ]]; then
                            menu_display=headless
                        else
                            menu_display=gui
                        fi
                    fi
                fi
                ;;
            arch)
                if selection="$(akaos_ui_menu \
                    "Target Architecture" "Select the CPU architecture." "$menu_arch" \
                    "x86_64" "64-bit x86 PC" \
                    "x86_32" "32-bit x86 PC" \
                    "aarch64" "64-bit ARM" \
                    "arm32" "32-bit ARM" \
                    "riscv64" "64-bit RISC-V" \
                    "riscv32" "32-bit RISC-V")"; then
                    menu_arch="$selection"
                fi
                ;;
            firmware)
                if selection="$(akaos_ui_menu \
                    "Firmware" "Select the firmware used for this run." "$menu_firmware" \
                    "uefi" "Run with UEFI firmware (GOP Display)" \
                    "bios" "Run with legacy BIOS firmware")"; then
                    menu_firmware="$selection"
                fi
                ;;
            disk)
                if selection="$(akaos_ui_menu \
                    "Disk / File Format" "Select the boot-media format." "$menu_disk" \
                    "fat32" "FAT32 disk image (.img)" \
                    "iso" "Bootable hybrid ISO image (.iso)")"; then
                    menu_disk="$selection"
                fi
                ;;
            display)
                if selection="$(akaos_ui_menu \
                    "Display Mode" "Select how QEMU displays akaOS." "$menu_display" \
                    "auto" "Use GUI window when available, otherwise headless" \
                    "gui" "Open a QEMU GTK graphical window" \
                    "headless" "Run in console serial terminal only")"; then
                    menu_display="$selection"
                fi
                ;;
            vga)
                if selection="$(akaos_ui_menu \
                    "Graphics Hardware Device" "Select emulated GPU / display device." "$menu_vga" \
                    "virtio" "VirtIO-GPU hardware accelerator device" \
                    "std" "Standard QEMU VESA/VGA display adapter" \
                    "cirrus" "Legacy Cirrus Logic GD5446 display adapter" \
                    "bochs" "Bochs VBE display adapter" \
                    "ramfb" "Linear RAM framebuffer device")"; then
                    menu_vga="$selection"
                fi
                ;;
            smp)
                if selection="$(akaos_ui_menu \
                    "CPU Cores (SMP)" "Select virtual CPU core count." "$menu_smp" \
                    "1" "Single Core (1 vCPU)" \
                    "2" "Dual Core (2 vCPUs - Recommended)" \
                    "4" "Quad Core (4 vCPUs)" \
                    "8" "Octa Core (8 vCPUs)")"; then
                    menu_smp="$selection"
                fi
                ;;
            mem)
                if selection="$(akaos_ui_menu \
                    "RAM Memory Limit" "Select allocated virtual RAM." "$menu_mem" \
                    "256M" "256 MB (Minimal)" \
                    "512M" "512 MB (Recommended for Desktop)" \
                    "1G" "1024 MB (1 GB)" \
                    "2G" "2048 MB (2 GB)")"; then
                    menu_mem="$selection"
                fi
                ;;
            deps)
                dependency_args=(--mode run --arch "$menu_arch" \
                    --firmware "$menu_firmware" --disk-type "$menu_disk")
                if report="$(bash "$ROOT_DIR/tools/dependencies.sh" \
                    "${dependency_args[@]}" 2>&1)"; then
                    akaos_ui_message "Host Dependencies" "$report"
                else
                    akaos_ui_message "Missing Host Dependencies" "$report"
                fi
                ;;
            start)
                dependency_args=(--mode run --arch "$menu_arch" \
                    --firmware "$menu_firmware" --disk-type "$menu_disk")
                if ! report="$(bash "$ROOT_DIR/tools/dependencies.sh" \
                    "${dependency_args[@]}" --quiet 2>&1)"; then
                    akaos_ui_message "Missing Host Dependencies" "$report"
                    continue
                fi
                summary="Target Architecture : $menu_arch
Boot Firmware       : $menu_firmware
File / Disk Type    : $menu_disk
Desktop Environment : $menu_desktop
Startup Mode        : $menu_session
Display Window      : $menu_display
Graphics Device     : $menu_vga
Virtual CPU Cores   : $menu_smp vCPUs
Virtual RAM Memory  : $menu_mem"
                if akaos_ui_confirm "Confirm QEMU Run" "$summary" "Start akaOS" "Back"; then
                    INTERACTIVE_RUN_ARGS=(
                        --arch "$menu_arch"
                        --firmware "$menu_firmware"
                        --disk-type "$menu_disk"
                        --desktop "$menu_desktop"
                        --session-mode "$menu_session"
                        --software "$menu_software"
                        --device "$menu_vga"
                        --smp "$menu_smp"
                        --memory "$menu_mem"
                    )
                    if [[ "$menu_display" == "gui" ]]; then
                        INTERACTIVE_RUN_ARGS+=(--gui)
                    else
                        INTERACTIVE_RUN_ARGS+=(--headless)
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
    INTERACTIVE_RUN_ARGS=()
    configure_run_interactively
    set -- "${INTERACTIVE_RUN_ARGS[@]}"
fi

arch=x86_64
firmware=uefi
firmware_set=0
disk_type=auto
disk_type_set=0
menu_desktop=none
menu_session=console
menu_software=all
menu_custom_packages=""
display_mode=auto
gpu_device=virtio
net_device=virtio
smp_cores="${AKAOS_SMP_CORES:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
custom_memory=""
enable_kvm=0
qemu_extra_args=()
check_deps_only=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            if [[ $# -lt 2 ]]; then
                echo "--arch requires a target name (x86_64, x86_32, aarch64, arm32, riscv64, riscv32)" >&2
                exit 2
            fi
            arch="$2"
            shift 2
            ;;
        --firmware)
            if [[ $# -lt 2 ]]; then
                echo "--firmware requires uefi or bios" >&2
                exit 2
            fi
            firmware="$2"
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
            menu_desktop="$2"
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
        --display-mode)
            if [[ $# -lt 2 ]]; then
                echo "--display-mode requires auto, gui, or headless" >&2
                exit 2
            fi
            display_mode="$2"
            shift 2
            ;;
        --headless|--console)
            display_mode=headless
            shift
            ;;
        --gui|--desktop)
            display_mode=gui
            shift
            ;;
        --device|--vga|--gpu)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires virtio, std, cirrus, bochs, or ramfb" >&2
                exit 2
            fi
            gpu_device="$2"
            shift 2
            ;;
        --net|--network)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires virtio, e1000, rtl8139, or none" >&2
                exit 2
            fi
            net_device="$2"
            shift 2
            ;;
        --smp|--cores|--cpus)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires a core count (e.g. 1, 2, 4, 8)" >&2
                exit 2
            fi
            smp_cores="$2"
            shift 2
            ;;
        --mem|--memory|--ram)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires memory size (e.g. 256M, 512M, 1G)" >&2
                exit 2
            fi
            custom_memory="$2"
            shift 2
            ;;
        --kvm|--accel)
            enable_kvm=1
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
        --)
            shift
            qemu_extra_args=("$@")
            break
            ;;
        *)
            echo "Unknown option '$1'; use --help or place QEMU options after --" >&2
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

case "$firmware" in
    uefi|bios) ;;
    *)
        echo "Unsupported firmware '$firmware' (expected uefi or bios)" >&2
        exit 2
        ;;
esac

case "$disk_type" in
    auto|fat32|iso) ;;
    *)
        echo "Unsupported disk type '$disk_type' (expected fat32 or iso)" >&2
        exit 2
        ;;
esac

dependency_firmware="$firmware"
if [[ "$arch" == "x86_32" && "$firmware_set" -eq 0 ]]; then
    dependency_firmware=bios
fi
dependency_disk="$disk_type"
if [[ "$dependency_disk" == "auto" ]]; then
    if [[ "$dependency_firmware" == "bios" ]]; then dependency_disk=iso
    else dependency_disk=fat32
    fi
fi
dependency_args=(--mode run --arch "$arch" --firmware "$dependency_firmware" \
    --disk-type "$dependency_disk")
if [[ "$check_deps_only" -eq 1 ]]; then
    exec bash "$ROOT_DIR/tools/dependencies.sh" "${dependency_args[@]}"
fi
if ! bash "$ROOT_DIR/tools/dependencies.sh" "${dependency_args[@]}" --quiet; then
    exit 1
fi

prepare_storage_image() {
    if [[ -n "${AKAOS_STORAGE_IMAGE:-}" ]]; then
        storage_image="$AKAOS_STORAGE_IMAGE"
        if [[ ! -f "$storage_image" ]]; then
            echo "[FAIL] AKAOS_STORAGE_IMAGE does not exist: $AKAOS_STORAGE_IMAGE" >&2
            exit 1
        fi
    else
        storage_image="$ROOT_DIR/build/akaos-data.img"
        if [[ ! -f "$storage_image" ]]; then
            mkdir -p "$ROOT_DIR/build"
            truncate -s 4M "$storage_image"
            echo "[INFO] Created persistent akaOS data disk: $storage_image" >&2
        fi
    fi
}

if [[ "$arch" == "x86_32" ]]; then
    if [[ "$firmware_set" -eq 0 ]]; then
        firmware=bios
    fi
    if [[ "$disk_type" == "auto" ]]; then
        if [[ "$firmware" == "uefi" ]]; then
            disk_type=fat32
        else
            disk_type=iso
        fi
    fi
    QEMU_32_BIN="qemu-system-i386"
    if ! command -v "$QEMU_32_BIN" >/dev/null 2>&1; then
        QEMU_32_BIN="qemu-system-x86_64"
    fi

    # UEFI can boot the native FAT32 ESP. Legacy BIOS uses the hybrid ISO,
    # because BIOS firmware cannot execute an EFI system partition directly.
    boot_disk_type="$disk_type"
    if [[ "$disk_type" == "auto" ]]; then
        boot_disk_type=iso
    fi
    if [[ "$firmware" == "uefi" && "$boot_disk_type" == "fat32" ]]; then
        if [[ ! -f "$ROOT_DIR/build/akaos32.img" ]]; then
            "$ROOT_DIR/build.sh" --arch x86_32 --firmware uefi --disk-type fat32
        fi
        X86_32_ARGS=(-m 128M -drive "format=raw,file=$ROOT_DIR/build/akaos32.img" -boot c -monitor none -serial stdio)
    else
        if [[ ! -f "$ROOT_DIR/build/akaos32.iso" ]]; then
            "$ROOT_DIR/build.sh" --arch x86_32 --firmware "$firmware" --disk-type iso
        fi
        X86_32_ARGS=(-m 128M -cdrom "$ROOT_DIR/build/akaos32.iso" -boot d -monitor none -serial stdio)
        boot_disk_type=iso
    fi
    # Keep boot media immutable and attach the same writable akaFS sidecar
    # used by every other architecture. UEFI uses transitional VirtIO; the
    # legacy BIOS guest uses the primary IDE controller for reliable PIO I/O.
    prepare_storage_image
    if [[ "$firmware" == "uefi" ]]; then
        X86_32_ARGS+=(
            -drive "if=none,format=raw,file=$storage_image,id=akaos-storage"
            -device virtio-blk-pci,drive=akaos-storage
        )
    else
        X86_32_ARGS+=(
            -drive "format=raw,file=$storage_image,if=ide,index=0,media=disk"
        )
    fi
    X86_32_ARGS+=(
        -netdev user,id=net0,hostfwd=tcp::8080-:80,hostfwd=tcp::2323-:23,hostfwd=udp::6969-:69
        -device virtio-net-pci,netdev=net0,disable-legacy=off
    )
    if [[ "$firmware" == "uefi" ]]; then
        OVMF_CODE="$(find_ovmf_code)"
        OVMF_VARS_SOURCE="$(find_ovmf_vars)"
        OVMF_VARS="$ROOT_DIR/build/OVMF32_VARS.fd"
        prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"
        QEMU_32_BIN="qemu-system-x86_64"
        X86_32_ARGS=(-machine q35
            -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
            -drive "if=pflash,format=raw,file=$OVMF_VARS"
            "${X86_32_ARGS[@]}")
    else
        X86_32_ARGS=(-M pc -vga std "${X86_32_ARGS[@]}")
    fi
    if [[ "$display_mode" == "headless" || ( "$display_mode" == "auto" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ) ]]; then
        echo "[INFO] Booting akaOS x86_32 $firmware/$boot_disk_type headless ($QEMU_32_BIN, 128M). Ctrl+C to exit." >&2
        exec "$QEMU_32_BIN" "${X86_32_ARGS[@]}" -display none "${qemu_extra_args[@]}"
    else
        echo "[INFO] Booting akaOS x86_32 $firmware/$boot_disk_type with a QEMU window." >&2
        exec "$QEMU_32_BIN" "${X86_32_ARGS[@]}" -display gtk "${qemu_extra_args[@]}"
    fi
fi

if [[ "$arch" == "aarch64" ]]; then
    if [[ ! -f "$ROOT_DIR/build/XSCORE_AARCH64.ELF" ]]; then
        build_args=(--arch aarch64 --firmware "$firmware")
        if [[ "$disk_type" != "auto" ]]; then build_args+=(--disk-type "$disk_type"); fi
        "$ROOT_DIR/build.sh" "${build_args[@]}"
    fi
    prepare_storage_image
    AARCH64_QEMU_ARGS=(-M virt -cpu cortex-a57 -m 128M -kernel "$ROOT_DIR/build/XSCORE_AARCH64.ELF" -device ramfb -device virtio-keyboard-device -device virtio-mouse-device -drive "if=none,format=raw,file=$storage_image,id=akaos-storage" -device virtio-blk-device,drive=akaos-storage -netdev user,id=net0,hostfwd=tcp::8080-:80,hostfwd=tcp::2323-:23,hostfwd=udp::6969-:69 -device virtio-net-device,netdev=net0 -serial stdio -monitor none)
    if [[ "$display_mode" == "headless" || ( "$display_mode" == "auto" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ) ]]; then
        echo "[INFO] Booting akaOS AArch64 headless (QEMU virt, 128M). Ctrl+C to exit." >&2
        exec qemu-system-aarch64 "${AARCH64_QEMU_ARGS[@]}" -display none "${qemu_extra_args[@]}"
    else
        echo "[INFO] Booting akaOS AArch64 with a QEMU window." >&2
        exec qemu-system-aarch64 "${AARCH64_QEMU_ARGS[@]}" -display gtk "${qemu_extra_args[@]}"
    fi
fi

if [[ "$arch" == "arm32" ]]; then
    if [[ ! -f "$ROOT_DIR/build/XSCORE_ARM32.ELF" ]]; then
        build_args=(--arch arm32 --firmware "$firmware")
        if [[ "$disk_type" != "auto" ]]; then build_args+=(--disk-type "$disk_type"); fi
        "$ROOT_DIR/build.sh" "${build_args[@]}"
    fi
    prepare_storage_image
    ARM32_QEMU_ARGS=(-M virt,highmem=off -cpu cortex-a7 -m 128M -kernel "$ROOT_DIR/build/XSCORE_ARM32.ELF" -device ramfb -device virtio-keyboard-device -device virtio-mouse-device -drive "if=none,format=raw,file=$storage_image,id=akaos-storage" -device virtio-blk-device,drive=akaos-storage -netdev user,id=net0,hostfwd=tcp::8080-:80,hostfwd=tcp::2323-:23,hostfwd=udp::6969-:69 -device virtio-net-device,netdev=net0 -serial stdio -monitor none)
    if [[ "$display_mode" == "headless" || ( "$display_mode" == "auto" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ) ]]; then
        echo "[INFO] Booting akaOS ARM32 headless (QEMU virt, 128M). Ctrl+C to exit." >&2
        exec qemu-system-arm "${ARM32_QEMU_ARGS[@]}" -display none "${qemu_extra_args[@]}"
    else
        echo "[INFO] Booting akaOS ARM32 with a QEMU window." >&2
        exec qemu-system-arm "${ARM32_QEMU_ARGS[@]}" -display gtk "${qemu_extra_args[@]}"
    fi
fi

if [[ "$arch" == "riscv64" ]]; then
    if [[ ! -f "$ROOT_DIR/build/XSCORE_RISCV64.ELF" ]]; then
        build_args=(--arch riscv64 --firmware "$firmware")
        if [[ "$disk_type" != "auto" ]]; then build_args+=(--disk-type "$disk_type"); fi
        "$ROOT_DIR/build.sh" "${build_args[@]}"
    fi
    prepare_storage_image
    RISCV64_QEMU_ARGS=(-M virt -m 128M -kernel "$ROOT_DIR/build/XSCORE_RISCV64.ELF" -device ramfb -device virtio-keyboard-device -device virtio-mouse-device -drive "if=none,format=raw,file=$storage_image,id=akaos-storage" -device virtio-blk-device,drive=akaos-storage -netdev user,id=net0,hostfwd=tcp::8080-:80,hostfwd=tcp::2323-:23,hostfwd=udp::6969-:69 -device virtio-net-device,netdev=net0 -serial stdio -monitor none)
    if [[ "$display_mode" == "headless" || ( "$display_mode" == "auto" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ) ]]; then
        echo "[INFO] Booting akaOS RISC-V 64 headless (QEMU virt, 128M). Ctrl+C to exit." >&2
        exec qemu-system-riscv64 "${RISCV64_QEMU_ARGS[@]}" -display none "${qemu_extra_args[@]}"
    else
        echo "[INFO] Booting akaOS RISC-V 64 with a QEMU window." >&2
        exec qemu-system-riscv64 "${RISCV64_QEMU_ARGS[@]}" -display gtk "${qemu_extra_args[@]}"
    fi
fi

if [[ "$arch" == "riscv32" ]]; then
    if [[ ! -f "$ROOT_DIR/build/XSCORE_RISCV32.ELF" ]]; then
        build_args=(--arch riscv32 --firmware "$firmware")
        if [[ "$disk_type" != "auto" ]]; then build_args+=(--disk-type "$disk_type"); fi
        "$ROOT_DIR/build.sh" "${build_args[@]}"
    fi
    prepare_storage_image
    RISCV32_QEMU_ARGS=(-M virt -m 128M -kernel "$ROOT_DIR/build/XSCORE_RISCV32.ELF" -device ramfb -device virtio-keyboard-device -device virtio-mouse-device -drive "if=none,format=raw,file=$storage_image,id=akaos-storage" -device virtio-blk-device,drive=akaos-storage -netdev user,id=net0,hostfwd=tcp::8080-:80,hostfwd=tcp::2323-:23,hostfwd=udp::6969-:69 -device virtio-net-device,netdev=net0 -serial stdio -monitor none)
    if [[ "$display_mode" == "headless" || ( "$display_mode" == "auto" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ) ]]; then
        echo "[INFO] Booting akaOS RISC-V 32 headless (QEMU virt, 128M). Ctrl+C to exit." >&2
        exec qemu-system-riscv32 "${RISCV32_QEMU_ARGS[@]}" -display none "${qemu_extra_args[@]}"
    else
        echo "[INFO] Booting akaOS RISC-V 32 with a QEMU window." >&2
        exec qemu-system-riscv32 "${RISCV32_QEMU_ARGS[@]}" -display gtk "${qemu_extra_args[@]}"
    fi
fi

if [[ "$disk_type" == "auto" ]]; then
    if [[ "$firmware" == "bios" ]]; then
        disk_type=iso
    else
        disk_type=fat32
    fi
fi

boot_firmware="$firmware"
boot_disk_type="$disk_type"
if [[ "$firmware" == "bios" && "$disk_type" == "fat32" ]]; then
    # BIOS cannot execute an EFI FAT32 partition; keep the requested selector
    # but choose the ISO fallback as the actual boot medium.
    boot_disk_type=iso
fi
if [[ "$boot_disk_type" == "iso" ]]; then
    if [[ ! -f "$ROOT_DIR/build/akaos.iso" ]]; then
        # BIOS/FAT32 transparently boots the ISO fallback; build only the
        # medium this QEMU instance will actually boot.
        "$ROOT_DIR/build.sh" --arch x86_64 --firmware "$firmware" --disk-type iso
    fi
elif [[ "$firmware" == "uefi" ]]; then
    if [[ ! -f "$ROOT_DIR/build/akaos.img" ]]; then
        "$ROOT_DIR/build.sh" --arch x86_64 --firmware uefi --disk-type fat32
    fi
fi

qemu_machine=q35
if [[ "$boot_firmware" == "bios" ]]; then
    qemu_machine=pc
fi
qemu_memory=512M
if [[ -n "$custom_memory" ]]; then
    qemu_memory="$custom_memory"
fi
QEMU_COMMON_ARGS=(
    -machine "$qemu_machine"
    -m "$qemu_memory"
    -smp "$smp_cores"
    -monitor none
)
if [[ "$enable_kvm" -eq 1 ]]; then
    if [[ -w /dev/kvm ]]; then
        QEMU_COMMON_ARGS+=(-enable-kvm -cpu host)
    fi
fi
case "$gpu_device" in
    virtio) QEMU_COMMON_ARGS+=(-device virtio-vga) ;;
    std) QEMU_COMMON_ARGS+=(-vga std) ;;
    cirrus) QEMU_COMMON_ARGS+=(-vga cirrus) ;;
    bochs) QEMU_COMMON_ARGS+=(-device bochs-display) ;;
    ramfb) QEMU_COMMON_ARGS+=(-device ramfb) ;;
    *) QEMU_COMMON_ARGS+=(-vga std) ;;
esac

if [[ "$boot_firmware" == "uefi" ]]; then
    OVMF_CODE="$(find_ovmf_code)"
    OVMF_VARS_SOURCE="$(find_ovmf_vars)"
    OVMF_VARS="$ROOT_DIR/build/OVMF_VARS.fd"
    prepare_ovmf_vars "$OVMF_VARS_SOURCE" "$OVMF_VARS"
    QEMU_COMMON_ARGS+=(
        -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
        -drive "if=pflash,format=raw,file=$OVMF_VARS"
    )
    if [[ "$boot_disk_type" == "iso" ]]; then
        QEMU_COMMON_ARGS+=(
            -cdrom "$ROOT_DIR/build/akaos.iso"
            -boot d
        )
    else
        QEMU_COMMON_ARGS+=(
            -drive "format=raw,file=$ROOT_DIR/build/akaos.img"
        )
    fi
else
    QEMU_COMMON_ARGS+=(
        -cdrom "$ROOT_DIR/build/akaos.iso"
        -boot d
    )
fi

# Persistent files live on a separate data disk so boot media stay immutable.
prepare_storage_image
if [[ "$boot_firmware" == "bios" ]]; then
    # SeaBIOS/i440FX exposes a dependable primary IDE channel to the x86
    # kernel; use the shared ATA PIO backend for its writable sidecar.
    QEMU_COMMON_ARGS+=(
        -drive "format=raw,file=$storage_image,if=ide,index=0,media=disk"
    )
else
    QEMU_COMMON_ARGS+=(
        -drive "if=none,format=raw,file=$storage_image,id=akaos-storage"
        -device virtio-blk-pci,drive=akaos-storage
    )
fi

QEMU_COMMON_ARGS+=(
    -netdev user,id=net0,hostfwd=tcp::8080-:80,hostfwd=tcp::2323-:23,hostfwd=udp::6969-:69
    -device virtio-net-pci,netdev=net0,disable-legacy=off
)

run_headless() {
    echo "[INFO] Booting akaOS $boot_firmware/$boot_disk_type headless (QEMU $qemu_machine, $qemu_memory). Ctrl+C to exit." >&2
    exec qemu-system-x86_64 -display none "${QEMU_COMMON_ARGS[@]}" -serial stdio "${qemu_extra_args[@]}"
}

run_gui() {
    echo "[INFO] Booting akaOS $boot_firmware/$boot_disk_type with a QEMU $qemu_machine window." >&2
    exec qemu-system-x86_64 -display gtk "${QEMU_COMMON_ARGS[@]}" -serial stdio "${qemu_extra_args[@]}"
}

if [[ "$display_mode" == "headless" ]]; then
    run_headless
fi
if [[ "$display_mode" == "gui" ]]; then
    run_gui
fi

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "[WARN] No display was detected; falling back to headless mode." >&2
    run_headless
fi

run_gui
