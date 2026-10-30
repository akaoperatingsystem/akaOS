#!/usr/bin/env bash
# Cross-distribution dependency checker for akaOS builds and QEMU runs.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/host-common.sh"
source "$ROOT_DIR/tools/qemu-common.sh"

usage() {
    cat <<'EOF'
Usage: ./tools/dependencies.sh --mode build|run [options]

Options:
  --arch NAME          x86_64, x86_32, aarch64, arm32, riscv64, riscv32
  --firmware NAME      uefi, bios, or both for build; uefi or bios for run
  --disk-type NAME     fat32 or iso
  --test               include dependencies needed by the selected test path
  --quiet              print only when dependencies are missing
  -h, --help           show this help

The checker never installs packages. It detects the Linux distribution and
prints a suggested install command when requirements are missing.
EOF
}

mode=build
arch=x86_64
firmware=uefi
disk_type=fat32
quiet=0
test_mode=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode) mode="${2:-}"; shift 2 ;;
        --arch) arch="${2:-}"; shift 2 ;;
        --firmware) firmware="${2:-}"; shift 2 ;;
        --disk-type) disk_type="${2:-}"; shift 2 ;;
        --test) test_mode=1; shift ;;
        --quiet) quiet=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown dependency-check option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

case "$mode" in build|run) ;; *) echo "Unsupported mode '$mode'" >&2; exit 2 ;; esac
case "$arch" in x86_64|x86_32|aarch64|arm32|riscv64|riscv32) ;;
    *) echo "Unsupported architecture '$arch'" >&2; exit 2 ;;
esac
case "$disk_type" in fat32|iso) ;; *) echo "Unsupported disk type '$disk_type'" >&2; exit 2 ;; esac
if [[ "$mode" == "build" ]]; then
    case "$firmware" in uefi|bios|both) ;;
        *) echo "Unsupported build firmware '$firmware'" >&2; exit 2 ;;
    esac
else
    case "$firmware" in uefi|bios) ;;
        *) echo "Unsupported run firmware '$firmware'" >&2; exit 2 ;;
    esac
fi

declare -a missing=()
declare -a required_commands=()
declare -a packages=()

need_base_build=0
need_fat_tools=0
need_grub_bios=0
need_grub_efi=0
need_xorriso=0
need_qemu_x86=0
need_qemu_arm=0
need_qemu_riscv=0
need_ovmf=0

add_unique() {
    local value="$1"
    local existing
    for existing in "${packages[@]:-}"; do
        if [[ "$existing" == "$value" ]]; then return 0; fi
    done
    packages+=("$value")
}

require_command() {
    local command_name="$1"
    required_commands+=("$command_name")
    if ! command -v "$command_name" >/dev/null 2>&1; then
        missing+=("command: $command_name")
    fi
}

require_any_command() {
    local label="$1"
    shift
    local candidate
    for candidate in "$@"; do
        if command -v "$candidate" >/dev/null 2>&1; then return 0; fi
    done
    missing+=("command: $label")
}

if [[ "$mode" == "build" ]]; then
    need_base_build=1
    for command_name in clang clang++ ld.lld objcopy readelf python3 cmake ninja meson git llvm-ar llvm-nm; do
        require_command "$command_name"
    done
    if command -v python3 >/dev/null 2>&1 &&
       ! python3 -c 'import PIL' >/dev/null 2>&1; then
        missing+=("Python module: Pillow (PIL)")
    fi
    if ! akaos_find_dejavu_font >/dev/null 2>&1; then
        missing+=("font: DejaVu Sans")
    fi

    if [[ "$arch" == "x86_64" ]]; then
        need_fat_tools=1
        for command_name in lld-link mkfs.fat mmd mcopy truncate; do
            require_command "$command_name"
        done
        if [[ "$disk_type" == "iso" || "$firmware" == "bios" || "$firmware" == "both" ]]; then
            need_grub_bios=1
            need_xorriso=1
            for command_name in grub-mkrescue grub-file xorriso; do
                require_command "$command_name"
            done
            if ! akaos_find_grub_platform_dir i386-pc \
                "${AKAOS_GRUB_BIOS_DIR:-}" >/dev/null 2>&1; then
                missing+=("GRUB modules: i386-pc")
            fi
        fi
    elif [[ "$arch" == "x86_32" ]]; then
        build_uefi=0
        build_iso=0
        if [[ "$disk_type" == "fat32" || "$firmware" == "uefi" || "$firmware" == "both" ]]; then
            build_uefi=1
        fi
        if [[ "$disk_type" == "iso" || "$firmware" == "bios" || "$firmware" == "both" ]]; then
            build_iso=1
        fi
        if [[ "$build_uefi" -eq 1 ]]; then
            need_fat_tools=1
            need_grub_efi=1
            for command_name in grub-mkstandalone mkfs.fat mmd mcopy truncate; do
                require_command "$command_name"
            done
            if ! akaos_find_grub_platform_dir x86_64-efi "${AKAOS_GRUB_EFI64_DIR:-}" >/dev/null 2>&1; then
                missing+=("GRUB modules: x86_64-efi")
            fi
            if ! akaos_find_grub_platform_dir i386-efi "${AKAOS_GRUB_EFI32_DIR:-}" >/dev/null 2>&1; then
                missing+=("GRUB modules: i386-efi")
            fi
        fi
        if [[ "$build_iso" -eq 1 ]]; then
            need_xorriso=1
            require_command xorriso
        fi
        if [[ "$build_iso" -eq 1 && ( "$firmware" == "bios" || "$firmware" == "both" ) ]]; then
            need_grub_bios=1
            require_command grub-mkimage
            if ! akaos_find_grub_platform_dir i386-pc "${AKAOS_GRUB_BIOS_DIR:-}" >/dev/null 2>&1; then
                missing+=("GRUB modules: i386-pc")
            fi
        fi
    fi

    if [[ "$test_mode" -eq 1 ]]; then
        case "$arch" in
            aarch64)
                need_qemu_arm=1
                require_command qemu-system-aarch64
                ;;
            arm32)
                need_qemu_arm=1
                require_command qemu-system-arm
                ;;
            riscv64)
                need_qemu_riscv=1
                require_command qemu-system-riscv64
                ;;
            riscv32)
                need_qemu_riscv=1
                require_command qemu-system-riscv32
                ;;
        esac
    fi
