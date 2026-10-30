#!/usr/bin/env bash
# Cross-distribution host resource discovery. This file is sourced.

akaos_find_dejavu_font() {
    local candidate
    if [[ -n "${AKAOS_FONT_FILE:-}" ]]; then
        if [[ -f "$AKAOS_FONT_FILE" ]]; then
            printf '%s\n' "$AKAOS_FONT_FILE"
            return 0
        fi
        echo "AKAOS_FONT_FILE does not exist: $AKAOS_FONT_FILE" >&2
        return 1
    fi

    for candidate in \
        /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
        /usr/share/fonts/dejavu/DejaVuSans.ttf \
        /usr/share/fonts/TTF/DejaVuSans.ttf \
        /usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "Unable to locate DejaVu Sans. Install your distribution's DejaVu font package." >&2
    return 1
}

akaos_find_grub_platform_dir() {
    local platform="$1"
    local override="${2:-}"
    local candidate

    if [[ -n "$override" ]]; then
        if [[ -d "$override" ]]; then
            printf '%s\n' "$override"
            return 0
        fi
        return 1
    fi

    for candidate in \
        "/usr/lib/grub/$platform" \
        "/usr/lib/grub2/$platform" \
        "/usr/lib64/grub2/$platform" \
        "/usr/share/grub/$platform" \
        "/usr/share/grub2/$platform"; do
        if [[ -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}
