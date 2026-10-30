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
CACHE_ROOT="${AKAOS_LAYERSHELLQT_CACHE_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos}"
SOURCE_DIR="${AKAOS_LAYERSHELLQT_SOURCE_DIR:-$CACHE_ROOT/layershellqt-src/layer-shell-qt-6.0.0}"
BUILD_DIR="${AKAOS_LAYERSHELLQT_BUILD_DIR:-$CACHE_ROOT/layershellqt-build/layer-shell-qt-6.0.0/x86_64-akaos}"
QT_ROOT="${AKAOS_QT_ROOT:-$CACHE_ROOT/qt6.12.0}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
QT_QML_TARGET="${AKAOS_QT_QML_TARGET_BUILD:-$QT_ROOT/build-target-qtdeclarative}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
QT_QML_HOST="${AKAOS_QT_QML_HOST_BUILD:-$QT_ROOT/build-host-qtdeclarative}"
KDE_PREFIX="${AKAOS_KDE_HOST_PREFIX:-$CACHE_ROOT/kde/host-prefix}"
GRAPHICS_SYSROOT="${AKAOS_GRAPHICS_SYSROOT:-$ROOT_DIR/build/upstream-sysroot/graphics-x86_64}"
TOOLCHAIN_ROOT="${AKAOS_TOOLCHAIN_ROOT:-$ROOT_DIR}"
WAYLAND_HOST_PREFIX="$ROOT_DIR/build/host-tools/wayland-1.26.0"
UPSTREAM_COMMIT="ec1a2d02fb53c24b28ab271f57848057f37bdb4f"
UPSTREAM_URL="https://github.com/KDE/layer-shell-qt.git"
BUILD_JOBS="${AKAOS_LAYERSHELLQT_BUILD_JOBS:-2}"

for command_name in clang clang++ cmake git llvm-readelf ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing LayerShellQt build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build pkg-config python3" >&2
        exit 2
    fi
done

if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_LAYERSHELLQT_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for required_file in \
    "$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    "$ROOT_DIR/build/libc.so.6" "$ROOT_DIR/build/libm.so.6" \
    "$ROOT_DIR/build/libc++.so.1" "$ROOT_DIR/build/libc++abi.so.1" \
    "$ROOT_DIR/build/libunwind.so.1" \
    "$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$QT_TARGET/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Network/Qt6NetworkConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClient/Qt6WaylandClientConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate/Qt6WaylandGlobalPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate/Qt6WaylandClientPrivateConfig.cmake" \
    "$QT_QML_TARGET/lib/cmake/Qt6Qml/Qt6QmlConfig.cmake" \
    "$QT_QML_TARGET/lib/cmake/Qt6QmlPrivate/Qt6QmlPrivateConfig.cmake" \
    "$QT_QML_TARGET/lib/cmake/Qt6QmlIntegration/Qt6QmlIntegrationConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_HOST/lib/cmake/Qt6WaylandScannerTools/Qt6WaylandScannerToolsConfig.cmake" \
    "$QT_QML_HOST/lib/cmake/Qt6Qml/Qt6QmlConfig.cmake" \
    "$QT_QML_HOST/lib/cmake/Qt6QmlTools/Qt6QmlToolsConfig.cmake" \
    "$KDE_PREFIX/share/ECM/cmake/ECMConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/wayland-client.pc" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/xkbcommon.pc" \
    "$GRAPHICS_SYSROOT/usr/local/share/pkgconfig/wayland-protocols.pc" \
    "$GRAPHICS_SYSROOT/usr/local/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" \
    "$GRAPHICS_SYSROOT/usr/local/share/wayland-protocols/staging/xdg-activation/xdg-activation-v1.xml" \
    "$WAYLAND_HOST_PREFIX/bin/wayland-scanner"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS LayerShellQt prerequisite: $required_file" >&2
        echo "Build Qt Wayland/QML, ECM, Wayland, xkbcommon, and Wayland protocols first." >&2
        exit 2
    fi
done

mkdir -p "$(dirname "$SOURCE_DIR")" "$(dirname "$BUILD_DIR")"
if [[ ! -d "$SOURCE_DIR/.git" ]]; then
    git clone --no-checkout "$UPSTREAM_URL" "$SOURCE_DIR"
