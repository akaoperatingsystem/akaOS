#!/usr/bin/env python3
"""
tools/mkinitramfs.py - akaOS Initramfs Archive Generator

Creates a deterministic binary archive for mounting by the XSCore VFS at boot.

Format:
  Header (24 bytes):
    magic:       8 bytes ("AKAOSRAM")
    version:     uint32 (1)
    entry_count: uint32
    total_size:  uint64
  Entry Table (152 bytes * entry_count):
    path:        128 bytes (null-terminated string)
    type:        uint32 (1 = Directory, 2 = Regular file)
    mode:        uint32 (Unix permission and special bits)
    size:        uint64 (file size in bytes)
    data_offset: uint64 (byte offset from archive start to payload)
  Payload:
    Concatenated raw bytes for each regular file.
"""

import argparse
import struct
import sys
from pathlib import Path

ENTRY_TYPE_DIR = 1
ENTRY_TYPE_FILE = 2
XSCORE_VFS_NAME_MAX = 255

def parse_mode(text, default):
    if text is None:
        return default
    if text == "exec":
        return 0o555
    try:
        return int(text, 8)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid mode '{text}' (use exec or octal, e.g. 4755)")


def has_unsupported_vfs_name(path):
    return any(
        len(component.encode("utf-8")) > XSCORE_VFS_NAME_MAX
        for component in path.split("/") if component
    )


def read_archive(path):
    data = Path(path).read_bytes()
    if len(data) < 24:
        raise ValueError(f"base archive is shorter than its header: {path}")

    magic, version, entry_count, total_size = struct.unpack_from("<8sIIQ", data)
    if magic != b"AKAOSRAM" or version != 1:
        raise ValueError(f"unsupported base archive format: {path}")

    table_end = 24 + (152 * entry_count)
    if table_end > len(data) or total_size != len(data):
        raise ValueError(f"base archive has an invalid size or entry table: {path}")

    entries = []
    for index in range(entry_count):
        offset = 24 + (152 * index)
        path_bytes, entry_type, mode, size, data_offset = struct.unpack_from(
            "<128sIIQQ", data, offset)
        path_text = path_bytes.split(b"\x00", 1)[0].decode("utf-8")

        if entry_type == ENTRY_TYPE_DIR:
            if size != 0:
                raise ValueError(f"base archive directory has data: {path_text}")
            contents = b""
        elif entry_type == ENTRY_TYPE_FILE:
            if data_offset < table_end or data_offset > total_size or size > total_size - data_offset:
                raise ValueError(f"base archive file is out of bounds: {path_text}")
            contents = data[data_offset:data_offset + size]
        else:
            raise ValueError(f"base archive has an unknown entry type: {entry_type}")

        entries.append({
            "path": path_text,
            "type": entry_type,
            "mode": mode,
            "data": contents,
        })

    return entries


