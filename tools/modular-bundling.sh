#!/usr/bin/env bash
# akaOS Modular Software Bundling Architecture - Strict Per-Environment Isolation

get_initramfs_args() {
    local args=()
    local dt="${target_desktop:-${menu_desktop:-none}}"
    local sw="${menu_software:-all}"

    # 1. No Desktop Profile (--desktop none): Exclude all desktop environments
    if [[ "$dt" == "none" ]]; then
        args+=(
            --exclude-prefix "/bin/openbox"
            --exclude-prefix "/bin/pcmanfm"
            --exclude-prefix "/bin/lxpanel"
            --exclude-prefix "/bin/startlxde"
            --exclude-prefix "/bin/startlxqt"
            --exclude-prefix "/bin/lxqt-panel"
            --exclude-prefix "/bin/pcmanfm-qt"
            --exclude-prefix "/bin/xfwm4"
            --exclude-prefix "/bin/xfce4-session"
            --exclude-prefix "/bin/xfconfd"
            --exclude-prefix "/usr/local/bin/openbox"
            --exclude-prefix "/usr/local/bin/pcmanfm"
            --exclude-prefix "/usr/local/bin/lxpanel"
            --exclude-prefix "/usr/local/bin/startlxde"
            --exclude-prefix "/usr/local/bin/startlxqt"
            --exclude-prefix "/usr/local/bin/lxqt-panel"
            --exclude-prefix "/usr/local/bin/lxqt-session"
            --exclude-prefix "/usr/local/bin/xfwm4"
            --exclude-prefix "/usr/local/bin/startxfce4"
            --exclude-prefix "/usr/local/bin/xfce4-panel"
            --exclude-prefix "/usr/local/bin/xfce4-session"
            --exclude-prefix "/usr/local/share/themes"
            --exclude-prefix "/etc/xdg/lxpanel"
            --exclude-prefix "/etc/xdg/pcmanfm"
            --exclude-prefix "/etc/xdg/openbox"
            --exclude-prefix "/etc/xdg/lxqt"
        )
    fi

    # 2. Strict Per-Environment Isolation: Bundle ONLY the requested desktop environment
    if [[ "$dt" != "none" ]]; then
        # Exclude LXDE components if not chosen
        if [[ "$dt" != "lxde" ]]; then
            args+=(
                --exclude-prefix "/bin/startlxde"
                --exclude-prefix "/bin/lxpanel"
                --exclude-prefix "/usr/local/bin/startlxde"
                --exclude-prefix "/usr/local/bin/lxpanel"
                --exclude-prefix "/usr/local/bin/lxpanelctl"
                --exclude-prefix "/etc/xdg/lxpanel"
            )
            # If openbox was not chosen either, exclude openbox and pcmanfm
            if [[ "$dt" != "openbox" ]]; then
                args+=(
                    --exclude-prefix "/bin/openbox"
                    --exclude-prefix "/bin/pcmanfm"
                    --exclude-prefix "/usr/local/bin/openbox"
                    --exclude-prefix "/usr/local/bin/pcmanfm"
                    --exclude-prefix "/usr/local/etc/openbox"
                    --exclude-prefix "/usr/local/etc/xdg/openbox"
                    --exclude-prefix "/etc/xdg/openbox"
                    --exclude-prefix "/etc/xdg/pcmanfm"
                )
            fi
        fi

        # Exclude LXQt components if not chosen
        if [[ "$dt" != "lxqt" ]]; then
            args+=(
                --exclude-prefix "/bin/startlxqt"
                --exclude-prefix "/bin/lxqt-panel"
                --exclude-prefix "/bin/pcmanfm-qt"
                --exclude-prefix "/usr/local/bin/startlxqt"
                --exclude-prefix "/usr/local/bin/lxqt-panel"
                --exclude-prefix "/usr/local/bin/lxqt-session"
                --exclude-prefix "/etc/xdg/lxqt"
                --exclude-prefix "/usr/share/lxqt"
            )
        fi

        # Exclude XFCE components if not chosen
        if [[ "$dt" != "xfce" ]]; then
            args+=(
                --exclude-prefix "/bin/xfwm4"
                --exclude-prefix "/bin/xfce4-session"
                --exclude-prefix "/bin/xfconfd"
                --exclude-prefix "/usr/local/bin/xfwm4"
                --exclude-prefix "/usr/local/bin/startxfce4"
                --exclude-prefix "/usr/local/bin/xfce4-panel"
                --exclude-prefix "/usr/local/bin/xfce4-session"
            )
        fi
    fi

    # 3. Minimal / Headless Base (--software minimal)
    if [[ "$sw" == "minimal" ]]; then
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
