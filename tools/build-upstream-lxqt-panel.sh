#!/usr/bin/env bash
set -euo pipefail
# The build host can carry include and pkg-config search paths for an unrelated
# toolchain.  Clang honours CPATH even under -nostdinc and pkg-config prefers
# PKG_CONFIG_PATH over PKG_CONFIG_LIBDIR, so both are cleared to keep the cross
# build self contained.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_ROOT="${AKAOS_LXQT_CACHE_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos}"
SOURCE_ROOT="$CACHE_ROOT/lxqt-src"
GLOBALKEYS_SOURCE="${AKAOS_LXQT_GLOBALKEYS_SOURCE_DIR:-$SOURCE_ROOT/lxqt-globalkeys-2.4.0-akaos-wayland}"
PANEL_SOURCE="${AKAOS_LXQT_PANEL_SOURCE_DIR:-$SOURCE_ROOT/lxqt-panel-2.4.0-akaos-wayland}"
BUILD_ROOT="${AKAOS_LXQT_BUILD_ROOT:-$CACHE_ROOT/lxqt-build}"
GLOBALKEYS_BUILD="${AKAOS_LXQT_GLOBALKEYS_BUILD_DIR:-$BUILD_ROOT/lxqt-globalkeys-2.4.0-akaos-wayland/x86_64-akaos}"
PANEL_BUILD="${AKAOS_LXQT_PANEL_BUILD_DIR:-$BUILD_ROOT/lxqt-panel-2.4.0-akaos-wayland/x86_64-akaos-crt}"
QT_ROOT="${AKAOS_QT_ROOT:-$CACHE_ROOT/qt6.12.0}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
QT_QML_TARGET="${AKAOS_QT_QML_TARGET_BUILD:-$QT_ROOT/build-target-qtdeclarative}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
QT_HOST_TOOLS="${AKAOS_QT_HOST_TOOLS_BUILD:-$QT_ROOT/build-host-qttools}"
QT_QML_HOST="${AKAOS_QT_QML_HOST_BUILD:-$QT_ROOT/build-host-qtdeclarative}"
LXQT_TOOLS_PREFIX="${AKAOS_LXQT_TOOLS_PREFIX:-$CACHE_ROOT/lxqt-stage/2.4.0/usr}"
KDE_PREFIX="${AKAOS_KDE_HOST_PREFIX:-$CACHE_ROOT/kde/host-prefix}"
PLASMA_PROTOCOLS_DIR="$KDE_PREFIX/share/cmake/PlasmaWaylandProtocols"
GRAPHICS_SYSROOT="${AKAOS_GRAPHICS_SYSROOT:-$ROOT_DIR/build/upstream-sysroot/graphics-x86_64}"
PROJECT_GRAPHICS_SYSROOT="${AKAOS_PROJECT_GRAPHICS_SYSROOT:-$ROOT_DIR/build/upstream-sysroot/graphics-x86_64}"
TOOLCHAIN_ROOT="${AKAOS_TOOLCHAIN_ROOT:-$ROOT_DIR}"
TOOLCHAIN_TOOLS="${AKAOS_TOOLCHAIN_TOOLS_DIR:-$TOOLCHAIN_ROOT/tools}"
WAYLAND_HOST_PREFIX="$ROOT_DIR/build/host-tools/wayland-1.26.0"
GLOBALKEYS_COMMIT="f339bc6bf64bd56c11e7380bf3ff061b45fa4c2e"
PANEL_COMMIT="2262db3beb6805fdeb6a187d9b533b5554d493d3"
BUILD_JOBS="${AKAOS_LXQT_BUILD_JOBS:-4}"

for command_name in clang clang++ cmake git llvm-readelf ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing LXQt panel build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build pkg-config python3" >&2
        exit 2
    fi
