#!/usr/bin/env bash
# clean.sh - akaOS Build and Artifact Cleanup Script
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
TEST_RESULTS_DIR="$ROOT_DIR/.test-results"
FREEBUFF_DIR="$ROOT_DIR/.freebuff"

usage() {
    cat <<'EOF'
Usage: ./clean.sh [options]

Clean akaOS build outputs, temporary files, and compiled artifacts.

Options:
  --all, -a            Full clean: delete all build artifacts, test logs, caches, and persistent disk
  --cache              Also clean downloaded upstream source archives (build/upstream-source)
  --keep-data          Preserve persistent shell data disk (build/akaos-data.img)
  -h, --help           Display this help and exit

Examples:
  ./clean.sh           # Clean standard build outputs and temporary files
  ./clean.sh --all     # Complete clean (resets build directory and test results)
  ./clean.sh --keep-data # Clean kernel and ISO images but keep akaos-data.img
EOF
}

clean_all=0
clean_cache=0
keep_data=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all|-a)
            clean_all=1
            shift
            ;;
        --cache)
            clean_cache=1
            shift
            ;;
        --keep-data)
            keep_data=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option '$1'; use --help for usage" >&2
            exit 2
            ;;
    esac
done

echo "[CLEAN] Cleaning akaOS workspace..."

# 1. Clean root directory autotools / out-of-tree remnants if present
rm -rf "$ROOT_DIR/applets" "$ROOT_DIR/data" "$ROOT_DIR/doc" "$ROOT_DIR/help"        "$ROOT_DIR/icons" "$ROOT_DIR/libmate-panel-applet" "$ROOT_DIR/mate-panel"        "$ROOT_DIR/po" "$ROOT_DIR/man"
rm -f "$ROOT_DIR/Makefile" "$ROOT_DIR/config.h" "$ROOT_DIR/config.log"       "$ROOT_DIR/config.status" "$ROOT_DIR/confdefs.h" "$ROOT_DIR/libtool"       "$ROOT_DIR/stamp-h1" "$ROOT_DIR"/*.log

# 2. Clean Python bytecode caches
find "$ROOT_DIR" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
find "$ROOT_DIR" -type f -name "*.pyc" -delete 2>/dev/null || true

# 3. Clean test results and scratch buffers
rm -rf "$TEST_RESULTS_DIR" "$FREEBUFF_DIR"

# 4. Clean build directory
if [[ -d "$BUILD_DIR" ]]; then
    if [[ "$clean_all" -eq 1 ]]; then
        if [[ "$keep_data" -eq 1 && -f "$BUILD_DIR/akaos-data.img" ]]; then
            echo "[INFO] Preserving persistent storage: $BUILD_DIR/akaos-data.img"
            mv "$BUILD_DIR/akaos-data.img" "$ROOT_DIR/.akaos-data.img.tmp"
            rm -rf "$BUILD_DIR"
            mkdir -p "$BUILD_DIR"
            mv "$ROOT_DIR/.akaos-data.img.tmp" "$BUILD_DIR/akaos-data.img"
        else
            rm -rf "$BUILD_DIR"
        fi
        echo "[CLEAN] Build directory removed."
    else
        # Standard clean: preserve upstream source caches if not --cache
        if [[ "$clean_cache" -eq 0 && -d "$BUILD_DIR/upstream-source" ]]; then
            echo "[INFO] Preserving cached upstream sources (use --cache to delete them)"
            # Delete compiled ELFs, objects, disks, and logs
            find "$BUILD_DIR" -maxdepth 1 -type f -delete 2>/dev/null || true
            rm -rf "$BUILD_DIR/esp" "$BUILD_DIR/arch-smoke" "$BUILD_DIR/bios-root"
        else
            if [[ "$keep_data" -eq 1 && -f "$BUILD_DIR/akaos-data.img" ]]; then
                mv "$BUILD_DIR/akaos-data.img" "$ROOT_DIR/.akaos-data.img.tmp"
                rm -rf "$BUILD_DIR"
                mkdir -p "$BUILD_DIR"
                mv "$ROOT_DIR/.akaos-data.img.tmp" "$BUILD_DIR/akaos-data.img"
            else
                rm -rf "$BUILD_DIR"
            fi
        fi
        echo "[CLEAN] Standard build artifacts and intermediate objects cleaned."
    fi
fi

echo "[DONE] Cleanup complete!"
