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
SOURCE_DIR="${AKAOS_LXQT_SESSION_SOURCE_DIR:-$CACHE_ROOT/lxqt-src/lxqt-session-2.4.0-akaos-wayland-v3}"
BUILD_DIR="${AKAOS_LXQT_SESSION_BUILD_DIR:-$CACHE_ROOT/lxqt-build/lxqt-session-2.4.0-akaos-wayland-v3/x86_64-akaos}"
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
TOOLCHAIN_ROOT="${AKAOS_TOOLCHAIN_ROOT:-$ROOT_DIR}"
WAYLAND_HOST_PREFIX="$ROOT_DIR/build/host-tools/wayland-1.26.0"
UPSTREAM_COMMIT="f4818ab24da4848ce1feeef1cefd90b7a01775f6"
UPSTREAM_URL="https://github.com/lxqt/lxqt-session.git"
BUILD_JOBS="${AKAOS_LXQT_BUILD_JOBS:-2}"
XDG_USER_DIRS_UPDATE="${AKAOS_XDG_USER_DIRS_UPDATE:-$(command -v xdg-user-dirs-update || true)}"

for command_name in clang clang++ cmake git llvm-readelf ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing LXQt session build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build pkg-config python3" >&2
        exit 2
    fi
done
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_LXQT_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi
if [[ -z "$XDG_USER_DIRS_UPDATE" || ! -x "$XDG_USER_DIRS_UPDATE" ]]; then
    echo "Missing host xdg-user-dirs-update; on Ubuntu install xdg-user-dirs" >&2
    exit 2
fi

for required_file in \
    "$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    "$ROOT_DIR/tools/qtpaths-akaos.sh" \
    "$ROOT_DIR/tools/patch-upstream-lxqt-session-wayland.py" \
    "$ROOT_DIR/tools/sanitize-elf-rpath.py" \
    "$ROOT_DIR/build/libc.so.6" "$ROOT_DIR/build/libm.so.6" \
    "$ROOT_DIR/build/libc++.so.1" "$ROOT_DIR/build/libc++abi.so.1" \
    "$ROOT_DIR/build/libunwind.so.1" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libdbus-1.so.3.38.3" \
    "$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$QT_TARGET/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Xml/Qt6XmlConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Network/Qt6NetworkConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClient/Qt6WaylandClientConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate/Qt6WaylandGlobalPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate/Qt6WaylandClientPrivateConfig.cmake" \
    "$QT_QML_TARGET/lib/cmake/Qt6Qml/Qt6QmlConfig.cmake" \
    "$QT_QML_TARGET/lib/cmake/Qt6QmlPrivate/Qt6QmlPrivateConfig.cmake" \
    "$QT_QML_TARGET/lib/cmake/Qt6QmlIntegration/Qt6QmlIntegrationConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6WaylandScannerTools/Qt6WaylandScannerToolsConfig.cmake" \
    "$QT_QML_HOST/lib/cmake/Qt6QmlTools/Qt6QmlToolsConfig.cmake" \
    "$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools/lxqt2-build-tools-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt/lxqt-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/qtxdg-tools/qtxdg-tools-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdg/qt6xdg-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdgiconloader/qt6xdgiconloader-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/sysstat-qt6/sysstat-qt6-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6SvgWidgets/Qt6SvgWidgetsConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/KF6WindowSystem/KF6WindowSystemConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/LayerShellQt/LayerShellQtConfig.cmake" \
    "$PLASMA_PROTOCOLS_DIR/PlasmaWaylandProtocolsConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/liblxqt.so.2.4.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libLayerShellQtInterface.so.6.0.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/plugins/wayland-shell-integration/liblayer-shell.so" \
    "$GRAPHICS_SYSROOT/usr/local/share/pkgconfig/wayland-protocols.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/wayland-client.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/xkbcommon.pc" \
    "$WAYLAND_HOST_PREFIX/bin/wayland-scanner"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS LXQt session prerequisite: $required_file" >&2
        echo "Build Qt, LXQt tools, liblxqt, LayerShellQt, Wayland, and KDE protocols first." >&2
        exit 2
    fi
done

mkdir -p "$(dirname "$SOURCE_DIR")" "$(dirname "$BUILD_DIR")"
if [[ ! -d "$SOURCE_DIR/.git" ]]; then
    git clone --no-checkout "$UPSTREAM_URL" "$SOURCE_DIR"
fi
if [[ "$(git -C "$SOURCE_DIR" rev-parse HEAD 2>/dev/null || true)" != "$UPSTREAM_COMMIT" ]]; then
    if ! git -C "$SOURCE_DIR" cat-file -e "$UPSTREAM_COMMIT^{commit}" 2>/dev/null; then
        git -C "$SOURCE_DIR" fetch --depth 1 origin refs/tags/2.4.0
    fi
    git -C "$SOURCE_DIR" checkout --detach "$UPSTREAM_COMMIT"