done
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_LXQT_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for required_file in \
    "$TOOLCHAIN_TOOLS/qtbase-akaos-x86_64.cmake" \
    "$TOOLCHAIN_TOOLS/qtpaths-akaos.sh" \
    "$TOOLCHAIN_TOOLS/patch-upstream-lxqt-globalkeys-wayland.py" \
    "$TOOLCHAIN_TOOLS/sanitize-elf-rpath.py" \
    "$ROOT_DIR/tools/patch-upstream-lxqt-panel-wayland.py" \
    "$TOOLCHAIN_ROOT/build/libc.so.6" "$TOOLCHAIN_ROOT/build/libm.so.6" \
    "$TOOLCHAIN_ROOT/build/libc++.so.1" "$TOOLCHAIN_ROOT/build/libc++abi.so.1" \
    "$TOOLCHAIN_ROOT/build/libunwind.so.1" \
    "$TOOLCHAIN_ROOT/build/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$QT_TARGET/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Concurrent/Qt6ConcurrentConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Xml/Qt6XmlConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClient/Qt6WaylandClientConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate/Qt6WaylandGlobalPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate/Qt6WaylandClientPrivateConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6WaylandScannerTools/Qt6WaylandScannerToolsConfig.cmake" \
    "$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools/lxqt2-build-tools-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt.so.2.4.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libLayerShellQtInterface.so.6.0.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libdbusmenu-lxqt.so.0.4.0" \
    "$GRAPHICS_SYSROOT/usr/share/cmake/lxqt-menu-data/lxqt-menu-data-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/KF6WindowSystem/KF6WindowSystemConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/LayerShellQt/LayerShellQtConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake" \
    "$PLASMA_PROTOCOLS_DIR/PlasmaWaylandProtocolsConfig.cmake"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing upstream LXQt panel prerequisite: $required_file" >&2
        echo "Build Qt, LXQt build tools, liblxqt, LayerShellQt, menu data, and QtSvg first." >&2
        exit 2
    fi
done

if [[ ! -f "$QT_TARGET/lib/libQt6Concurrent.so.6.12.0" ]]; then
    echo "[BUILD] Qt 6.12 Concurrent target library"
    cmake --build "$QT_TARGET" --target Concurrent --parallel "$BUILD_JOBS"
fi
if [[ ! -f "$QT_TARGET/lib/libQt6Concurrent.so.6.12.0" ]]; then
    echo "Qt Concurrent target did not produce libQt6Concurrent.so.6.12.0" >&2
    exit 1
fi
QT_RUNTIME_PREFIX="$ROOT_DIR/build/upstream-sysroot/qtbase-6.12.0-x86_64/usr/local"
mkdir -p "$QT_RUNTIME_PREFIX/lib"
for library in "$QT_TARGET"/lib/libQt6Concurrent.so*; do
    [[ -e "$library" || -L "$library" ]] || continue
    cp -a "$library" "$QT_RUNTIME_PREFIX/lib/"
done

mkdir -p "$SOURCE_ROOT" "$(dirname "$GLOBALKEYS_BUILD")" "$(dirname "$PANEL_BUILD")"
if [[ ! -d "$GLOBALKEYS_SOURCE/.git" ]]; then
    git clone --no-checkout https://github.com/lxqt/lxqt-globalkeys.git "$GLOBALKEYS_SOURCE"
fi
if [[ "$(git -C "$GLOBALKEYS_SOURCE" rev-parse HEAD 2>/dev/null || true)" != "$GLOBALKEYS_COMMIT" ]]; then
    if ! git -C "$GLOBALKEYS_SOURCE" cat-file -e "$GLOBALKEYS_COMMIT^{commit}" 2>/dev/null; then
        git -C "$GLOBALKEYS_SOURCE" fetch --depth 1 origin refs/tags/2.4.0
    fi
    git -C "$GLOBALKEYS_SOURCE" checkout --detach "$GLOBALKEYS_COMMIT"
fi
if [[ ! -d "$PANEL_SOURCE/.git" ]]; then
    git clone --no-checkout https://github.com/lxqt/lxqt-panel.git "$PANEL_SOURCE"
fi
if [[ "$(git -C "$PANEL_SOURCE" rev-parse HEAD 2>/dev/null || true)" != "$PANEL_COMMIT" ]]; then
    if ! git -C "$PANEL_SOURCE" cat-file -e "$PANEL_COMMIT^{commit}" 2>/dev/null; then
        git -C "$PANEL_SOURCE" fetch --depth 1 origin refs/tags/2.4.0
    fi
    git -C "$PANEL_SOURCE" checkout --detach "$PANEL_COMMIT"
fi
[[ "$(git -C "$GLOBALKEYS_SOURCE" rev-parse HEAD)" == "$GLOBALKEYS_COMMIT" ]]
[[ "$(git -C "$PANEL_SOURCE" rev-parse HEAD)" == "$PANEL_COMMIT" ]]

