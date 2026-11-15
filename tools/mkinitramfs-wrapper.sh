#!/usr/bin/env bash
args=()
if [[ "$menu_desktop" == "none" ]]; then
    args+=(--exclude-prefix "/bin/aurora" --exclude-prefix "/bin/lumen" --exclude-prefix "/bin/openbox" --exclude-prefix "/bin/pcmanfm" --exclude-prefix "/bin/lxpanel")
fi
if [[ "$menu_software" == "minimal" || "$menu_software" == "net" || "$menu_software" == "dev" ]]; then
    args+=(--exclude-prefix "/lib/libgtk" --exclude-prefix "/lib/libqt" --exclude-prefix "/lib/libcairo" --exclude-prefix "/lib/libpango" --exclude-prefix "/lib/libfreetype")
fi
python3 "$ROOT_DIR/tools/mkinitramfs.py" "$@" "${args[@]}"