else
    require_command truncate
    case "$arch" in
        x86_64)
            need_qemu_x86=1
            require_command qemu-system-x86_64
            ;;
        x86_32)
            need_qemu_x86=1
            require_any_command "qemu-system-i386 or qemu-system-x86_64" \
                qemu-system-i386 qemu-system-x86_64
            ;;
        aarch64)
            need_qemu_arm=1
            require_command qemu-system-aarch64
            ;;
        arm32)
            need_qemu_arm=1
            require_command qemu-system-arm
            ;;
        riscv64)
            need_qemu_riscv=1
            require_command qemu-system-riscv64
            ;;
        riscv32)
            need_qemu_riscv=1
            require_command qemu-system-riscv32
            ;;
    esac
    if [[ ( "$arch" == "x86_64" || "$arch" == "x86_32" ) && "$firmware" == "uefi" ]]; then
        need_ovmf=1
        if ! find_ovmf_code >/dev/null 2>&1 || ! find_ovmf_vars >/dev/null 2>&1; then
            missing+=("firmware files: OVMF code and variables")
        fi
    fi
fi

distro_id="${AKAOS_DISTRO_ID:-unknown}"
distro_like=""
distro_name="$distro_id"
if [[ "$distro_id" == "unknown" && -r /etc/os-release ]]; then
    # /etc/os-release is the standard distribution-owned metadata file.
    source /etc/os-release
    distro_id="${ID:-unknown}"
    distro_like="${ID_LIKE:-}"
    distro_name="${PRETTY_NAME:-$distro_id}"
fi

case "$distro_id $distro_like" in
    *ubuntu*|*debian*) distro_family=apt ;;
    *arch*) distro_family=pacman ;;
    *fedora*|*rhel*|*centos*) distro_family=dnf ;;
    *suse*) distro_family=zypper ;;
    *alpine*) distro_family=apk ;;
    *) distro_family=unknown ;;
esac