python3 "$TOOLCHAIN_TOOLS/patch-upstream-lxqt-globalkeys-wayland.py" "$GLOBALKEYS_SOURCE"
python3 "$ROOT_DIR/tools/patch-upstream-lxqt-panel-wayland.py" "$PANEL_SOURCE"

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export PATH="$WAYLAND_HOST_PREFIX/bin:$LXQT_TOOLS_PREFIX/bin:$QT_HOST/bin:$QT_QML_HOST/bin:$PATH"

QT_PREFIX_PATH="$QT_TARGET;$QT_QML_TARGET;$QT_HOST;$QT_HOST_TOOLS;$QT_QML_HOST;$LXQT_TOOLS_PREFIX;$KDE_PREFIX;$GRAPHICS_SYSROOT/usr/local"
if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys.so.2.4.0" || \
      ! -f "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys-ui.so.2.4.0" || \
      ! -f "$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys/lxqt-globalkeys-config.cmake" || \
      ! -f "$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys-ui/lxqt-globalkeys-ui-config.cmake" ]]; then
    if [[ -f "$PROJECT_GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys.so.2.4.0" && \
          -f "$PROJECT_GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys-ui.so.2.4.0" && \
          -f "$PROJECT_GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys/lxqt-globalkeys-config.cmake" && \
          -f "$PROJECT_GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys-ui/lxqt-globalkeys-ui-config.cmake" ]]; then
        echo "[STAGE] Existing LXQt global-keys client/UI libraries into graphics sysroot"
        mkdir -p "$GRAPHICS_SYSROOT/usr/local/lib" \
                 "$GRAPHICS_SYSROOT/usr/local/share/cmake" \
                 "$GRAPHICS_SYSROOT/usr/local/include"
        for library in "$PROJECT_GRAPHICS_SYSROOT"/usr/local/lib/liblxqt-globalkeys*.so*; do
            [[ -e "$library" || -L "$library" ]] && cp -a "$library" "$GRAPHICS_SYSROOT/usr/local/lib/"
        done
        for package in lxqt-globalkeys lxqt-globalkeys-ui; do
            cp -a "$PROJECT_GRAPHICS_SYSROOT/usr/local/share/cmake/$package" \
                "$GRAPHICS_SYSROOT/usr/local/share/cmake/"
        done
        for header_dir in lxqt-globalkeys lxqt-globalkeys-ui; do
            if [[ -d "$PROJECT_GRAPHICS_SYSROOT/usr/local/include/$header_dir" ]]; then
                cp -a "$PROJECT_GRAPHICS_SYSROOT/usr/local/include/$header_dir" \
                    "$GRAPHICS_SYSROOT/usr/local/include/"
            fi
        done
        for pc_file in "$PROJECT_GRAPHICS_SYSROOT"/usr/local/lib/pkgconfig/lxqt-globalkeys*.pc; do
            [[ -f "$pc_file" ]] || continue
            mkdir -p "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig"
            cp -a "$pc_file" "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/"
        done
    fi
fi

if [[ ! -f "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys.so.2.4.0" || \
      ! -f "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys-ui.so.2.4.0" || \
      ! -f "$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys/lxqt-globalkeys-config.cmake" || \
      ! -f "$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys-ui/lxqt-globalkeys-ui-config.cmake" ]]; then
    cmake -S "$GLOBALKEYS_SOURCE" -B "$GLOBALKEYS_BUILD" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_TOOLS/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$TOOLCHAIN_ROOT" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX_PATH" \
    -DQUERY_EXECUTABLE="$TOOLCHAIN_TOOLS/qtpaths-akaos.sh" \
    -DECM_DIR="$KDE_PREFIX/share/ECM/cmake" \
    -Dlxqt2-build-tools_DIR="$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools" \
    -Dlxqt_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6Concurrent_DIR="$QT_TARGET/lib/cmake/Qt6Concurrent" \
    -DQt6Xml_DIR="$QT_TARGET/lib/cmake/Qt6Xml" \
    -DQt6LinguistTools_DIR="$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools" \
    -DAKAOS_WAYLAND_ONLY=ON \
    -DUPDATE_TRANSLATIONS=OFF \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_RPATH=/usr/local/lib:/lib \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
    cmake --build "$GLOBALKEYS_BUILD" --parallel "$BUILD_JOBS"
    DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$GLOBALKEYS_BUILD"
else
    echo "[INFO] Reusing installed LXQt global-keys client/UI libraries"
fi

