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
SOURCE_DIR="${AKAOS_LIBQTXDG_SOURCE_DIR:-$CACHE_ROOT/lxqt-src/libqtxdg-4.4.0}"
BUILD_DIR="${AKAOS_LIBQTXDG_BUILD_DIR:-$CACHE_ROOT/lxqt-build/libqtxdg-4.4.0/x86_64-akaos}"
QT_ROOT="${AKAOS_QT_ROOT:-$CACHE_ROOT/qt6.12.0}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
LXQT_TOOLS_PREFIX="${AKAOS_LXQT_TOOLS_PREFIX:-$CACHE_ROOT/lxqt-stage/2.4.0/usr}"
GRAPHICS_SYSROOT="$ROOT_DIR/build/upstream-sysroot/graphics-x86_64"
UPSTREAM_COMMIT="d033d9e8de993e56dc743b49cd61972e8e095af3"
UPSTREAM_URL="https://github.com/lxqt/libqtxdg.git"
BUILD_JOBS="${AKAOS_LXQT_BUILD_JOBS:-2}"

for command_name in clang clang++ cmake git llvm-readelf ninja pkg-config; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing LXQt XDG build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm ninja-build pkg-config" >&2
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
    "$ROOT_DIR/build/cxx-runtime/llvm-21.1.8/x86_64/libakaos-compiler-builtins.a" \
    "$QT_TARGET/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6CorePrivate/Qt6CorePrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6GuiPrivate/Qt6GuiPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6WidgetsPrivate/Qt6WidgetsPrivateConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Xml/Qt6XmlConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Network/Qt6NetworkConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6ZlibPrivate/Qt6ZlibPrivateConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6CoreTools/Qt6CoreToolsConfig.cmake" \
    "$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools/lxqt2-build-tools-config.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libQt6Svg.so.6.12.0" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/glib-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gobject-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gio-2.0.pc" \
    "$GRAPHICS_SYSROOT/usr/lib/pkgconfig/gio-unix-2.0.pc"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS libqtxdg prerequisite: $required_file" >&2
        echo "Build QtSvg, the x86_64 Qt runtime, and target GLib first." >&2
        exit 2
    fi
done

if [[ ! -f "$QT_TARGET/lib/libQt6Xml.so.6.12.0" ]]; then
    cmake --build "$QT_TARGET" --target Xml --parallel "$BUILD_JOBS"
fi
install -D -m 0755 "$QT_TARGET/lib/libQt6Xml.so.6.12.0" \
    "$GRAPHICS_SYSROOT/usr/local/lib/libQt6Xml.so.6.12.0"
ln -sfn libQt6Xml.so.6.12.0 \
    "$GRAPHICS_SYSROOT/usr/local/lib/libQt6Xml.so.6"
ln -sfn libQt6Xml.so.6 \
    "$GRAPHICS_SYSROOT/usr/local/lib/libQt6Xml.so"

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
    echo "The libqtxdg source checkout does not match the pinned upstream commit" >&2
    exit 1
fi

export PKG_CONFIG_LIBDIR="$GRAPHICS_SYSROOT/usr/local/lib/pkgconfig:$GRAPHICS_SYSROOT/usr/local/share/pkgconfig:$GRAPHICS_SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$GRAPHICS_SYSROOT"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$ROOT_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$QT_HOST;$LXQT_TOOLS_PREFIX;$GRAPHICS_SYSROOT/usr/local" \
    -Dlxqt2-build-tools_DIR="$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6CorePrivate_DIR="$QT_TARGET/lib/cmake/Qt6CorePrivate" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6GuiPrivate_DIR="$QT_TARGET/lib/cmake/Qt6GuiPrivate" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6WidgetsPrivate_DIR="$QT_TARGET/lib/cmake/Qt6WidgetsPrivate" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DQt6Xml_DIR="$QT_TARGET/lib/cmake/Qt6Xml" \
    -DQt6Network_DIR="$QT_TARGET/lib/cmake/Qt6Network" \
    -DQt6ZlibPrivate_DIR="$QT_TARGET/lib/cmake/Qt6ZlibPrivate" \
    -DQt6Svg_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6Svg" \
    -DQt6SvgPrivate_DIR="$GRAPHICS_SYSROOT/usr/local/lib/cmake/Qt6SvgPrivate" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DBUILD_TESTS=OFF \
    -DBUILD_DEV_UTILS=OFF \
    -DWITH_DOC=OFF \
    -DQTXDGX_ICONENGINEPLUGIN_INSTALL_PATH=/usr/local/plugins/iconengines \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

XDG_LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/libQt6Xdg.so.4.4.0"
ICON_LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/libQt6XdgIconLoader.so.4.4.0"
for library in "$XDG_LIBRARY" "$ICON_LIBRARY"; do
    if [[ ! -f "$library" ]]; then
        echo "libqtxdg install did not produce $library" >&2
        exit 1
    fi
    ELF_INFO="$(llvm-readelf -h -d "$library")"
    if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
       ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO"; then
        echo "libqtxdg output has the wrong architecture: $library" >&2
        exit 1
    fi
done
if ! llvm-readelf -d "$ICON_LIBRARY" | grep -Fq 'Shared library: [libQt6Svg.so.6]'; then
    echo "libqtxdg icon loader is not linked against the upstream QtSvg module" >&2
    exit 1
fi

echo "[DONE] Upstream libqtxdg 4.4.0 for akaOS x86_64: $XDG_LIBRARY"