def main():
    parser = argparse.ArgumentParser(description="Create or update an akaOS initramfs archive")
    parser.add_argument("output", help="Output archive path (e.g., build/initramfs.img)")
    parser.add_argument("--base", help="Preserve entries from this existing archive, then overlay supplied entries")
    parser.add_argument("--exclude-prefix", action="append", default=[],
                        help="Remove this path and its descendants from a base archive before applying overlays")
    parser.add_argument("--exclude-long-vfs-names", action="store_true",
                        help="Skip entries with a path component longer than XSCore VFS supports (255 bytes)")
    parser.add_argument("--profile", choices=("full", "minimal"), default="full",
                        help="minimal retains console login and essential recovery commands")
    parser.add_argument("--dir", action="append", default=[], help="Add directory path (e.g. /bin)")
    parser.add_argument("--file", action="append", default=[],
                        help="Add file: VFS_PATH:HOST_PATH[:MODE] (mode can be 'exec')")
    parser.add_argument("--data", action="append", default=[],
                        help="Add file with raw text: VFS_PATH:TEXT[:MODE]")

    args = parser.parse_args()

    overlay_entries = []
    # Collect directories
    for d in args.dir:
        parts = d.rsplit(":", 1)
        path = parts[0]
        mode = parse_mode(parts[1], 0o755) if len(parts) == 2 else 0o755
        overlay_entries.append({
            "path": path,
            "type": ENTRY_TYPE_DIR,
            "mode": mode,
            "data": b"",
        })

    # Collect files from host
    for f in args.file:
        parts = f.split(":")
        vfs_path = parts[0]
        host_path = parts[1]
        mode = parse_mode(parts[2] if len(parts) > 2 else None, 0o444)
        with open(host_path, "rb") as fp:
            data = fp.read()
        overlay_entries.append({
            "path": vfs_path,
            "type": ENTRY_TYPE_FILE,
            "mode": mode,
            "data": data,
        })

    # Collect raw data files
    for d in args.data:
        parts = d.split(":", 2)
        vfs_path = parts[0]
        text = parts[1]
        mode = parse_mode(parts[2] if len(parts) > 2 else None, 0o444)
        # Handle escape sequences like \n
        raw_bytes = text.encode("utf-8").decode("unicode_escape").encode("utf-8")
        overlay_entries.append({
            "path": vfs_path,
            "type": ENTRY_TYPE_FILE,
            "mode": mode,
            "data": raw_bytes,
        })

    try:
        base_entries = read_archive(args.base) if args.base else []
        excluded_count = 0
        if args.exclude_prefix:
            prefixes = tuple(prefix.rstrip("/") for prefix in args.exclude_prefix)
            kept_base_entries = []
            for entry in base_entries:
                path = entry["path"]
                if any(path == prefix or path.startswith(prefix + "/")
                       for prefix in prefixes):
                    excluded_count += 1
                else:
                    kept_base_entries.append(entry)
            base_entries = kept_base_entries
        if args.profile == "minimal":
            # Preserve a console recovery path. Directories are retained so VFS
            # parents exist, while only core login/shell/runtime files remain.
            keep_files = {
                "/bin/init", "/bin/login", "/bin/akash", "/bin/bash", "/bin/sh", "/bin/rbash",
                "/bin/echo", "/bin/ls", "/bin/cat", "/bin/ps", "/bin/free", "/bin/uname",
                "/bin/whoami", "/bin/clear", "/bin/which", "/bin/env", "/bin/sleep", "/bin/pwd",
                "/bin/touch", "/bin/mkdir", "/bin/rm", "/bin/cp", "/bin/mv", "/bin/true",
                "/bin/false", "/bin/test", "/bin/[", "/bin/hello", "/bin/akafetch",
                "/lib/ld-akaos.so", "/lib/libc.so.6", "/lib/libc.so", "/lib/libm.so.6",
                "/etc/passwd", "/etc/group", "/etc/shadow", "/etc/sudoers", "/etc/profile",
                "/etc/shells", "/etc/akaos.conf", "/etc/hostname", "/etc/hosts", "/etc/resolv.conf",
                "/etc/fstab", "/etc/motd", "/etc/dbus-services.conf"
            }
            before = len(base_entries) + len(overlay_entries)
            base_entries = [e for e in base_entries if e["type"] == ENTRY_TYPE_DIR or e["path"] in keep_files]
            overlay_entries = [e for e in overlay_entries if e["type"] == ENTRY_TYPE_DIR or e["path"] in keep_files]
            excluded_count += before - len(base_entries) - len(overlay_entries)
        if args.exclude_long_vfs_names:
            kept_base_entries = []
            for entry in base_entries:
                if has_unsupported_vfs_name(entry["path"]):
                    excluded_count += 1
                else:
                    kept_base_entries.append(entry)
            base_entries = kept_base_entries
            kept_overlay_entries = []
            for entry in overlay_entries:
                if has_unsupported_vfs_name(entry["path"]):
                    excluded_count += 1
                else:
                    kept_overlay_entries.append(entry)
            overlay_entries = kept_overlay_entries
        if args.profile == "minimal":
            keep_files = {
                "/bin/init", "/bin/login", "/bin/akash", "/bin/bash", "/bin/sh", "/bin/rbash",
                "/bin/echo", "/bin/ls", "/bin/cat", "/bin/ps", "/bin/free", "/bin/uname",
                "/bin/whoami", "/bin/clear", "/bin/which", "/bin/env", "/bin/sleep",
                "/bin/pwd", "/bin/touch", "/bin/mkdir", "/bin/rm", "/bin/cp", "/bin/mv",
                "/bin/true", "/bin/false", "/bin/test", "/bin/[", "/bin/hello", "/bin/akafetch",
                "/lib/ld-akaos.so", "/lib/libc.so.6", "/lib/libc.so", "/lib/libm.so.6",
                "/etc/passwd", "/etc/group", "/etc/shadow", "/etc/sudoers", "/etc/profile",
                "/etc/shells", "/etc/akaos.conf", "/etc/hostname", "/etc/hosts", "/etc/resolv.conf",
                "/etc/fstab", "/etc/motd", "/etc/dbus-services.conf"
            }
            def essential(entry):
                path = entry["path"]
                return entry["type"] == ENTRY_TYPE_DIR or path in keep_files
            before = len(base_entries) + len(overlay_entries)
            base_entries = [entry for entry in base_entries if essential(entry)]
            overlay_entries = [entry for entry in overlay_entries if essential(entry)]
            excluded_count += before - len(base_entries) - len(overlay_entries)
        entries_by_path = {entry["path"]: entry for entry in base_entries}
        for entry in overlay_entries:
            entries_by_path[entry["path"]] = entry
        entries = list(entries_by_path.values())
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    if excluded_count:
        print(f"[INITRAMFS] Skipped {excluded_count} entries with VFS path components longer than {XSCORE_VFS_NAME_MAX} bytes")

    # XSCore mounts entries sequentially. Emit an explicit directory's
    # ancestors first, while preserving the existing order of unrelated files
    # and directories in base archives.
    entries_by_path = {entry["path"]: entry for entry in entries}
    ordered_entries = []
    emitted_paths = set()

    def ensure_directory(path):
        if not path or path == "/" or path in emitted_paths:
            return
        parent = path.rsplit("/", 1)[0]
        if parent:
            ensure_directory(parent)
        if path not in emitted_paths:
            entry = entries_by_path.get(path)
            if not entry:
                entry = {
                    "path": path,
                    "type": ENTRY_TYPE_DIR,
                    "mode": 0o755,
                    "data": b"",
                }
                entries_by_path[path] = entry
            ordered_entries.append(entry)
            emitted_paths.add(path)

    for entry in entries:
        parent = entry["path"].rsplit("/", 1)[0]
        if parent:
            ensure_directory(parent)
        if entry["type"] == ENTRY_TYPE_DIR:
            ensure_directory(entry["path"])
        else:
            ordered_entries.append(entry)
            emitted_paths.add(entry["path"])
    entries = ordered_entries

    entry_count = len(entries)
    header_size = 24
    entry_table_size = 152 * entry_count
    current_data_offset = header_size + entry_table_size

    # Assign data offsets
    for entry in entries:
        if entry["type"] == ENTRY_TYPE_FILE:
            entry["size"] = len(entry["data"])
            entry["data_offset"] = current_data_offset
            current_data_offset += len(entry["data"])
        else:
            entry["size"] = 0
            entry["data_offset"] = 0

    total_size = current_data_offset

    # Pack header
    header_fmt = "<8sIIQ"
    header_bytes = struct.pack(header_fmt, b"AKAOSRAM", 1, entry_count, total_size)

    # Pack entry table
    entry_fmt = "<128sIIQQ"
    entries_bytes = bytearray()
    payload_bytes = bytearray()

    for entry in entries:
        path_bytes = entry["path"].encode("utf-8")
        if len(path_bytes) >= 128:
            print(f"Error: Path too long: {entry['path']}", file=sys.stderr)
            sys.exit(1)
        padded_path = path_bytes.ljust(128, b"\x00")
        entry_packed = struct.pack(
            entry_fmt,
            padded_path,
            entry["type"],
            entry["mode"],
            entry["size"],
            entry["data_offset"],
        )
        entries_bytes.extend(entry_packed)
        if entry["type"] == ENTRY_TYPE_FILE:
            payload_bytes.extend(entry["data"])

    # Write output
    with open(args.output, "wb") as fp:
        fp.write(header_bytes)
        fp.write(entries_bytes)
        fp.write(payload_bytes)

    print(f"[INITRAMFS] Created {args.output} ({entry_count} entries, {total_size} bytes)")

if __name__ == "__main__":
    main()
