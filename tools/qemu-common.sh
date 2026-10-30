#!/usr/bin/env bash

find_ovmf_code() {
    local candidate
    for candidate in \
        /usr/share/OVMF/OVMF_CODE_4M.fd \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/edk2/x64/OVMF_CODE.4m.fd \
        /usr/share/edk2/x64/OVMF_CODE.fd \
        /usr/share/edk2/ovmf/OVMF_CODE_4M.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd \
        /usr/share/qemu/ovmf-x86_64-code.bin; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "Unable to locate OVMF firmware. Run ./run.sh --check-deps for host-specific package guidance." >&2
    return 1
}

find_ovmf_vars() {
    local candidate
    for candidate in \
        /usr/share/OVMF/OVMF_VARS_4M.fd \
        /usr/share/OVMF/OVMF_VARS.fd \
        /usr/share/edk2/x64/OVMF_VARS.4m.fd \
        /usr/share/edk2/x64/OVMF_VARS.fd \
        /usr/share/edk2/ovmf/OVMF_VARS_4M.fd \
        /usr/share/edk2/ovmf/OVMF_VARS.fd \
        /usr/share/qemu/ovmf-x86_64-vars.bin; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "Unable to locate OVMF variable storage. Run ./run.sh --check-deps for host-specific package guidance." >&2
    return 1
}

prepare_ovmf_vars() {
    local source_file="$1"
    local destination_file="$2"
    if [[ ! -f "$destination_file" ]]; then
        cp "$source_file" "$destination_file"
    fi
}
