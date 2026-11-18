#!/usr/bin/env bash
# akaOS Modular Software Bundling Architecture

get_initramfs_args() {
    local args=()
    local dt="${target_desktop:-${menu_desktop:-none}}"
    local sw="${menu_software:-all}"

    # 1. No Desktop Profile (--desktop none)
    if [[ "$dt" == "none" ]]; then
        args+=(
            --exclude-prefix "/bin/openbox"
            --exclude-prefix "/bin/pcmanfm"
            --exclude-prefix "/bin/lxpanel"
            --exclude-prefix "/bin/xfwm4"
            --exclude-prefix "/bin/xfce4-session"
            --exclude-prefix "/bin/xfconfd"
            --exclude-prefix "/bin/startlxde"
            --exclude-prefix "/bin/startlxqt"
            --exclude-prefix "/bin/lxqt-panel"
            --exclude-prefix "/bin/pcmanfm-qt"
            --exclude-prefix "/bin/gnome-shell"
            --exclude-prefix "/bin/mutter"
            --exclude-prefix "/bin/gnome-session"
            --exclude-prefix "/bin/plasma"
            --exclude-prefix "/bin/kwin"
            --exclude-prefix "/bin/startplasma"
            --exclude-prefix "/bin/hyprland"
            --exclude-prefix "/bin/hyprctl"
            --exclude-prefix "/usr/local/bin/hyprland"
            --exclude-prefix "/usr/local/bin/hyprctl"
            --exclude-prefix "/bin/xserver"
            --exclude-prefix "/bin/x11-server"
            --exclude-prefix "/bin/wayland-compositor"
            --exclude-prefix "/bin/xsdisplay"
            --exclude-prefix "/bin/display-protocols"
            --exclude-prefix "/bin/desktop"
        )
    fi

    # 2. Specific Desktop Bundles (when not 'all' software and not 'none' desktop)
    if [[ "$dt" != "none" && "$sw" != "all" ]]; then
        if [[ "$dt" != "lxde" && "$dt" != "openbox" ]]; then
            args+=(--exclude-prefix "/bin/openbox" --exclude-prefix "/bin/startlxde" --exclude-prefix "/bin/pcmanfm" --exclude-prefix "/bin/lxpanel")
        fi
        if [[ "$dt" != "xfce" ]]; then
            args+=(--exclude-prefix "/bin/xfwm4" --exclude-prefix "/bin/xfce4-session" --exclude-prefix "/bin/xfconfd")
        fi
        if [[ "$dt" != "lxqt" ]]; then
            args+=(--exclude-prefix "/bin/startlxqt" --exclude-prefix "/bin/lxqt-panel" --exclude-prefix "/bin/pcmanfm-qt")
        fi
        if [[ "$dt" != "gnome" ]]; then
            args+=(--exclude-prefix "/bin/gnome-shell" --exclude-prefix "/bin/mutter" --exclude-prefix "/bin/gnome-session")
        fi
        if [[ "$dt" != "kde" && "$dt" != "plasma" ]]; then
            args+=(--exclude-prefix "/bin/plasma" --exclude-prefix "/bin/kwin" --exclude-prefix "/bin/startplasma")
        fi
    fi

    # 3. Headless / Console Bundles (--software minimal, --software net, --software dev)
    if [[ "$sw" == "minimal" || "$sw" == "net" || "$sw" == "dev" ]]; then
        args+=(
            --exclude-prefix "/bin/notepad"
            --exclude-prefix "/bin/webviewer"
            --exclude-prefix "/bin/taskmgr"
            --exclude-prefix "/bin/palette"
            --exclude-prefix "/lib/libgtk"
            --exclude-prefix "/lib/libgdk"
            --exclude-prefix "/lib/libQt6"
            --exclude-prefix "/lib/libcairo"
            --exclude-prefix "/lib/libpango"
            --exclude-prefix "/lib/libfreetype"
            --exclude-prefix "/lib/libglib"
            --exclude-prefix "/lib/libgobject"
            --exclude-prefix "/lib/libgio"
        )
    fi

    echo "${args[@]}"
}
