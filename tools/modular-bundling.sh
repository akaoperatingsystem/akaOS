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
            --exclude-prefix "/bin/xfce4-panel"
            --exclude-prefix "/bin/xfce4-session"
            --exclude-prefix "/bin/xfconfd"
            --exclude-prefix "/usr/local/bin/openbox"
            --exclude-prefix "/usr/local/bin/pcmanfm"
            --exclude-prefix "/usr/local/bin/lxpanel"
            --exclude-prefix "/usr/local/bin/startlxde"
            --exclude-prefix "/usr/local/bin/lxsession"
            --exclude-prefix "/usr/local/bin/startlxqt"
            --exclude-prefix "/usr/local/bin/lxqt-panel"
            --exclude-prefix "/usr/local/bin/lxqt-session"
            --exclude-prefix "/usr/local/bin/xfwm4"
            --exclude-prefix "/usr/local/bin/startxfce4"
            --exclude-prefix "/usr/local/bin/xfce4-panel"
            --exclude-prefix "/usr/local/bin/xfce4-session"
            --exclude-prefix "/usr/local/share/themes"
            --exclude-prefix "/usr/local/share/xfce4"
            --exclude-prefix "/usr/local/share/xfwm4"
            --exclude-prefix "/usr/local/share/lxsession"
            --exclude-prefix "/usr/local/share/lxde"
            --exclude-prefix "/usr/local/share/lxpanel"
            --exclude-prefix "/usr/local/share/pcmanfm"
            --exclude-prefix "/usr/local/share/lxqt"
            --exclude-prefix "/usr/share/lxqt"
            --exclude-prefix "/etc/xdg/lxpanel"
            --exclude-prefix "/etc/xdg/pcmanfm"
            --exclude-prefix "/etc/xdg/openbox"
            --exclude-prefix "/etc/xdg/lxqt"
            --exclude-prefix "/etc/xdg/xfce4"
            --exclude-prefix "/etc/xdg/xfwm4"
            --exclude-prefix "/usr/local/lib/libQt6"
            --exclude-prefix "/usr/local/lib/liblxqt"
            --exclude-prefix "/usr/local/lib/libKF6"
            --exclude-prefix "/usr/local/lib/libLayerShell"
            --exclude-prefix "/usr/local/lib/libdbusmenu-lxqt"
            --exclude-prefix "/usr/local/lib/libsysstat"
            --exclude-prefix "/usr/local/lib/libxfce4"
            --exclude-prefix "/usr/local/lib/libxfconf"
            --exclude-prefix "/usr/local/lib/libexo"
            --exclude-prefix "/usr/local/lib/libgarcon"
            --exclude-prefix "/usr/local/lib/libfm"
            --exclude-prefix "/usr/local/lib/libwnck"
            --exclude-prefix "/usr/local/lib/libmenu-cache"
            --exclude-prefix "/usr/local/lib/libkeybinder"
            --exclude-prefix "/usr/local/lib/liblxpanel"
            --exclude-prefix "/usr/local/lib/libobt"
            --exclude-prefix "/usr/local/lib/libobrender"
            --exclude-prefix "/usr/local/lib/libgtk"
            --exclude-prefix "/usr/local/lib/libgdk"
            --exclude-prefix "/usr/local/lib/libatk"
            --exclude-prefix "/usr/local/lib/libepoxy"
            --exclude-prefix "/lib/libQt6"
            --exclude-prefix "/lib/liblxqt"
            --exclude-prefix "/lib/libKF6"
            --exclude-prefix "/lib/libxfce4"
            --exclude-prefix "/lib/libxfconf"
            --exclude-prefix "/lib/libexo"
            --exclude-prefix "/lib/libgarcon"
            --exclude-prefix "/lib/libfm"
            --exclude-prefix "/lib/libwnck"
            --exclude-prefix "/lib/libmenu-cache"
            --exclude-prefix "/lib/libkeybinder"
            --exclude-prefix "/lib/liblxpanel"
            --exclude-prefix "/lib/libobt"
            --exclude-prefix "/lib/libobrender"
            --exclude-prefix "/lib/libgtk"
            --exclude-prefix "/lib/libgdk"
            --exclude-prefix "/lib/libatk"
            --exclude-prefix "/lib/libepoxy"
            --exclude-prefix "/lib/.upstream-gtk3"
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
                --exclude-prefix "/usr/local/bin/lxsession"
                --exclude-prefix "/usr/local/bin/lxsettings-daemon"
                --exclude-prefix "/usr/local/bin/lxsession-xdg-autostart"
                --exclude-prefix "/usr/local/bin/lxsession-logout"
                --exclude-prefix "/usr/local/bin/lxsession-edit"
                --exclude-prefix "/usr/local/bin/lxsession-db"
                --exclude-prefix "/usr/local/bin/lxlock"
                --exclude-prefix "/usr/local/bin/lxsession-default"
                --exclude-prefix "/usr/local/bin/lxsession-default-terminal"
                --exclude-prefix "/usr/local/bin/openbox-lxde"
                --exclude-prefix "/usr/local/bin/lxde-logout"
                --exclude-prefix "/usr/local/libexec/lxsession-xsettings"
                --exclude-prefix "/usr/local/share/lxsession"
                --exclude-prefix "/usr/local/share/lxde"
                --exclude-prefix "/usr/local/share/xsessions/LXDE.desktop"
                --exclude-prefix "/etc/xdg/lxsession"
                --exclude-prefix "/etc/xdg/lxpanel"
            )
            if [[ "$dt" == "openbox" ]]; then
                # Standalone Openbox must not inherit LXDE's file manager or panel libs.
                args+=(
                    --exclude-prefix "/bin/pcmanfm"
                    --exclude-prefix "/usr/local/bin/pcmanfm"
                    --exclude-prefix "/usr/local/share/pcmanfm"
                    --exclude-prefix "/etc/xdg/pcmanfm"
                    --exclude-prefix "/usr/local/lib/libfm"
                    --exclude-prefix "/usr/local/lib/libwnck"
                    --exclude-prefix "/usr/local/lib/libmenu-cache"
                    --exclude-prefix "/usr/local/lib/libkeybinder"
                    --exclude-prefix "/usr/local/lib/liblxpanel"
                    --exclude-prefix "/lib/libfm"
                    --exclude-prefix "/lib/libwnck"
                    --exclude-prefix "/lib/libmenu-cache"
                    --exclude-prefix "/lib/libkeybinder"
                    --exclude-prefix "/lib/liblxpanel"
                )
            else
                args+=(
                    --exclude-prefix "/bin/openbox"
                    --exclude-prefix "/bin/pcmanfm"
                    --exclude-prefix "/usr/local/bin/openbox"
                    --exclude-prefix "/usr/local/bin/pcmanfm"
                    --exclude-prefix "/usr/local/etc/openbox"
                    --exclude-prefix "/usr/local/etc/xdg/openbox"
                    --exclude-prefix "/etc/xdg/openbox"
                    --exclude-prefix "/etc/xdg/pcmanfm"
                    --exclude-prefix "/usr/local/lib/libobt"
                    --exclude-prefix "/usr/local/lib/libobrender"
                    --exclude-prefix "/lib/libobt"
                    --exclude-prefix "/lib/libobrender"
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
                --exclude-prefix "/usr/local/bin/lxqt-leave"
                --exclude-prefix "/usr/local/bin/qtxdg-mat"
                --exclude-prefix "/etc/xdg/lxqt"
                --exclude-prefix "/usr/share/lxqt"
                --exclude-prefix "/usr/local/share/lxqt"
                --exclude-prefix "/usr/local/share/xsessions/lxqt.desktop"
                --exclude-prefix "/usr/local/lib/libQt6"
                --exclude-prefix "/usr/local/lib/liblxqt"
                --exclude-prefix "/usr/local/lib/libKF6"
                --exclude-prefix "/usr/local/lib/libLayerShell"
                --exclude-prefix "/usr/local/lib/libdbusmenu-lxqt"
                --exclude-prefix "/usr/local/lib/libsysstat"
                --exclude-prefix "/usr/local/lib/plugins/kf6"
                --exclude-prefix "/usr/local/plugins"
                --exclude-prefix "/lib/libQt6"
                --exclude-prefix "/lib/liblxqt"
                --exclude-prefix "/lib/libKF6"
            )
        fi

        # Exclude XFCE components if not chosen
        if [[ "$dt" != "xfce" ]]; then
            args+=(
                --exclude-prefix "/bin/xfwm4"
                --exclude-prefix "/bin/xfce4-session"
                --exclude-prefix "/bin/xfce4-panel"
                --exclude-prefix "/bin/xfconfd"
                --exclude-prefix "/usr/local/bin/xfwm4"
                --exclude-prefix "/usr/local/bin/startxfce4"
                --exclude-prefix "/usr/local/bin/xfce4-panel"
                --exclude-prefix "/usr/local/bin/xfce4-session"
                --exclude-prefix "/usr/local/bin/xfconfd"
                --exclude-prefix "/usr/local/bin/xfconf-query"
                --exclude-prefix "/usr/local/bin/xfsettingsd"
                --exclude-prefix "/usr/local/bin/xfdesktop"
                --exclude-prefix "/usr/local/share/xfce4"
                --exclude-prefix "/usr/local/share/xfwm4"
                --exclude-prefix "/usr/local/share/xfconf"
                --exclude-prefix "/etc/xdg/xfce4"
                --exclude-prefix "/etc/xdg/xfwm4"
                --exclude-prefix "/etc/xdg/autostart/xfce"
                --exclude-prefix "/usr/local/share/dbus-1/services/org.xfce"
                --exclude-prefix "/usr/local/lib/libxfce4"
                --exclude-prefix "/usr/local/lib/libxfconf"
                --exclude-prefix "/usr/local/lib/libexo"
                --exclude-prefix "/usr/local/lib/libgarcon"
                --exclude-prefix "/lib/libxfce4"
                --exclude-prefix "/lib/libxfconf"
                --exclude-prefix "/lib/libexo"
                --exclude-prefix "/lib/libgarcon"
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
