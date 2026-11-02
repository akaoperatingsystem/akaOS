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
            --menu "$prompt" 22 96 14 \
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
        printf '\nSelect an option (1-%d), or press [q] to cancel [%s]: ' "$count" "$default_item" >/dev/tty
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
            --yesno "$message" 17 84
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
            --msgbox "$message" 22 88
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

akaos_ui_checklist() {
    local title="$1"
    local prompt="$2"
    shift 2

    if akaos_ui_has_whiptail; then
        whiptail \
            --backtitle "akaOS Build & Run Configuration" \
            --title "$title" \
            --checklist "$prompt" 22 96 14 \
            "$@" 3>&1 1>&2 2>&3
        return
    fi

    local -a entries=("$@")
    local count=$(( ${#entries[@]} / 3 ))
    local index answer tag description status
    local -A selected
    for ((index = 0; index < count; ++index)); do
        tag="${entries[index * 3]}"
        status="${entries[index * 3 + 2]}"
        if [[ "$status" == "ON" || "$status" == "on" ]]; then
            selected["$tag"]=1
        fi
    done

    while true; do
        printf '\033[2J\033[H' >/dev/tty
        printf 'akaOS Build & Run Configuration\n\n%s\n%s\n\n' \
            "$title" "$prompt" >/dev/tty
        for ((index = 0; index < count; ++index)); do
            tag="${entries[index * 3]}"
            description="${entries[index * 3 + 1]}"
            local mark=" "
            [[ -n "${selected[$tag]:-}" ]] && mark="*"
            printf '  %2d) [%s] %-14s %s\n' "$((index + 1))" "$mark" "$tag" "$description" >/dev/tty
        done
        printf '\nEnter option number to toggle (1-%d), [a]ll, [n]one, or [c]onfirm selection: ' "$count" >/dev/tty
        IFS= read -r answer </dev/tty || return 130
        if [[ "$answer" == "c" || "$answer" == "C" || -z "$answer" ]]; then
            local result=""
            for ((index = 0; index < count; ++index)); do
                tag="${entries[index * 3]}"
                if [[ -n "${selected[$tag]:-}" ]]; then
                    result+="${result:+ }\"$tag\""
                fi
            done
            printf '%s\n' "$result"
            return 0
        fi
        if [[ "$answer" == "q" || "$answer" == "Q" ]]; then
            return 130
        fi
        if [[ "$answer" == "a" || "$answer" == "A" ]]; then
            for ((index = 0; index < count; ++index)); do
                tag="${entries[index * 3]}"
                selected["$tag"]=1
            done
            continue
        fi
        if [[ "$answer" == "n" || "$answer" == "N" ]]; then
            selected=()
            continue
        fi
        if [[ "$answer" =~ ^[0-9]+$ ]] &&
           (( answer >= 1 && answer <= count )); then
            tag="${entries[(answer - 1) * 3]}"
            if [[ -n "${selected[$tag]:-}" ]]; then
                unset selected["$tag"]
            else
                selected["$tag"]=1
            fi
        fi
    done
}
