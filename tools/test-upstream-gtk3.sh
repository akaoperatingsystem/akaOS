#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AKAOS_TEST_GTK3=1 AKAOS_TEST_GTK3_ONLY=1 AKAOS_TEST_SKIP_GTK_PREFLIGHT=1 \
    bash "$ROOT_DIR/tools/test-upstream-gtk4.sh"