add_group_packages() {
    local group="$1"
    case "$distro_family:$group" in
        apt:base) for p in clang lld llvm binutils cmake ninja-build meson git python3 python3-pil fonts-dejavu-core; do add_unique "$p"; done ;;
        apt:fat) for p in dosfstools mtools; do add_unique "$p"; done ;;
        apt:grub_bios) for p in grub-pc-bin grub-common grub2-common; do add_unique "$p"; done ;;
        apt:grub_efi) for p in grub-efi-amd64-bin grub-efi-ia32-bin grub-common; do add_unique "$p"; done ;;
        apt:xorriso) add_unique xorriso ;;
        apt:qemu_x86) add_unique qemu-system-x86 ;;
        apt:qemu_arm) add_unique qemu-system-arm ;;
        apt:qemu_riscv) add_unique qemu-system-misc ;;
        apt:ovmf) add_unique ovmf ;;

        pacman:base) for p in clang lld llvm binutils cmake ninja meson git python python-pillow ttf-dejavu; do add_unique "$p"; done ;;
        pacman:fat) for p in dosfstools mtools; do add_unique "$p"; done ;;
        pacman:grub_bios|pacman:grub_efi) add_unique grub ;;
        pacman:xorriso) add_unique xorriso ;;
        pacman:qemu_x86|pacman:qemu_arm|pacman:qemu_riscv) add_unique qemu-full ;;
        pacman:ovmf) add_unique edk2-ovmf ;;

        dnf:base) for p in clang lld llvm binutils cmake ninja-build meson git python3 python3-pillow dejavu-sans-fonts; do add_unique "$p"; done ;;
        dnf:fat) for p in dosfstools mtools; do add_unique "$p"; done ;;
        dnf:grub_bios) for p in grub2-tools grub2-tools-extra grub2-pc-modules; do add_unique "$p"; done ;;
        dnf:grub_efi) for p in grub2-tools grub2-efi-x64-modules grub2-efi-ia32-modules; do add_unique "$p"; done ;;
        dnf:xorriso) add_unique xorriso ;;
        dnf:qemu_x86) add_unique qemu-system-x86 ;;
        dnf:qemu_arm) for p in qemu-system-arm qemu-system-aarch64; do add_unique "$p"; done ;;
        dnf:qemu_riscv) add_unique qemu-system-riscv ;;
        dnf:ovmf) add_unique edk2-ovmf ;;

        zypper:base) for p in clang lld llvm binutils cmake ninja meson git python3 python3-Pillow dejavu-fonts; do add_unique "$p"; done ;;
        zypper:fat) for p in dosfstools mtools; do add_unique "$p"; done ;;
        zypper:grub_bios) for p in grub2 grub2-i386-pc; do add_unique "$p"; done ;;
        zypper:grub_efi) for p in grub2 grub2-x86_64-efi grub2-i386-efi; do add_unique "$p"; done ;;
        zypper:xorriso) add_unique xorriso ;;
        zypper:qemu_x86) add_unique qemu-x86 ;;
        zypper:qemu_arm) add_unique qemu-arm ;;
        zypper:qemu_riscv) add_unique qemu-extra ;;
        zypper:ovmf) add_unique qemu-ovmf-x86_64 ;;

        apk:base) for p in clang lld llvm binutils cmake ninja meson git python3 py3-pillow font-dejavu; do add_unique "$p"; done ;;
        apk:fat) for p in dosfstools mtools; do add_unique "$p"; done ;;
        apk:grub_bios|apk:grub_efi) add_unique grub ;;
        apk:xorriso) add_unique xorriso ;;
        apk:qemu_x86) for p in qemu-system-x86_64 qemu-system-i386; do add_unique "$p"; done ;;
        apk:qemu_arm) for p in qemu-system-aarch64 qemu-system-arm; do add_unique "$p"; done ;;
        apk:qemu_riscv) for p in qemu-system-riscv64 qemu-system-riscv32; do add_unique "$p"; done ;;
        apk:ovmf) add_unique ovmf ;;
    esac
}

if [[ "$need_base_build" -eq 1 ]]; then add_group_packages base; fi
if [[ "$need_fat_tools" -eq 1 ]]; then add_group_packages fat; fi
if [[ "$need_grub_bios" -eq 1 ]]; then add_group_packages grub_bios; fi
if [[ "$need_grub_efi" -eq 1 ]]; then add_group_packages grub_efi; fi
if [[ "$need_xorriso" -eq 1 ]]; then add_group_packages xorriso; fi
if [[ "$need_qemu_x86" -eq 1 ]]; then add_group_packages qemu_x86; fi
if [[ "$need_qemu_arm" -eq 1 ]]; then add_group_packages qemu_arm; fi
if [[ "$need_qemu_riscv" -eq 1 ]]; then add_group_packages qemu_riscv; fi
if [[ "$need_ovmf" -eq 1 ]]; then add_group_packages ovmf; fi

if [[ ${#missing[@]} -eq 0 ]]; then
    if [[ "$quiet" -eq 0 ]]; then
        echo "[OK] $distro_name has all $mode dependencies for $arch/$firmware/$disk_type."
    fi
    exit 0
fi

echo "[FAIL] Missing $mode dependencies for $arch/$firmware/$disk_type:"
for item in "${missing[@]}"; do echo "  - $item"; done
echo
echo "Detected host: $distro_name"
if [[ ${#packages[@]} -gt 0 ]]; then
    package_list="${packages[*]}"
    echo "Suggested prerequisite command:"
    case "$distro_family" in
        apt)
            echo "  sudo apt update"
            echo "  sudo apt install -y $package_list"
            ;;
        pacman) echo "  sudo pacman -S --needed $package_list" ;;
        dnf) echo "  sudo dnf install -y $package_list" ;;
        zypper) echo "  sudo zypper install $package_list" ;;
        apk) echo "  sudo apk add $package_list" ;;
    esac
else
    echo "Install the missing commands with your distribution's package manager."
    echo "See docs/BUILDING.md for known package groups and host setup."
fi
echo
echo "The checker does not run sudo or install packages automatically."
exit 1
