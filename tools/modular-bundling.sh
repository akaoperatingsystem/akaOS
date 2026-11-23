#!/usr/bin/env bash
# akaOS Modular Software Bundling Architecture - Strict Per-Environment Isolation

get_initramfs_args() {
    local args=()
    local dt="${target_desktop:-${menu_desktop:-none}}"
    local sw="${menu_software:-all}"

    # 1. No Desktop Profile (--desktop none): Exclude all desktop shell and GUI binaries
    if [[ "$dt" == "none" ]]; then
        args+=(
            --exclude-prefix "/bin/lumen"
            --exclude-prefix "/bin/terminal"
            --exclude-prefix "/bin/notepad"
            --exclude-prefix "/bin/taskmgr"
            --exclude-prefix "/bin/webviewer"
            --exclude-prefix "/bin/palette"
            --exclude-prefix "/bin/status_cgi"
            --exclude-prefix "/usr/local/share/themes"
            --exclude-prefix "/usr/local/lib/libQt6"
            --exclude-prefix "/usr/local/lib/libgtk"
            --exclude-prefix "/usr/local/lib/libgdk"
            --exclude-prefix "/lib/libgtk"
            --exclude-prefix "/lib/libgdk"
            --exclude-prefix "/lib/.upstream-gtk3"
        )
    fi

    # 2. If not Onyx desktop, exclude Lumen & Terminal
    if [[ "$dt" != "onyx" ]]; then
        args+=(
            --exclude-prefix "/bin/lumen"
            --exclude-prefix "/bin/terminal"
        )
    fi

    echo "${args[@]}"
}
