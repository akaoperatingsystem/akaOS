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
SOURCE_DIR="${AKAOS_QTXDG_TOOLS_SOURCE_DIR:-$CACHE_ROOT/lxqt-src/qtxdg-tools-4.4.0-akaos-v3}"
BUILD_DIR="${AKAOS_QTXDG_TOOLS_BUILD_DIR:-$CACHE_ROOT/lxqt-build/qtxdg-tools-4.4.0-akaos-v3/x86_64-akaos}"
QT_ROOT="${AKAOS_QT_ROOT:-$CACHE_ROOT/qt6.12.0}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
LXQT_TOOLS_PREFIX="${AKAOS_LXQT_TOOLS_PREFIX:-$CACHE_ROOT/lxqt-stage/2.4.0/usr}"
GRAPHICS_SYSROOT="$ROOT_DIR/build/upstream-sysroot/graphics-x86_64"
UPSTREAM_COMMIT="900e14ad6d02046359ed21985337e6723031372d"
UPSTREAM_URL="https://github.com/lxqt/qtxdg-tools.git"
BUILD_JOBS="${AKAOS_LXQT_BUILD_JOBS:-2}"

for command_name in clang clang++ cmake git llvm-readelf ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing qtxdg-tools build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build" >&2
        exit 2
    fi
done

if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_LXQT_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for required_file in \
    "$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    "$ROOT_DIR/build/libc.so.6" "$ROOT_DIR/build/libm.so.6" \
    "$ROOT_DIR/build/libc++.so.1" "$ROOT_DIR/build/libc++abi.so.1" \
    "$ROOT_DIR/build/libunwind.so.1" \
    "$ROOT_DIR/build/user-dynamic-crt0.o" \
    "$ROOT_DIR/tools/qtxdg-tools-main-wrapper.cpp" \
    "$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$QT_TARGET/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Xml/Qt6XmlConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6ZlibPrivate/Qt6ZlibPrivateConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6CoreTools/Qt6CoreToolsConfig.cmake" \
    "$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools/lxqt2-build-tools-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdg/qt6xdg-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdgiconloader/qt6xdgiconloader-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libQt6Xdg.so.4.4.0"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS qtxdg-tools prerequisite: $required_file" >&2
        echo "Build Qt 6.12, lxqt-build-tools, and libqtxdg first." >&2
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
    echo "The qtxdg-tools source checkout does not match the pinned upstream commit" >&2
    exit 1
fi
if ! grep -Fq 'AKAOS_DYNAMIC_CRT_OBJECT' "$SOURCE_DIR/src/mat/CMakeLists.txt"; then
    patch -d "$SOURCE_DIR" -p1 \
        < "$ROOT_DIR/tools/qtxdg-tools-akaos.patch"
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$ROOT_DIR" \
    -DAKAOS_DYNAMIC_CRT_OBJECT="$ROOT_DIR/build/user-dynamic-crt0.o" \
    -DAKAOS_QTXDG_MAIN_WRAPPER="$ROOT_DIR/tools/qtxdg-tools-main-wrapper.cpp" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$QT_HOST;$LXQT_TOOLS_PREFIX;$GRAPHICS_SYSROOT/usr/local" \
    -Dlxqt2-build-tools_DIR="$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6Xml_DIR="$QT_TARGET/lib/cmake/Qt6Xml" \
    -DQt6ZlibPrivate_DIR="$QT_TARGET/lib/cmake/Qt6ZlibPrivate" \
    -DQt6Svg_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg" \
    -DQt6Xdg_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdg" \
    -DQt6XdgIconLoader_DIR="$GRAPHICS_SYSROOT/usr/local/share/cmake/qt6xdgiconloader" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DAKAOS_QTXDG_TRACE_MAIN="${AKAOS_QTXDG_TRACE_MAIN:-0}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

BUILT_PROGRAM="$BUILD_DIR/src/mat/qtxdg-mat"
if [[ ! -f "$BUILT_PROGRAM" ]]; then
    echo "qtxdg-tools build did not produce $BUILT_PROGRAM" >&2
    exit 1
fi
ELF_INFO="$(llvm-readelf -h -d "$BUILT_PROGRAM")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Shared library: [libQt6Xdg.so.4]' <<<"$ELF_INFO"; then
    echo "qtxdg-mat output has the wrong architecture or missing libqtxdg ABI" >&2
    exit 1
fi
if llvm-readelf -h "$BUILT_PROGRAM" | grep -Eq 'Entry point address:[[:space:]]*0x0+$'; then
    echo "qtxdg-mat output has no akaOS startup entry point" >&2
    exit 1
fi

DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"
PROGRAM="$GRAPHICS_SYSROOT/usr/local/bin/qtxdg-mat"
if [[ ! -f "$PROGRAM" ]]; then
    echo "qtxdg-tools install did not produce $PROGRAM" >&2
    exit 1
fi

echo "[DONE] Upstream qtxdg-tools 4.4.0 for akaOS x86_64: $PROGRAM"
