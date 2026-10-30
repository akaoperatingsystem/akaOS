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
CACHE_ROOT="${AKAOS_KDE_CACHE_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos/kde}"
SOURCE_DIR="${AKAOS_KWINDOWSYSTEM_SOURCE_DIR:-$CACHE_ROOT/source/kwindowsystem-6.31.0}"
BUILD_DIR="${AKAOS_KWINDOWSYSTEM_BUILD_DIR:-$CACHE_ROOT/build/kwindowsystem-6.31.0/x86_64-akaos-wayland}"
PREFIX="${AKAOS_KDE_HOST_PREFIX:-$CACHE_ROOT/host-prefix}"
QT_ROOT="${AKAOS_QT_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos/qt6.12.0}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
QT_HOST_TOOLS="${AKAOS_QT_HOST_TOOLS_BUILD:-$QT_ROOT/build-host-qttools}"
PLASMA_PROTOCOLS_DIR="$PREFIX/share/cmake/PlasmaWaylandProtocols"
GRAPHICS_SYSROOT="$ROOT_DIR/build/upstream-sysroot/graphics-x86_64"
WAYLAND_HOST_PREFIX="$ROOT_DIR/build/host-tools/wayland-1.26.0"
UPSTREAM_COMMIT="52279762ee1775eca7d03af60792f49f3dc313d4"
UPSTREAM_URL="https://github.com/KDE/kwindowsystem.git"
BUILD_JOBS="${AKAOS_KDE_BUILD_JOBS:-2}"

for command_name in clang clang++ cmake git llvm-readelf ninja pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing KDE KWindowSystem build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build pkg-config" >&2
        exit 2
    fi
done

if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_KDE_BUILD_JOBS must be a positive integer" >&2
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
    "$QT_TARGET/lib/cmake/Qt6ZlibPrivate/Qt6ZlibPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBusPrivate/Qt6DBusPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClient/Qt6WaylandClientConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate/Qt6WaylandClientPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate/Qt6WaylandGlobalPrivateConfig.cmake" \
    "$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake" \
    "$PREFIX/share/ECM/cmake/ECMConfig.cmake" \
    "$PLASMA_PROTOCOLS_DIR/PlasmaWaylandProtocolsConfig.cmake" \
    "$ROOT_DIR/build/upstream-source/wayland-1.26.0/extracted/protocol/wayland.xml" \
    "$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig/wayland-client.pc" \
    "$GRAPHICS_SYSROOT/usr/local/share/pkgconfig/wayland-protocols.pc"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing KWindowSystem prerequisite: $required_file" >&2
        echo "Build Qt Wayland, KDE ECM, Plasma Wayland Protocols, and Wayland first." >&2
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
    echo "The KWindowSystem source checkout does not match the pinned commit" >&2
    exit 1
fi

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"
export PATH="$WAYLAND_HOST_PREFIX/bin:$PATH"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$ROOT_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$QT_HOST;$QT_HOST_TOOLS;$PREFIX;$GRAPHICS_SYSROOT/usr/local" \
    -DQUERY_EXECUTABLE="$ROOT_DIR/tools/qtpaths-akaos.sh" \
    -DECM_DIR="$PREFIX/share/ECM/cmake" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6CorePrivate_DIR="$QT_TARGET/lib/cmake/Qt6CorePrivate" \
    -DQt6ZlibPrivate_DIR="$QT_TARGET/lib/cmake/Qt6ZlibPrivate" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6GuiPrivate_DIR="$QT_TARGET/lib/cmake/Qt6GuiPrivate" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6DBusPrivate_DIR="$QT_TARGET/lib/cmake/Qt6DBusPrivate" \
    -DQt6WaylandClient_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClient" \
    -DQt6WaylandClientPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandClientPrivate" \
    -DQt6WaylandGlobalPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WaylandGlobalPrivate" \
    -DQt6LinguistTools_DIR="$QT_HOST_TOOLS/lib/cmake/Qt6LinguistTools" \
    -DPlasmaWaylandProtocols_DIR="$PLASMA_PROTOCOLS_DIR" \
    -DWayland_DATADIR="$ROOT_DIR/build/upstream-source/wayland-1.26.0/extracted/protocol" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DKWINDOWSYSTEM_X11=OFF \
    -DKWINDOWSYSTEM_WAYLAND=ON \
    -DKWINDOWSYSTEM_QML=OFF \
    -DBUILD_TESTING=OFF \
    -DQT_GENERATE_SBOM=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/libKF6WindowSystem.so.6.31.0"
if [[ ! -f "$LIBRARY" ]]; then
    echo "KWindowSystem install did not produce $LIBRARY" >&2
    exit 1
fi
ELF_INFO="$(llvm-readelf -h -d "$LIBRARY")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Shared library: [libQt6Gui.so.6]' <<<"$ELF_INFO"; then
    echo "KWindowSystem output has the wrong architecture or missing QtGui ABI" >&2
    exit 1
fi

echo "[DONE] KDE KWindowSystem 6.31.0 (Wayland-only) for akaOS x86_64: $LIBRARY"
