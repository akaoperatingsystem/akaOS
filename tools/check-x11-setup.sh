#!/usr/bin/env bash
# Validate the akaOS X11 connection setup reply with the system libxcb.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST_TOOLS="${AKAOS_HOST_TOOLS:-$HOME/.cache/akaos/host-tools}"
XCB_DEV="$HOST_TOOLS/xcb-dev"
mkdir -p "$HOST_TOOLS"
if [[ ! -f "$XCB_DEV/usr/include/xcb/xcb.h" ]]; then
    mkdir -p "$HOST_TOOLS/deb" "$XCB_DEV"
    (cd "$HOST_TOOLS/deb" && apt-get download libxcb1-dev >/dev/null 2>&1 &&
        dpkg-deb -x libxcb1-dev*.deb "$XCB_DEV")
fi
XCB_LIB_DIR="$(dirname "$(find "$XCB_DEV" -name 'libxcb.so' -print -quit 2>/dev/null)")"
if [[ -z "$XCB_LIB_DIR" ]]; then
    XCB_LIB_DIR=/usr/lib/x86_64-linux-gnu
fi
cc -O1 -Wall -Wextra -o "$HOST_TOOLS/x11-setup-check" \
   "$ROOT_DIR/tools/x11-setup-check.c" \
   "$ROOT_DIR/userspace/bin/xserver/setup.c" \
   -I"$XCB_DEV/usr/include" \
   -I"$ROOT_DIR/userspace/bin/xserver" \
   -I"$ROOT_DIR/userspace/bin/xserver/include" \
   -L"$XCB_LIB_DIR" -lxcb
exec "$HOST_TOOLS/x11-setup-check"
