#!/usr/bin/env python3
"""Replace ELF64 DT_RPATH/DT_RUNPATH strings without changing file layout."""

import struct
import sys
from pathlib import Path


ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
DYNAMIC_ENTRY = struct.Struct("<qQ")
SHT_DYNAMIC = 6
DT_RPATH = 15
DT_RUNPATH = 29


def c_string(table: bytes, offset: int) -> bytes:
    if offset < 0 or offset >= len(table):
        raise ValueError("dynamic string offset is out of range")
    end = table.find(b"\0", offset)
    if end < 0:
        raise ValueError("dynamic string is not terminated")
    return table[offset:end]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: sanitize-elf-rpath.py ELF_FILE RUNPATH", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    desired = sys.argv[2].encode("utf-8")
    image = bytearray(path.read_bytes())
    if len(image) < ELF_HEADER.size:
        raise SystemExit(f"{path}: truncated ELF header")
    header = ELF_HEADER.unpack_from(image)
    ident = header[0]
    if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1:
        raise SystemExit(f"{path}: expected little-endian ELF64")

    section_offset = header[6]
    section_entry_size = header[11]
    section_count = header[12]
    if section_entry_size < SECTION_HEADER.size or section_count == 0:
        raise SystemExit(f"{path}: invalid ELF section table")
    if section_offset + section_entry_size * section_count > len(image):
        raise SystemExit(f"{path}: truncated ELF section table")

    sections = [
        SECTION_HEADER.unpack_from(image, section_offset + index * section_entry_size)
        for index in range(section_count)
    ]
    string_table_index = header[13]
    if string_table_index >= section_count:
        raise SystemExit(f"{path}: invalid section-name table index")
    string_header = sections[string_table_index]
    string_offset, string_size = string_header[4], string_header[5]
    if string_offset + string_size > len(image):
        raise SystemExit(f"{path}: truncated section-name table")
    section_names = bytes(image[string_offset:string_offset + string_size])

    dynamic_sections = []
    for section in sections:
        if section[1] == SHT_DYNAMIC:
            dynamic_sections.append(section)
    changed = False
    for dynamic in dynamic_sections:
        dynstr_index = dynamic[6]
        if dynstr_index >= section_count:
            raise SystemExit(f"{path}: invalid dynamic string-table index")
        dynstr = sections[dynstr_index]
        dynstr_offset, dynstr_size = dynstr[4], dynstr[5]
        if dynstr_offset + dynstr_size > len(image):
            raise SystemExit(f"{path}: truncated dynamic string table")
        entry_size = dynamic[9] or DYNAMIC_ENTRY.size
        if entry_size < DYNAMIC_ENTRY.size:
            raise SystemExit(f"{path}: invalid dynamic entry size")
        start = dynamic[4]
        size = dynamic[5]
        if start + size > len(image):
            raise SystemExit(f"{path}: truncated dynamic section")
        for entry_offset in range(start, start + size, entry_size):
            tag, value = DYNAMIC_ENTRY.unpack_from(image, entry_offset)
            if tag not in (DT_RPATH, DT_RUNPATH):
                continue
            old = c_string(image[dynstr_offset:dynstr_offset + dynstr_size], value)
            if old == desired:
                continue
            if len(desired) > len(old):
                raise SystemExit(f"{path}: requested runpath does not fit in ELF string slot")
            slot = dynstr_offset + value
            replacement = desired + bytes(len(old) - len(desired)) + b"\0"
            image[slot:slot + len(replacement)] = replacement
            changed = True

    if changed:
        path.write_bytes(image)
    print(f"[RPATH] {path}: {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
