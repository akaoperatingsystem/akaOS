#!/usr/bin/env python3
"""Generate the small anti-aliased font atlas compiled into XSCore diagnostics."""

from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont


FIRST = 32
LAST = 126
CELL_WIDTH = 18
CELL_HEIGHT = 24
BASELINE = 19
FONT_SIZE = 17
LOGO_MARGIN = 2


def values(data: bytes, indent: str = "    ") -> str:
    rows = []
    for offset in range(0, len(data), 24):
        rows.append(indent + ", ".join(str(value) for value in data[offset : offset + 24]))
    return ",\n".join(rows)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: generate_font.py FONT_FILE OUTPUT_HEADER", file=sys.stderr)
        return 2
    font_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    if not font_path.is_file():
        print(f"font not found: {font_path}", file=sys.stderr)
        return 1

    font = ImageFont.truetype(str(font_path), FONT_SIZE)
    glyphs = []
    advances = []
    for codepoint in range(FIRST, LAST + 1):
        image = Image.new("L", (CELL_WIDTH, CELL_HEIGHT), 0)
        draw = ImageDraw.Draw(image)
        draw.text((2, BASELINE), chr(codepoint), font=font, fill=255, anchor="ls")
        glyphs.extend(image.tobytes())
        advances.append(max(4, min(CELL_WIDTH, round(font.getlength(chr(codepoint))))))

    logo_font = ImageFont.truetype(str(font_path), 48)
    logo_text = "akaOS"
    logo_left, logo_top, logo_right, logo_bottom = logo_font.getbbox(logo_text)
    logo_width = logo_right - logo_left + (LOGO_MARGIN * 2)
    logo_height = logo_bottom - logo_top + (LOGO_MARGIN * 2)
    logo = Image.new("L", (logo_width, logo_height), 0)
    ImageDraw.Draw(logo).text(
        (LOGO_MARGIN - logo_left, LOGO_MARGIN - logo_top),
        logo_text,
        font=logo_font,
        fill=255,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        "#ifndef AKAOS_GENERATED_FONT_DATA_H\n"
        "#define AKAOS_GENERATED_FONT_DATA_H\n\n"
        "#include <stdint.h>\n\n"
        f"#define AKAOS_FONT_FIRST {FIRST}\n"
        f"#define AKAOS_FONT_LAST {LAST}\n"
        f"#define AKAOS_FONT_CELL_WIDTH {CELL_WIDTH}\n"
        f"#define AKAOS_FONT_CELL_HEIGHT {CELL_HEIGHT}\n"
        f"#define AKAOS_LOGO_WIDTH {logo_width}\n"
        f"#define AKAOS_LOGO_HEIGHT {logo_height}\n\n"
        f"static const uint8_t akaos_font_advances[{len(advances)}] = {{\n{values(bytes(advances))}\n}};\n\n"
        f"static const uint8_t akaos_font_alpha[{len(glyphs)}] = {{\n{values(bytes(glyphs))}\n}};\n\n"
        f"static const uint8_t akaos_logo_alpha[{logo_width * logo_height}] = {{\n{values(logo.tobytes())}\n}};\n\n"
        "#endif\n",
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
