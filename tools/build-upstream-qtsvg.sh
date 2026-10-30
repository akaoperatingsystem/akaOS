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
QT_ROOT="${AKAOS_QT_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos/qt6.12.0}"
SOURCE_DIR="${AKAOS_QTSVG_SOURCE_DIR:-$QT_ROOT/source-qtsvg}"
BUILD_DIR="${AKAOS_QTSVG_BUILD_DIR:-$QT_ROOT/build-target-qtsvg-ninja}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
GRAPHICS_SYSROOT="$ROOT_DIR/build/upstream-sysroot/graphics-x86_64"
UPSTREAM_COMMIT="883a58532078d003cefef0a549bfe9f885ce41f5"
UPSTREAM_URL="https://github.com/qt/qtsvg.git"
BUILD_JOBS="${AKAOS_QTSVG_BUILD_JOBS:-2}"

for command_name in clang clang++ cmake git llvm-readelf ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing QtSvg build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build" >&2
        exit 2
    fi
done

if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_QTSVG_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for required_file in \
    "$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    "$ROOT_DIR/build/libc.so.6" "$ROOT_DIR/build/libm.so.6" \
    "$ROOT_DIR/build/libc++.so.1" "$ROOT_DIR/build/libc++abi.so.1" \
    "$ROOT_DIR/build/libunwind.so.1" \
    "$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$QT_TARGET/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_TARGET/lib/cmake/Qt6BuildInternals/Qt6BuildInternalsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WidgetsPrivate/Qt6WidgetsPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Xml/Qt6XmlConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Network/Qt6NetworkConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6ZlibPrivate/Qt6ZlibPrivateConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6CoreTools/Qt6CoreToolsConfig.cmake"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS QtSvg prerequisite: $required_file" >&2
        echo "Build the x86_64 akaOS Qt 6.12 runtime first." >&2
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
    echo "The QtSvg source checkout does not match the pinned upstream commit" >&2
    exit 1
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$ROOT_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$QT_HOST" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6BuildInternals_DIR="$QT_TARGET/lib/cmake/Qt6BuildInternals" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6GuiPrivate_DIR="$QT_TARGET/lib/cmake/Qt6GuiPrivate" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6WidgetsPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WidgetsPrivate" \
    -DQt6Xml_DIR="$QT_TARGET/lib/cmake/Qt6Xml" \
    -DQt6Network_DIR="$QT_TARGET/lib/cmake/Qt6Network" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6CorePrivate_DIR="$QT_TARGET/lib/cmake/Qt6CorePrivate" \
    -DQt6ZlibPrivate_DIR="$QT_TARGET/lib/cmake/Qt6ZlibPrivate" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DQT_BUILD_TESTS=OFF \
    -DQT_GENERATE_SBOM=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/libQt6Svg.so.6.12.0"
if [[ ! -f "$LIBRARY" ]]; then
    echo "QtSvg install did not produce $LIBRARY" >&2
    exit 1
fi
ELF_INFO="$(llvm-readelf -h -d "$LIBRARY")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Shared library: [libQt6Gui.so.6]' <<<"$ELF_INFO"; then
    echo "QtSvg output has the wrong architecture or missing QtGui ABI" >&2
    exit 1
fi

echo "[DONE] Upstream QtSvg 6.12.0 for akaOS x86_64: $LIBRARY"
