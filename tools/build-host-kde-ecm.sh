#!/usr/bin/env bash
set -euo pipefail

CACHE_ROOT="${AKAOS_KDE_CACHE_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos/kde}"
SOURCE_DIR="${AKAOS_ECM_SOURCE_DIR:-$CACHE_ROOT/source/extra-cmake-modules-6.31.0}"
BUILD_DIR="${AKAOS_ECM_BUILD_DIR:-$CACHE_ROOT/build/extra-cmake-modules-6.31.0}"
PREFIX="${AKAOS_KDE_HOST_PREFIX:-$CACHE_ROOT/host-prefix}"
UPSTREAM_COMMIT="2958379fb49c71129f119ec17f88df0b5ea21550"
UPSTREAM_URL="https://github.com/KDE/extra-cmake-modules.git"

for command_name in cmake git ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing KDE build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install cmake git ninja-build" >&2
        exit 2
    fi
done

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
    echo "The Extra CMake Modules source checkout does not match the pinned commit" >&2
    exit 1
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR" --parallel 2
cmake --install "$BUILD_DIR"

if [[ ! -f "$PREFIX/share/ECM/cmake/ECMConfig.cmake" ]]; then
    echo "Extra CMake Modules install did not produce ECMConfig.cmake" >&2
    exit 1
fi

echo "[DONE] KDE Extra CMake Modules 6.31.0 installed for host builds: $PREFIX"