fi
if [[ "$(git -C "$SOURCE_DIR" rev-parse HEAD)" != "$UPSTREAM_COMMIT" ]]; then
    echo "The lxqt-session source checkout does not match pinned 2.4.0" >&2
    exit 1
fi
python3 "$ROOT_DIR/tools/patch-upstream-lxqt-session-wayland.py" "$SOURCE_DIR"

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export PATH="$WAYLAND_HOST_PREFIX/bin:$LXQT_TOOLS_PREFIX/bin:$QT_HOST/bin:$QT_QML_HOST/bin:$PATH"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$TOOLCHAIN_ROOT" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$QT_QML_TARGET;$QT_HOST;$QT_HOST_TOOLS;$QT_QML_HOST;$LXQT_TOOLS_PREFIX;$KDE_PREFIX;$GRAPHICS_SYSROOT/usr/local" \
    -DQUERY_EXECUTABLE="$ROOT_DIR/tools/qtpaths-akaos.sh" \
    -DECM_DIR="$KDE_PREFIX/share/ECM/cmake" \
    -Dlxqt2-build-tools_DIR="$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools" \
    -Dlxqt_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/lxqt" \
    -Dqtxdg-tools_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/qtxdg-tools" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6CorePrivate_DIR="$QT_TARGET/lib/cmake/Qt6CorePrivate" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6GuiPrivate_DIR="$QT_TARGET/lib/cmake/Qt6GuiPrivate" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6Xml_DIR="$QT_TARGET/lib/cmake/Qt6Xml" \
    -DQt6Network_DIR="$QT_TARGET/lib/cmake/Qt6Network" \
    -DQt6WaylandClient_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClient" \
    -DQt6WaylandGlobalPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate" \
    -DQt6WaylandClientPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate" \
    -DQt6Qml_DIR="$QT_QML_TARGET/lib/cmake/Qt6Qml" \
    -DQt6QmlPrivate_DIR="$QT_QML_TARGET/lib/cmake/Qt6QmlPrivate" \
    -DQt6QmlIntegration_DIR="$QT_QML_TARGET/lib/cmake/Qt6QmlIntegration" \
    -DQt6QmlTools_DIR="$QT_QML_HOST/lib/cmake/Qt6QmlTools" \
    -DQt6LinguistTools_DIR="$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools" \
    -DQt6Xdg_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdg" \
    -DQt6XdgIconLoader_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdgiconloader" \
    -Dsysstat-qt6_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/sysstat-qt6" \
    -DQt6Svg_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg" \
    -DQt6SvgWidgets_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6SvgWidgets" \
    -DKF6WindowSystem_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/KF6WindowSystem" \
    -DLayerShellQt_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/LayerShellQt" \
    -DPlasmaWaylandProtocols_DIR="$PLASMA_PROTOCOLS_DIR" \
    -DXdgUserDirsUpdate_EXECUTABLE="$XDG_USER_DIRS_UPDATE" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DAKAOS_WAYLAND_ONLY=ON \
    -DWITH_LIBUDEV=OFF \
    -DUPDATE_TRANSLATIONS=OFF \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_RPATH=/usr/local/lib:/lib \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

SESSION="$GRAPHICS_SYSROOT/usr/local/bin/lxqt-session"
LEAVE="$GRAPHICS_SYSROOT/usr/local/bin/lxqt-leave"
if [[ ! -f "$SESSION" || ! -f "$LEAVE" || ! -f "$GRAPHICS_SYSROOT/usr/local/bin/startlxqt" ]]; then
    echo "LXQt session install did not produce the session, leave dialog, and launcher" >&2
    exit 1
fi
for binary in "$SESSION" "$LEAVE"; do
    python3 "$ROOT_DIR/tools/sanitize-elf-rpath.py" "$binary" /usr/local/lib:/lib
done
ELF_INFO="$(llvm-readelf -h -d "$SESSION")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO" || \
   ! grep -Eq 'Entry point address:[[:space:]]+0x0*[1-9a-fA-F]' <<<"$ELF_INFO"; then
    echo "LXQt session output has the wrong architecture or no executable entry point" >&2
    exit 1
fi
if grep -Eq 'libX11|libproc2|libprocps' <<<"$ELF_INFO"; then
    echo "Wayland-only LXQt session unexpectedly depends on X11 or procps" >&2
    exit 1
fi

if [[ "$GRAPHICS_SYSROOT" != "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64" ]]; then
    mkdir -p "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/local" \
             "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/share/lxqt" \
             "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/etc/xdg/autostart"
    cp -a "$GRAPHICS_SYSROOT/usr/local/." \
        "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/local/"
    cp -a "$GRAPHICS_SYSROOT/usr/share/lxqt/." \
        "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/share/lxqt/"
    cp -a "$GRAPHICS_SYSROOT/etc/xdg/autostart/." \
        "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/etc/xdg/autostart/"
fi

echo "[DONE] Upstream lxqt-session 2.4.0 Wayland-only x86_64: $SESSION"
