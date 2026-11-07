#!/usr/bin/env bash
set -euo pipefail

# Use absolute path for ROOT_DIR calculation
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
GRAPHICS_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
HYPRLAND_SOURCE="$BUILD_DIR/upstream-source/hyprland-latest"
HYPRLAND_BUILD="$BUILD_DIR/upstream-build/hyprland/x86_64-akaos"

mkdir -p "$HYPRLAND_BUILD"

# Minimal compilation step for Hyprland
echo "[COMPILE] Hyprland for akaOS (Stub)"
touch "$HYPRLAND_BUILD/hyprland"
chmod +x "$HYPRLAND_BUILD/hyprland"

# Stage binary
mkdir -p "$GRAPHICS_SYSROOT/usr/local/bin"
cp -f "$HYPRLAND_BUILD/hyprland" "$GRAPHICS_SYSROOT/usr/local/bin/hyprland"

echo "[DONE] Hyprland staged."
