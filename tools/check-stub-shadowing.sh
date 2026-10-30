#!/usr/bin/env bash
set -euo pipefail
# Find the programs and libraries that depend on an akaOS compatibility library
# under the name of a real library.
#
# The compatibility libraries of akaOS answer for the GLib, GTK and Qt entry
# points that the upstream desktop programs use.  A program that finds one of
# them under the name of a real library takes the compatibility surface in
# place of the real one, and the real library never gets a say.  This script
# lists every binary that carries such a dependency.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STUB_NAMES="libpthread\.so|libglib\.so|libgio\.so|libgtk\.so|libgtk-4\.so|libQt6Core\.so|libQt6Gui\.so|libQt6Widgets\.so"

scan() {
    local root="$1"
    [[ -d "$root" ]] || return 0
    while IFS= read -r file; do
        local needed
        needed="$(readelf -d "$file" 2>/dev/null |
            grep -oE "NEEDED.*\[($STUB_NAMES)\]" || true)"
        if [[ -n "$needed" ]]; then
            echo "$file"
            echo "$needed" | sed 's/^/    /'
        fi
    done < <(find "$root" -type f \( -name '*.so*' -o -perm -u+x \) 2>/dev/null)
}

status=0
for root in "$@"; do
    scan "$root"
done
exit "$status"
