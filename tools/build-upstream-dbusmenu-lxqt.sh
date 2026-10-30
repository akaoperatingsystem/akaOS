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
SOURCE_DIR="${AKAOS_LXQT_SOURCE_DIR:-$CACHE_ROOT/lxqt-src/libdbusmenu-lxqt-0.4.0}"
BUILD_DIR="${AKAOS_LXQT_BUILD_DIR:-$CACHE_ROOT/lxqt-build/libdbusmenu-lxqt-0.4.0/x86_64-akaos}"
QT_ROOT="${AKAOS_QT_ROOT:-$CACHE_ROOT/qt6.12.0}"
QT_TARGET="${AKAOS_QT_TARGET_BUILD:-$QT_ROOT/build-target-x86_64-akaos}"
LXQT_TOOLS_PREFIX="${AKAOS_LXQT_TOOLS_PREFIX:-$CACHE_ROOT/lxqt-stage/2.4.0/usr}"
GRAPHICS_SYSROOT="$ROOT_DIR/build/upstream-sysroot/graphics-x86_64"
UPSTREAM_COMMIT="f5e2ad08eeddc15219d5a7223eb82467b4ad6fc8"
UPSTREAM_URL="https://github.com/lxqt/libdbusmenu-lxqt.git"
BUILD_JOBS="${AKAOS_LXQT_BUILD_JOBS:-2}"

for command_name in clang clang++ cmake git llvm-readelf patch; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing LXQt build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install clang lld cmake git llvm patch" >&2
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
    "$QT_TARGET/lib/cmake/Qt6Gui/Qt6GuiConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" \
    "$QT_TARGET/lib/cmake/Qt6DBus/Qt6DBusConfig.cmake"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing akaOS LXQt prerequisite: $required_file" >&2
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
    echo "The LXQt source checkout does not match the pinned upstream commit" >&2
    exit 1
fi

if ! grep -Fq 'option(AKAOS_BUILD_UPSTREAM_TESTS' "$SOURCE_DIR/CMakeLists.txt"; then
    patch -d "$SOURCE_DIR" -p1 \
        < "$ROOT_DIR/tools/libdbusmenu-lxqt-no-tests.patch"
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/tools/qtbase-akaos-x86_64.cmake" \
    -DAKAOS_PROJECT_ROOT="$ROOT_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_TARGET;$LXQT_TOOLS_PREFIX" \
    -DQt6_DIR="$QT_TARGET/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_TARGET/lib/cmake/Qt6Core" \
    -DQt6Gui_DIR="$QT_TARGET/lib/cmake/Qt6Gui" \
    -DQt6Widgets_DIR="$QT_TARGET/lib/cmake/Qt6Widgets" \
    -DQt6DBus_DIR="$QT_TARGET/lib/cmake/Qt6DBus" \
    -DAKAOS_BUILD_UPSTREAM_TESTS=OFF \
    -DWITH_DOC=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

LIBRARY="$GRAPHICS_SYSROOT/usr/local/lib/libdbusmenu-lxqt.so.0.4.0"
if [[ ! -f "$LIBRARY" ]]; then
    echo "LXQt libdbusmenu install did not produce $LIBRARY" >&2
    exit 1
fi
ELF_INFO="$(llvm-readelf -h -d "$LIBRARY")"
if ! grep -Fq 'Class:                             ELF64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Machine:                           Advanced Micro Devices X86-64' <<<"$ELF_INFO" || \
   ! grep -Fq 'Shared library: [libQt6Widgets.so.6]' <<<"$ELF_INFO"; then
    echo "LXQt libdbusmenu output has the wrong architecture or missing Qt ABI" >&2
    exit 1
fi

echo "[DONE] Upstream libdbusmenu-lxqt 0.4.0 for akaOS x86_64: $LIBRARY"
