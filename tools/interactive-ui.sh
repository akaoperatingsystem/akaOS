#!/usr/bin/env bash
# Shared terminal UI helpers for build.sh and run.sh. This file is sourced.

akaos_ui_has_whiptail() {
    [[ "${AKAOS_UI_BACKEND:-auto}" != "plain" ]] &&
        command -v whiptail >/dev/null 2>&1
}

akaos_ui_menu() {
    local title="$1"
    local prompt="$2"
    local default_item="$3"
    shift 3

    if akaos_ui_has_whiptail; then
        whiptail \
            --backtitle "akaOS Build & Run Configuration" \
            --title "$title" \
            --default-item "$default_item" \
            --menu "$prompt" 22 78 12 \
            "$@" 3>&1 1>&2 2>&3
        return
    fi

    local -a entries=("$@")
    local count=$(( ${#entries[@]} / 2 ))
    local index answer tag description
    while true; do
        printf '\033[2J\033[H' >/dev/tty
        printf 'akaOS Build & Run Configuration\n\n%s\n%s\n\n' \
            "$title" "$prompt" >/dev/tty
        for ((index = 0; index < count; ++index)); do
            tag="${entries[index * 2]}"
            description="${entries[index * 2 + 1]}"
            printf '  %2d) %-12s %s\n' "$((index + 1))" "$tag" "$description" >/dev/tty
        done
        printf '\nSelect 1-%d, or q to cancel [%s]: ' "$count" "$default_item" >/dev/tty
        IFS= read -r answer </dev/tty || return 130
        if [[ -z "$answer" ]]; then
            printf '%s\n' "$default_item"
            return 0
        fi
        if [[ "$answer" == "q" || "$answer" == "Q" ]]; then
            return 130
        fi
        if [[ "$answer" =~ ^[0-9]+$ ]] &&
           (( answer >= 1 && answer <= count )); then
            printf '%s\n' "${entries[(answer - 1) * 2]}"
            return 0
        fi
        for ((index = 0; index < count; ++index)); do
            if [[ "$answer" == "${entries[index * 2]}" ]]; then
                printf '%s\n' "$answer"
                return 0
            fi
        done
    done
}

akaos_ui_confirm() {
    local title="$1"
    local message="$2"
    local yes_label="$3"
    local no_label="$4"

    if akaos_ui_has_whiptail; then
        whiptail \
            --backtitle "akaOS Build & Run Configuration" \
            --title "$title" \
            --yes-button "$yes_label" \
            --no-button "$no_label" \
            --yesno "$message" 17 72
        return
    fi

    local answer
    printf '\033[2J\033[H%s\n\n%s\n\n%s? [Y/n]: ' \
        "$title" "$message" "$yes_label" >/dev/tty
    IFS= read -r answer </dev/tty || return 1
    [[ -z "$answer" || "$answer" == "y" || "$answer" == "Y" ]]
}

akaos_ui_message() {
    local title="$1"
    local message="$2"

    if akaos_ui_has_whiptail; then
        whiptail \
            --backtitle "akaOS Build & Run Configuration" \
            --title "$title" \
            --scrolltext \
            --msgbox "$message" 22 78
        return
    fi

    printf '\033[2J\033[H%s\n\n%s\n\nPress Enter to continue...' \
        "$title" "$message" >/dev/tty
    IFS= read -r _ </dev/tty || true
}

akaos_ui_require_terminal() {
    if [[ ! -t 0 || ! -t 1 ]]; then
        echo "The interactive menu requires a terminal." >&2
        echo "Use --help to see the non-interactive command options." >&2
        return 1
    fi
}