fi
if [[ "$(git -C "$SOURCE_DIR" rev-parse HEAD 2>/dev/null || true)" != "$UPSTREAM_COMMIT" ]]; then
    if ! git -C "$SOURCE_DIR" cat-file -e "$UPSTREAM_COMMIT^{commit}" 2>/dev/null; then
        git -C "$SOURCE_DIR" fetch --depth 1 origin refs/tags/v6.0.0
    fi
    git -C "$SOURCE_DIR" checkout --detach "$UPSTREAM_COMMIT"
fi
if [[ "$(git -C "$SOURCE_DIR" rev-parse HEAD)" != "$UPSTREAM_COMMIT" ]]; then
    echo "The LayerShellQt source checkout does not match the pinned commit" >&2
    exit 1
fi
python3 "$ROOT_DIR/tools/patch-upstream-layershellqt.py" "$SOURCE_DIR"

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export PATH="$WAYLAND_HOST_PREFIX/bin:$QT_HOST/bin:$QT_QML_HOST/bin:$PATH"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$TOOLCHAIN_ROOT" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$QT_QML_TARGET;$QT_HOST;$QT_QML_HOST;$KDE_PREFIX;$GRAPHICS_SYSROOT/usr/local" \
    -DECM_DIR="$KDE_PREFIX/share/ECM/cmake" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6CorePrivate_DIR="$QT_TARGET/lib/cmake/Qt6CorePrivate" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6GuiPrivate_DIR="$QT_TARGET/lib/cmake/Qt6GuiPrivate" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6Network_DIR="$QT_TARGET/lib/cmake/Qt6Network" \
    -DQt6WaylandClient_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClient" \
    -DQt6WaylandGlobalPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate" \
    -DQt6WaylandClientPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate" \
    -DQt6WaylandScannerTools_DIR="$QT_HOST/lib/cmake/Qt6WaylandScannerTools" \
    -DQt6Qml_DIR="$QT_QML_TARGET/lib/cmake/Qt6Qml" \
    -DQt6QmlPrivate_DIR="$QT_QML_TARGET/lib/cmake/Qt6QmlPrivate" \
    -DQt6QmlIntegration_DIR="$QT_QML_TARGET/lib/cmake/Qt6QmlIntegration" \
    -DQt6QmlTools_DIR="$QT_QML_HOST/lib/cmake/Qt6QmlTools" \
    -DWayland_DATADIR="$ROOT_DIR/build/upstream-source/wayland-1.26.0/extracted/protocol" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DQT_HOST_PATH_CMAKE_DIR="$QT_HOST/lib/cmake" \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_RPATH=/usr/local/lib \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/libLayerShellQtInterface.so.6.0.0"
PLUGIN="$GRAPHICS_SYSROOT/usr/local/lib/plugins/wayland-shell-integration/liblayer-shell.so"
if [[ ! -f "$LIBRARY" || ! -f "$PLUGIN" ]]; then
    echo "LayerShellQt install did not produce its interface library and Wayland plugin" >&2
    exit 1
fi
python3 "$ROOT_DIR/tools/sanitize-elf-rpath.py" "$LIBRARY" /usr/local/lib
python3 "$ROOT_DIR/tools/sanitize-elf-rpath.py" "$PLUGIN" /usr/local/lib
python3 "$ROOT_DIR/tools/sanitize-elf-rpath.py" \
    "$GRAPHICS_SYSROOT/usr/local/lib/qml/org/kde/layershell/libLayerShellQtQml.so" \
    /usr/local/lib
ELF_INFO="$(llvm-readelf -h -d "$LIBRARY")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO"; then
    echo "LayerShellQt output has the wrong architecture" >&2
    exit 1
fi
if [[ "$GRAPHICS_SYSROOT" != "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64" ]]; then
    mkdir -p "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/local"
    cp -a "$GRAPHICS_SYSROOT/usr/local/." \
        "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/local/"
fi

echo "[DONE] Upstream LayerShellQt 6.0.0 for akaOS x86_64: $LIBRARY"
