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
SOURCE_DIR="${AKAOS_LIBLXQT_SOURCE_DIR:-$CACHE_ROOT/lxqt-src/liblxqt-2.4.0-akaos-wayland}"
BUILD_DIR="${AKAOS_LIBLXQT_BUILD_DIR:-$CACHE_ROOT/lxqt-build/liblxqt-2.4.0-akaos-wayland/x86_64-akaos}"
QT_ROOT="${AKAOS_QT_ROOT:-$CACHE_ROOT/qt6.12.0}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
QT_HOST_TOOLS="${AKAOS_QT_HOST_TOOLS_BUILD:-$QT_ROOT/build-host-qttools}"
LXQT_TOOLS_PREFIX="${AKAOS_LXQT_TOOLS_PREFIX:-$CACHE_ROOT/lxqt-stage/2.4.0/usr}"
KDE_PREFIX="${AKAOS_KDE_HOST_PREFIX:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos/kde/host-prefix}"
PLASMA_PROTOCOLS_DIR="$KDE_PREFIX/share/cmake/PlasmaWaylandProtocols"
GRAPHICS_SYSROOT="${AKAOS_GRAPHICS_SYSROOT:-$ROOT_DIR/build/upstream-sysroot/graphics-x86_64}"
TOOLCHAIN_ROOT="${AKAOS_TOOLCHAIN_ROOT:-$ROOT_DIR}"
WAYLAND_HOST_PREFIX="$ROOT_DIR/build/host-tools/wayland-1.26.0"
UPSTREAM_COMMIT="d72a647c04078475170b9c1c1ee29c8044a9f433"
UPSTREAM_URL="https://github.com/lxqt/liblxqt.git"
BUILD_JOBS="${AKAOS_LXQT_BUILD_JOBS:-2}"

for command_name in clang clang++ cmake git llvm-readelf ninja pkg-config python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing LXQt liblxqt build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build pkg-config python3" >&2
        exit 2
    fi
done

if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_LXQT_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for required_file in \
    "$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    "$ROOT_DIR/tools/qtpaths-akaos.sh" \
    "$ROOT_DIR/build/libc.so.6" "$ROOT_DIR/build/libm.so.6" \
    "$ROOT_DIR/build/libc++.so.1" "$ROOT_DIR/build/libc++abi.so.1" \
    "$ROOT_DIR/build/libunwind.so.1" \
    "$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$QT_TARGET/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Xml/Qt6XmlConfig.cmake" \
    "$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake" \
    "$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools/lxqt2-build-tools-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdg/qt6xdg-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdgiconloader/qt6xdgiconloader-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/sysstat-qt6/sysstat-qt6-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/KF6WindowSystem/KF6WindowSystemConfig.cmake" \
    "$PLASMA_PROTOCOLS_DIR/PlasmaWaylandProtocolsConfig.cmake"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS liblxqt prerequisite: $required_file" >&2
        echo "Build Qt, Qt LinguistTools, lxqt-build-tools, libqtxdg, libsysstat, " \
             "KWindowSystem, and Plasma Wayland Protocols first." >&2
        exit 2
    fi
done

mkdir -p "$(dirname "$SOURCE_DIR")" "$(dirname "$BUILD_DIR")"
if [[ ! -d "$SOURCE_DIR/.git" ]]; then
    git clone --no-checkout "$UPSTREAM_URL" "$SOURCE_DIR"
fi
if [[ "$(git -C "$SOURCE_DIR" rev-parse HEAD 2>/dev/null || true)" != "$UPSTREAM_COMMIT" ]]; then
    if ! git -C "$SOURCE_DIR" cat-file -e "$UPSTREAM_COMMIT^{commit}" 2>/dev/null; then
        git -C "$SOURCE_DIR" fetch --depth 1 origin "$UPSTREAM_COMMIT"
    fi
    git -C "$SOURCE_DIR" checkout --detach "$UPSTREAM_COMMIT"
fi
if [[ "$(git -C "$SOURCE_DIR" rev-parse HEAD)" != "$UPSTREAM_COMMIT" ]]; then
    echo "The liblxqt source checkout does not match the pinned upstream commit" >&2
    exit 1
fi
python3 "$ROOT_DIR/tools/patch-upstream-liblxqt-wayland-only.py" "$SOURCE_DIR"

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export PATH="$WAYLAND_HOST_PREFIX/bin:$PATH"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$TOOLCHAIN_ROOT" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$QT_HOST;$QT_HOST_TOOLS;$LXQT_TOOLS_PREFIX;$KDE_PREFIX;$GRAPHICS_SYSROOT/usr/local" \
    -DQUERY_EXECUTABLE="$ROOT_DIR/tools/qtpaths-akaos.sh" \
    -DECM_DIR="$KDE_PREFIX/share/ECM/cmake" \
    -Dlxqt2-build-tools_DIR="$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6CorePrivate_DIR="$QT_TARGET/lib/cmake/Qt6CorePrivate" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6GuiPrivate_DIR="$QT_TARGET/lib/cmake/Qt6GuiPrivate" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6Xml_DIR="$QT_TARGET/lib/cmake/Qt6Xml" \
    -DQt6LinguistTools_DIR="$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools" \
    -DQt6Xdg_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdg" \
    -DQt6XdgIconLoader_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdgiconloader" \
    -Dsysstat-qt6_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/sysstat-qt6" \
    -DKF6WindowSystem_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/KF6WindowSystem" \
    -DPlasmaWaylandProtocols_DIR="$PLASMA_PROTOCOLS_DIR" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DAKAOS_WAYLAND_ONLY=ON \
    -DBUILD_BACKLIGHT_LINUX_BACKEND=OFF \
    -DUPDATE_TRANSLATIONS=OFF \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

if [[ "$GRAPHICS_SYSROOT" != "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64" ]]; then
    mkdir -p "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/local" \
             "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/share/lxqt"
    cp -a "$GRAPHICS_SYSROOT/usr/local/." \
        "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/local/"
    cp -a "$GRAPHICS_SYSROOT/usr/share/lxqt/." \
        "$ROOT_DIR/build/upstream-sysroot/graphics-x86_64/usr/share/lxqt/"
fi

LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/liblxqt.so.2.4.0"
if [[ ! -f "$LIBRARY" ]]; then
    echo "liblxqt install did not produce $LIBRARY" >&2
    exit 1
fi
ELF_INFO="$(llvm-readelf -h -d "$LIBRARY")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Shared library: [libKF6WindowSystem.so.6]' <<<"$ELF_INFO"; then
    echo "liblxqt output has the wrong architecture or missing KWindowSystem ABI" >&2
    exit 1
fi

echo "[DONE] Upstream liblxqt 2.4.0 (Wayland-only build) for akaOS x86_64: $LIBRARY"
