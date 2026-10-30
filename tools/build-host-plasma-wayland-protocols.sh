#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_ROOT="${AKAOS_KDE_CACHE_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos/kde}"
SOURCE_DIR="${AKAOS_PLASMA_WAYLAND_PROTOCOLS_SOURCE_DIR:-$CACHE_ROOT/source/plasma-wayland-protocols-1.23.0}"
BUILD_DIR="${AKAOS_PLASMA_WAYLAND_PROTOCOLS_BUILD_DIR:-$CACHE_ROOT/build/plasma-wayland-protocols-1.23.0}"
PREFIX="${AKAOS_KDE_HOST_PREFIX:-$CACHE_ROOT/host-prefix}"
UPSTREAM_COMMIT="c5ac4db818f4a575a6ccfe0065b73ecfaba6e93e"
UPSTREAM_URL="https://github.com/KDE/plasma-wayland-protocols.git"

for command_name in cmake git ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing Plasma Wayland Protocols build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install cmake git ninja-build" >&2
        exit 2
    fi
done

if [[ ! -f "$PREFIX/share/ECM/cmake/ECMConfig.cmake" ]]; then
    echo "Missing KDE Extra CMake Modules at $PREFIX" >&2
    echo "Run tools/build-host-kde-ecm.sh first." >&2
    exit 2
fi

mkdir -p "$(dirname "$SOURCE_DIR")" "$(dirname "$BUILD_DIR")" "$PREFIX"
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
    echo "The Plasma Wayland Protocols checkout does not match the pinned commit" >&2
    exit 1
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DECM_DIR="$PREFIX/share/ECM/cmake" \
    -DQUERY_EXECUTABLE="$ROOT_DIR/tools/qtpaths-akaos.sh" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR" --parallel 2
cmake --install "$BUILD_DIR"

if [[ ! -f "$PREFIX/share/cmake/PlasmaWaylandProtocols/PlasmaWaylandProtocolsConfig.cmake" ]]; then
    echo "Plasma Wayland Protocols install did not produce its CMake package" >&2
    exit 1
fi

echo "[DONE] Plasma Wayland Protocols 1.23.0 installed for host builds: $PREFIX"
