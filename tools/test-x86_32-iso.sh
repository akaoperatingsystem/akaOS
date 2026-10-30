#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT_DIR/tools/qemu-common.sh"

if [[ "${1:-}" != "--no-build" ]]; then
    "$ROOT_DIR/build.sh" --arch x86_32 --firmware both --disk-type iso
fi

ISO="$ROOT_DIR/build/akaos32.iso"
if [[ ! -f "$ISO" ]]; then
    echo "Missing x86_32 ISO: $ISO" >&2
    exit 1
fi

CATALOG="$ROOT_DIR/build/test-x86_32-eltorito.txt"
xorriso -indev "$ISO" -report_el_torito plain >"$CATALOG" 2>/dev/null
grep -Fq "BIOS" "$CATALOG"
grep -Fq "UEFI" "$CATALOG"

OVMF_CODE="$(find_ovmf_code)"
OVMF_VARS_SOURCE="$(find_ovmf_vars)"
OVMF_VARS="$ROOT_DIR/build/TEST_X86_32_OVMF_VARS.fd"
cp "$OVMF_VARS_SOURCE" "$OVMF_VARS"

python3 - "$ROOT_DIR" "$OVMF_CODE" "$OVMF_VARS" <<'PY'
import subprocess
import sys
import time
from pathlib import Path

root, ovmf_code, ovmf_vars = sys.argv[1:4]
root = Path(root)
iso = root / "build" / "akaos32.iso"

boots = {
    "uefi": [
        # OVMF plus the dual-firmware GRUB image and 32-bit kernel need more
        # headroom than the 128 MiB BIOS smoke test; avoid a false GRUB OOM.
        "qemu-system-x86_64", "-machine", "q35", "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
        "-cdrom", str(iso), "-boot", "d",
    ],
    "bios": [
        "qemu-system-i386", "-M", "pc", "-m", "128M",
        "-cdrom", str(iso), "-boot", "d",
    ],
}

for firmware, command in boots.items():
    log = root / "build" / f"test-x86_32-{firmware}-iso.log"
    log.unlink(missing_ok=True)
    process = subprocess.Popen(command + [
        "-display", "none", "-monitor", "none", "-serial", f"file:{log}"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 90
    try:
        while time.time() < deadline:
            text = log.read_text(errors="ignore") if log.exists() else ""
            if "[FATAL]" in text:
                raise RuntimeError(f"{firmware} boot emitted a fatal error")
            expected = "[ OK ] UEFI handoff received" if firmware == "uefi" else "[ OK ] BIOS Multiboot2 handoff received"
            if expected in text and "[PASS] akaOS user-mode milestone complete" in text and "login:" in text:
                break
            if process.poll() is not None:
                raise RuntimeError(f"{firmware} QEMU exited before login")
            time.sleep(0.1)
        else:
            raise RuntimeError(f"{firmware} boot timed out before login")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    print(f"[PASS] x86_32 {firmware.upper()} ISO reached login")
PY

echo "[PASS] akaOS x86_32 single-ISO UEFI/BIOS verification passed."