for library in \
    "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys.so.2.4.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt-globalkeys-ui.so.2.4.0"; do
    if [[ ! -f "$library" ]]; then
        echo "LXQt globalkeys Wayland client build did not produce $library" >&2
        exit 1
    fi
    python3 "$TOOLCHAIN_TOOLS/sanitize-elf-rpath.py" "$library" /usr/local/lib:/lib
    elf_info="$(llvm-readelf -h -d "$library")"
    if ! grep -Fq 'Class:                             ELF64' <<<"$elf_info" || \
       ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$elf_info" || \
       grep -Eq 'libX11|libxcb' <<<"$elf_info"; then
        echo "LXQt globalkeys library has an invalid architecture or X11 dependency: $library" >&2
        exit 1
    fi
done

cmake -S "$PANEL_SOURCE" -B "$PANEL_BUILD" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_TOOLS/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$TOOLCHAIN_ROOT" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX_PATH" \
    -DQUERY_EXECUTABLE="$TOOLCHAIN_TOOLS/qtpaths-akaos.sh" \
    -DECM_DIR="$KDE_PREFIX/share/ECM/cmake" \
    -Dlxqt2-build-tools_DIR="$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools" \
    -Dlxqt_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt" \
    -Dlxqt-menu-data_DIR="$GRAPHICS_SYSROOT/usr/share/cmake/lxqt-menu-data" \
    -Dlxqt-globalkeys_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys" \
    -Dlxqt-globalkeys-ui_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt-globalkeys-ui" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6CorePrivate_DIR="$QT_TARGET/lib/cmake/Qt6CorePrivate" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6GuiPrivate_DIR="$QT_TARGET/lib/cmake/Qt6GuiPrivate" \
    -DQt6WaylandClient_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClient" \
    -DQt6WaylandGlobalPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate" \
    -DQt6WaylandClientPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6Concurrent_DIR="$QT_TARGET/lib/cmake/Qt6Concurrent" \
    -DQt6Xml_DIR="$QT_TARGET/lib/cmake/Qt6Xml" \
    -DQt6LinguistTools_DIR="$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools" \
    -DQt6Svg_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg" \
    -DQt6SvgWidgets_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6SvgWidgets" \
    -DKF6WindowSystem_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/KF6WindowSystem" \
    -DLayerShellQt_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/LayerShellQt" \
    -DPlasmaWaylandProtocols_DIR="$PLASMA_PROTOCOLS_DIR" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DAKAOS_WAYLAND_ONLY=ON \
    -DAKAOS_WAYLAND_SCANNER="$WAYLAND_HOST_PREFIX/bin/wayland-scanner" \
    -DAKAOS_QTWAYLANDSCANNER="$QT_HOST/libexec/qtwaylandscanner" \
    -DCOLORPICKER_PLUGIN=OFF \
    -DCPULOAD_PLUGIN=OFF \
    -DCUSTOMCOMMAND_PLUGIN=ON \
    -DDIRECTORYMENU_PLUGIN=ON \
    -DDOM_PLUGIN=ON \
    -DDESKTOPSWITCH_PLUGIN=OFF \
    -DFANCYMENU_PLUGIN=ON \
    -DKBINDICATOR_PLUGIN=OFF \
    -DMAINMENU_PLUGIN=ON \
    -DMOUNT_PLUGIN=OFF \
    -DQUICKLAUNCH_PLUGIN=ON \
    -DSENSORS_PLUGIN=OFF \
    -DSHOWDESKTOP_PLUGIN=ON \
    -DQEYES_PLUGIN=OFF \
    -DNETWORKMONITOR_PLUGIN=OFF \
    -DSYSSTAT_PLUGIN=ON \
    -DTASKBAR_PLUGIN=ON \
    -DSTATUSNOTIFIER_PLUGIN=ON \
    -DTRAY_PLUGIN=OFF \
    -DVOLUME_PLUGIN=OFF \
    -DWORLDCLOCK_PLUGIN=ON \
    -DSPACER_PLUGIN=ON \
    -DBACKLIGHT_PLUGIN=OFF \
    -DWITH_SCREENSAVER_FALLBACK=OFF \
    -DUPDATE_TRANSLATIONS=OFF \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_RPATH=/usr/local/lib:/lib \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
cmake --build "$PANEL_BUILD" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$PANEL_BUILD"

PANEL="$GRAPHICS_SYSROOT/usr/local/bin/lxqt-panel"
if [[ ! -f "$PANEL" ]]; then
    echo "LXQt panel install did not produce $PANEL" >&2
    exit 1
