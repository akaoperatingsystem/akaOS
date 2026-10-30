#!/usr/bin/env python3
"""Generate public, anti-aliased DejaVu font atlases for native akaOS UI code."""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FIRST = 32
LAST = 126
HEIGHT = 18


def generate(font_path: str, size: int, canvas_width: int, prefix: str, output: Path,
             fixed_advance: int | None = None) -> None:
    font = ImageFont.truetype(font_path, size)
    glyphs: list[list[int]] = []
    advances: list[int] = []
    for codepoint in range(FIRST, LAST + 1):
        char = chr(codepoint)
        image = Image.new("L", (canvas_width, HEIGHT), 0)
        draw = ImageDraw.Draw(image)
        # A shared baseline preserves natural ascenders, descenders and punctuation.
        draw.text((1, 14), char, font=font, fill=255, anchor="ls")
        glyphs.append(list(image.get_flattened_data()))
        advances.append(fixed_advance or max(3, min(canvas_width, round(font.getlength(char)) + 1)))

    guard = f"AKAOS_{prefix.upper()}_FONT_H"
    lines = [
        f"#ifndef {guard}", f"#define {guard}", "", "#include <stdint.h>", "",
        "/* Generated from DejaVu Fonts 2.37. DejaVu derives from Bitstream Vera",
        " * and is redistributable under its permissive font license. Do not edit",
        " * this atlas manually; run tools/generate-ui-font.py. */",
        f"#define {prefix.upper()}_FONT_FIRST {FIRST}U",
        f"#define {prefix.upper()}_FONT_LAST {LAST}U",
        f"#define {prefix.upper()}_FONT_WIDTH {canvas_width}U",
        f"#define {prefix.upper()}_FONT_HEIGHT {HEIGHT}U", "",
        f"static const uint8_t {prefix}_font_advance[{LAST - FIRST + 1}] = {{",
    ]
    for start in range(0, len(advances), 16):
        lines.append("    " + ", ".join(str(v) for v in advances[start:start + 16]) + ",")
    lines.extend(["};", "", f"static const uint8_t {prefix}_font_alpha"
                  f"[{LAST - FIRST + 1}][{HEIGHT}][{canvas_width}] = {{"])
    for index, pixels in enumerate(glyphs):
        lines.append(f"    /* {index + FIRST}: {chr(index + FIRST)!r} */ {{")
        for row in range(HEIGHT):
            values = pixels[row * canvas_width:(row + 1) * canvas_width]
            lines.append("        {" + ",".join(str(v) for v in values) + "},")
        lines.append("    },")
    lines.extend([
        "};", "",
        f"static inline uint8_t {prefix}_font_get_advance(char character) {{",
        f"    unsigned code = (unsigned char)character;",
        f"    if (code < {prefix.upper()}_FONT_FIRST || code > {prefix.upper()}_FONT_LAST) code = '?';",
        f"    return {prefix}_font_advance[code - {prefix.upper()}_FONT_FIRST];",
        "}", "",
        f"static inline uint8_t {prefix}_font_get_alpha(char character, uint32_t y, uint32_t x) {{",
        f"    unsigned code = (unsigned char)character;",
        f"    if (x >= {prefix.upper()}_FONT_WIDTH || y >= {prefix.upper()}_FONT_HEIGHT) return 0;",
        f"    if (code < {prefix.upper()}_FONT_FIRST || code > {prefix.upper()}_FONT_LAST) code = '?';",
        f"    return {prefix}_font_alpha[code - {prefix.upper()}_FONT_FIRST][y][x];",
        "}", "", f"#endif /* {guard} */", "",
    ])
    output.write_text("\n".join(lines), encoding="ascii")


generate("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15, 16,
         "akaos_ui", ROOT / "sdk/include/akaos/ui_font_aa.h")
generate("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 14, 10,
         "akaos_mono", ROOT / "sdk/include/akaos/ui_font_mono_aa.h", fixed_advance=9)
