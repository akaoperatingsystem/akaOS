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
  --dry-run, -n        Show what would be cleaned without removing anything
  -h, --help           Display this help and exit

Examples:
  ./clean.sh             # Clean standard build outputs and intermediate objects
  ./clean.sh --all       # Complete clean (resets build directory and test results)
  ./clean.sh --keep-data # Clean kernel and ISO images but preserve akaos-data.img
  ./clean.sh --dry-run   # Preview cleanup targets without deleting
EOF
}

clean_all=0
clean_cache=0
keep_data=0
dry_run=0

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
        --dry-run|-n)
            dry_run=1
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

do_remove() {
    local target="$1"
    if [[ -e "$target" ]]; then
        if [[ "$dry_run" -eq 1 ]]; then
            echo "  [DRY-RUN] Would remove: $target"
        else
            rm -rf "$target"
        fi
    fi
}

echo "[CLEAN] Cleaning akaOS workspace (all=$clean_all, cache=$clean_cache, keep_data=$keep_data, dry_run=$dry_run)..."

# 1. Clean root directory autotools / out-of-tree remnants if present
for root_clutter in     "$ROOT_DIR/applets" "$ROOT_DIR/data" "$ROOT_DIR/doc" "$ROOT_DIR/help"     "$ROOT_DIR/icons" "$ROOT_DIR/libmate-panel-applet" "$ROOT_DIR/mate-panel"     "$ROOT_DIR/po" "$ROOT_DIR/man"     "$ROOT_DIR/Makefile" "$ROOT_DIR/config.h" "$ROOT_DIR/config.log"     "$ROOT_DIR/config.status" "$ROOT_DIR/confdefs.h" "$ROOT_DIR/libtool"     "$ROOT_DIR/stamp-h1"; do
    do_remove "$root_clutter"
done

# Clean root temporary logs
if [[ "$dry_run" -eq 1 ]]; then
    for f in "$ROOT_DIR"/*.log; do
        [[ -f "$f" ]] && echo "  [DRY-RUN] Would remove: $f"
    done
else
    rm -f "$ROOT_DIR"/*.log 2>/dev/null || true
fi

# 2. Clean Python bytecode caches in tools/ and userspace/
for pycache in "$ROOT_DIR/tools/__pycache__" "$ROOT_DIR/tools/patches/__pycache__"; do
    do_remove "$pycache"
done

# 3. Clean test results and scratch buffers
do_remove "$TEST_RESULTS_DIR"
do_remove "$FREEBUFF_DIR"

# 4. Clean build directory
if [[ -d "$BUILD_DIR" ]]; then
    if [[ "$clean_all" -eq 1 ]]; then
        if [[ "$keep_data" -eq 1 && -f "$BUILD_DIR/akaos-data.img" ]]; then
            echo "[INFO] Preserving persistent storage: $BUILD_DIR/akaos-data.img"
            if [[ "$dry_run" -eq 0 ]]; then
                mv "$BUILD_DIR/akaos-data.img" "$ROOT_DIR/.akaos-data.img.tmp"
                rm -rf "$BUILD_DIR"
                mkdir -p "$BUILD_DIR"
                mv "$ROOT_DIR/.akaos-data.img.tmp" "$BUILD_DIR/akaos-data.img"
            fi
        else
            do_remove "$BUILD_DIR"
        fi
        echo "[CLEAN] Build directory reset."
    else
        # Standard clean: preserve upstream source archives unless --cache
        if [[ "$clean_cache" -eq 0 && -d "$BUILD_DIR/upstream-source" ]]; then
            echo "[INFO] Preserving cached upstream sources: $BUILD_DIR/upstream-source (use --cache to delete them)"
        fi

        # Remove compiled root-level artifacts in build/
        if [[ "$dry_run" -eq 1 ]]; then
            echo "  [DRY-RUN] Would remove top-level compiled artifacts in: $BUILD_DIR"
        else
            find "$BUILD_DIR" -maxdepth 1 -type f ! -name "akaos-data.img" -delete 2>/dev/null || true
            if [[ "$keep_data" -eq 0 ]]; then
                rm -f "$BUILD_DIR/akaos-data.img"
            fi
        fi

        # Remove intermediate target and test directories inside build/
        for sub in             esp arch-smoke bios-root iso32-root generated             user32 user_aarch64 user_arm32 user_riscv32 user_riscv64             compiler-rt compiler_rt_probe cross-desktop-libc-probes cxx-runtime             dbus-activation-helper dbus-helper dbus-match-fix dbus-protocol-fix             dbus-requestname dbus-serviceunknown gtk-wayland-test host-tools             probe-pthread qtquick-diagnostics-20261015 qtquick-fonts-20261016             qtquick-framecheck-20261015 test-* lxqt-* wayland-bridge-protocol             wayland-client-test _deps; do
            for matched in "$BUILD_DIR"/$sub; do
                do_remove "$matched"
            done
        done

        if [[ "$clean_cache" -eq 1 ]]; then
            do_remove "$BUILD_DIR/upstream-source"
            do_remove "$BUILD_DIR/upstream-build"
            do_remove "$BUILD_DIR/upstream-sysroot"
            do_remove "$BUILD_DIR/upstream-install"
        fi

        echo "[CLEAN] Standard build artifacts and intermediate objects cleaned."
    fi
fi

echo "[DONE] Cleanup complete!"