fi
    python3 "$TOOLCHAIN_TOOLS/sanitize-elf-rpath.py" "$PANEL" /usr/local/lib:/lib
PANEL_INFO="$(llvm-readelf -h -d "$PANEL")"
if ! grep -Eq 'Entry point address:[[:space:]]+0x0*[1-9a-fA-F]' <<<"$PANEL_INFO" || \
   ! grep -Fq 'Class:                             ELF64' <<<"$PANEL_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$PANEL_INFO" || \
   grep -Eq 'libX11|libxcb|libstatgrab|libsensors|libasound|libpulse' <<<"$PANEL_INFO"; then
    echo "LXQt panel output has no executable entry point, an invalid architecture, or a disabled optional backend dependency" >&2
    exit 1
fi

if [[ "$GRAPHICS_SYSROOT" != "$PROJECT_GRAPHICS_SYSROOT" ]]; then
    echo "[STAGE] LXQt panel runtime into project graphics sysroot"
    mkdir -p "$PROJECT_GRAPHICS_SYSROOT/usr/local/bin" \
             "$PROJECT_GRAPHICS_SYSROOT/usr/local/lib" \
             "$PROJECT_GRAPHICS_SYSROOT/usr/local/share/lxqt" \
             "$PROJECT_GRAPHICS_SYSROOT/usr/local/share/applications" \
             "$PROJECT_GRAPHICS_SYSROOT/usr/share/applications" \
             "$PROJECT_GRAPHICS_SYSROOT/usr/share/lxqt/translations" \
             "$PROJECT_GRAPHICS_SYSROOT/etc/xdg/lxqt" \
             "$PROJECT_GRAPHICS_SYSROOT/etc/xdg/autostart"
    cp -a "$PANEL" "$PROJECT_GRAPHICS_SYSROOT/usr/local/bin/"
    for library in "$GRAPHICS_SYSROOT"/usr/local/lib/liblxqt-globalkeys*.so*; do
        [[ -e "$library" || -L "$library" ]] && cp -a "$library" "$PROJECT_GRAPHICS_SYSROOT/usr/local/lib/"
    done
    if [[ -d "$GRAPHICS_SYSROOT/usr/local/lib/lxqt-panel" ]]; then
        cp -a "$GRAPHICS_SYSROOT/usr/local/lib/lxqt-panel" "$PROJECT_GRAPHICS_SYSROOT/usr/local/lib/"
    fi
    if [[ -d "$GRAPHICS_SYSROOT/usr/local/share/lxqt/lxqt-panel" ]]; then
        cp -a "$GRAPHICS_SYSROOT/usr/local/share/lxqt/lxqt-panel" \
            "$PROJECT_GRAPHICS_SYSROOT/usr/local/share/lxqt/"
    fi
    if [[ -f "$GRAPHICS_SYSROOT/usr/share/applications/lxqt-panel.desktop" ]]; then
        cp -a "$GRAPHICS_SYSROOT/usr/share/applications/lxqt-panel.desktop" \
            "$PROJECT_GRAPHICS_SYSROOT/usr/share/applications/"
    fi
    if [[ -f "$GRAPHICS_SYSROOT/etc/xdg/lxqt/panel.conf" ]]; then
        cp -a "$GRAPHICS_SYSROOT/etc/xdg/lxqt/panel.conf" \
            "$PROJECT_GRAPHICS_SYSROOT/etc/xdg/lxqt/"
    fi
    if [[ -f "$GRAPHICS_SYSROOT/etc/xdg/autostart/lxqt-panel.desktop" ]]; then
        cp -a "$GRAPHICS_SYSROOT/etc/xdg/autostart/lxqt-panel.desktop" \
            "$PROJECT_GRAPHICS_SYSROOT/etc/xdg/autostart/"
    fi
    if [[ -d "$GRAPHICS_SYSROOT/usr/share/lxqt/translations/lxqt-panel" ]]; then
        mkdir -p "$PROJECT_GRAPHICS_SYSROOT/usr/share/lxqt/translations"
        cp -a "$GRAPHICS_SYSROOT/usr/share/lxqt/translations/lxqt-panel" \
            "$PROJECT_GRAPHICS_SYSROOT/usr/share/lxqt/translations/"
    fi
fi

echo "[DONE] Upstream LXQt panel 2.4.0 Wayland-only x86_64: $PANEL"
