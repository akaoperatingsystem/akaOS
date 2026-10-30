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
SOURCE_DIR="${AKAOS_LXQT_MENU_DATA_SOURCE_DIR:-$CACHE_ROOT/lxqt-src/lxqt-menu-data-2.4.0}"
BUILD_DIR="${AKAOS_LXQT_MENU_DATA_BUILD_DIR:-$CACHE_ROOT/lxqt-build/lxqt-menu-data-2.4.0}"
LXQT_TOOLS_PREFIX="${AKAOS_LXQT_TOOLS_PREFIX:-$CACHE_ROOT/lxqt-stage/2.4.0/usr}"
QT_ROOT="${AKAOS_QT_ROOT:-$CACHE_ROOT/qt6.12.0}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$QT_ROOT/build-host}"
QTTOOLS_BUILD="${AKAOS_QTTOOLS_BUILD_DIR:-$QT_ROOT/build-host-qttools}"
GRAPHICS_SYSROOT="$ROOT_DIR/build/upstream-sysroot/graphics-x86_64"
UPSTREAM_COMMIT="5045c6baa73036d794cd750e8b64a33f4ddf6293"
UPSTREAM_URL="https://github.com/lxqt/lxqt-menu-data.git"

for command_name in cmake git ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing LXQt menu-data build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install cmake git ninja-build" >&2
        exit 2
    fi
done

if [[ ! -f "$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools/lxqt2-build-tools-config.cmake" ]]; then
    echo "Missing LXQt build tools at $LXQT_TOOLS_PREFIX" >&2
    echo "Build lxqt-build-tools 2.4.0 first." >&2
    exit 2
fi
if [[ ! -f "$QT_HOST/lib/cmake/Qt6/Qt6Config.cmake" ||
      ! -f "$QTTOOLS_BUILD/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake" ||
      ! -x "$QTTOOLS_BUILD/bin/lrelease" ]]; then
    echo "Missing host Qt LinguistTools; run tools/build-host-qt-linguist-tools.sh first" >&2
    exit 2
fi

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
    echo "The LXQt menu-data source checkout does not match the pinned commit" >&2
    exit 1
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_PREFIX_PATH="$QT_HOST;$QTTOOLS_BUILD;$LXQT_TOOLS_PREFIX" \
    -DQt6_DIR="$QT_HOST/lib/cmake/Qt6" \
    -DQt6Core_DIR="$QT_HOST/lib/cmake/Qt6Core" \
    -DQt6LinguistTools_DIR="$QTTOOLS_BUILD/lib/cmake/Qt6LinguistTools" \
    -DQT_HOST_PATH="$QT_HOST" \
    -Dlxqt2-build-tools_DIR="$LXQT_TOOLS_PREFIX/share/cmake/lxqt2-build-tools" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" --parallel "${AKAOS_LXQT_BUILD_JOBS:-2}"
DESTDIR="$GRAPHICS_SYSROOT" cmake --install "$BUILD_DIR"

MENU_FILE="$GRAPHICS_SYSROOT/etc/xdg/menus/lxqt-applications.menu"
if [[ ! -f "$MENU_FILE" ]]; then
    echo "LXQt menu-data install did not produce $MENU_FILE" >&2
    exit 1
fi

echo "[DONE] Upstream LXQt menu-data 2.4.0 staged: $GRAPHICS_SYSROOT/usr/share"
