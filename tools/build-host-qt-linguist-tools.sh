#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_ROOT="${AKAOS_QT_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/akaos/qt6.12.0}"
SOURCE_DIR="${AKAOS_QTTOOLS_SOURCE_DIR:-$CACHE_ROOT/source-qttools}"
BUILD_DIR="${AKAOS_QTTOOLS_BUILD_DIR:-$CACHE_ROOT/build-host-qttools}"
QT_HOST="${AKAOS_QT_HOST_BUILD:-$CACHE_ROOT/build-host}"
QT_HOST_PREFIX="${AKAOS_QT_HOST_PREFIX:-$CACHE_ROOT/host-prefix}"
UPSTREAM_COMMIT="613a6fbf0a7998010ab2fda260b64066d66a021d"
UPSTREAM_URL="https://github.com/qt/qttools.git"
BUILD_JOBS="${AKAOS_QTTOOLS_BUILD_JOBS:-2}"

for command_name in cmake git ninja; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing host Qt LinguistTools build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install cmake git ninja-build" >&2
        exit 2
    fi
done

if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AKAOS_QTTOOLS_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for required_file in \
    "$QT_HOST/lib/cmake/Qt6/Qt6Config.cmake" \
    "$QT_HOST/lib/cmake/Qt6BuildInternals/Qt6BuildInternalsConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
    "$QT_HOST/lib/cmake/Qt6CoreTools/Qt6CoreToolsConfig.cmake"; do
    if [[ ! -f "$required_file" ]]; then
        echo "Missing host Qt 6.12 prerequisite: $required_file" >&2
        echo "Build the Qt 6.12.0 host tools first." >&2
        exit 2
    fi
done

mkdir -p "$(dirname "$SOURCE_DIR")" "$(dirname "$BUILD_DIR")" "$QT_HOST_PREFIX"
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
    echo "The QtTools source checkout does not match the pinned Qt 6.12.0 commit" >&2
    exit 1
fi
git -C "$SOURCE_DIR" submodule update --init --recursive

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_PREFIX_PATH="$QT_HOST;$QT_HOST_PREFIX" \
    -DQt6_DIR="$QT_HOST/lib/cmake/Qt6" \
    -DQt6BuildInternals_DIR="$QT_HOST/lib/cmake/Qt6BuildInternals" \
    -DQt6Core_DIR="$QT_HOST/lib/cmake/Qt6Core" \
    -DQt6CoreTools_DIR="$QT_HOST/lib/cmake/Qt6CoreTools" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DQT_FORCE_BUILD_TOOLS=ON \
    -DQT_NO_FEATURE_AUTO_RESET=ON \
    -DQT_FEATURE_assistant=OFF \
    -DQT_FEATURE_designer=OFF \
    -DQT_FEATURE_kmap2qmap=OFF \
    -DQT_FEATURE_qdoc=OFF \
    -DQT_FEATURE_pixeltool=OFF \
    -DQT_FEATURE_distancefieldgenerator=OFF \
    -DQT_FEATURE_qdbus=OFF \
    -DQT_FEATURE_qtdiag=OFF \
    -DQT_FEATURE_qtattributionsscanner=OFF \
    -DQT_FEATURE_qev=OFF \
    -DQT_FEATURE_linguist=ON \
    -DQT_GENERATE_SBOM=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DQT_BUILD_EXAMPLES=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$QT_HOST_PREFIX"

cmake --build "$BUILD_DIR" \
    --target lrelease lupdate lconvert lcheck lrelease-pro lupdate-pro ltext2id \
    --parallel "$BUILD_JOBS"

for tool_name in lrelease lupdate lconvert lcheck lrelease-pro lupdate-pro ltext2id; do
    if [[ ! -x "$BUILD_DIR/bin/$tool_name" ]]; then
        echo "Qt LinguistTools build did not produce $BUILD_DIR/bin/$tool_name" >&2
        exit 1
    fi
done
if [[ ! -f "$BUILD_DIR/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake" ]]; then
    echo "Qt LinguistTools CMake package metadata was not generated" >&2
    exit 1
fi

echo "[DONE] Host Qt LinguistTools 6.12.0: $BUILD_DIR/bin/lrelease"
