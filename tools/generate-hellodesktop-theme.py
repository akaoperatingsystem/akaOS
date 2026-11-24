#!/usr/bin/env python3
"""Generate the helloDesktop theme for the Openbox window manager.

The theme gives the window frames of helloDesktop the look of the macOS
class user interface: a dark title bar and the three traffic light buttons
that close, minimize and maximize a window.

Openbox draws a title bar button as a one bit mask that it tints with a
colour of the theme, so every button is a separate mask with its own
colour.  The masks are written in the X bitmap format, which is the format
the window manager reads.

Usage:
    python3 tools/generate-hellodesktop-theme.py
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
THEME_DIR = ROOT / "userspace" / "share" / "themes" / "hellodesktop" / "openbox-3"

SIZE = 16
RADIUS = 7.4
CENTRE = (SIZE - 1) / 2.0

# The colours of the traffic lights of macOS.
COLOUR_CLOSE = "#ff5f57"
COLOUR_MINIMIZE = "#febc2e"
COLOUR_MAXIMIZE = "#28c840"
COLOUR_CLOSE_HOVER = "#ff6b63"
COLOUR_MINIMIZE_HOVER = "#ffc744"
COLOUR_MAXIMIZE_HOVER = "#3ad45c"
COLOUR_UNFOCUSED = "#8f8f8f"
COLOUR_UNFOCUSED_HOVER = "#b4b4b4"


def in_circle(x: int, y: int) -> bool:
    """Whether the pixel belongs to the disc of the traffic light."""
    dx = x - CENTRE
    dy = y - CENTRE
    return dx * dx + dy * dy <= RADIUS * RADIUS


def in_cross(x: int, y: int) -> bool:
    """Whether the pixel belongs to the cross of the close button."""
    distance = abs(x - CENTRE)
    return abs(x - y) <= 1.2 or abs(x + y - (SIZE - 1)) <= 1.2


def in_bar(x: int, y: int) -> bool:
    """Whether the pixel belongs to the bar of the minimize button."""
    return 4 <= x <= SIZE - 5 and abs(y - (SIZE - 4)) <= 0.9


def in_plus(x: int, y: int) -> bool:
    """Whether the pixel belongs to the cross of the maximize button."""
    return (abs(x - CENTRE) <= 0.9 and 3 <= y <= SIZE - 4) or (
        abs(y - CENTRE) <= 0.9 and 3 <= x <= SIZE - 4
    )


def build_mask(shape) -> list:
    """Build the rows of a mask that holds the given shape."""
    rows = []
    for y in range(SIZE):
        row = 0
        for x in range(SIZE):
            if shape(x, y):
                row |= 1 << x
        rows.append(row)
    return rows


def write_xbm(path: Path, name: str, rows: list) -> None:
    """Write the mask in the X bitmap format that Openbox reads."""
    stride = (SIZE + 7) // 8
    data = bytearray()
    for row in rows:
        for index in range(stride):
            if index == 0:
                data.append(row & 0xff)
            elif index == 1:
                data.append((row >> 8) & 0xff)
            else:
                data.append(0)
    values = ", ".join("0x%02x" % byte for byte in data)
    lines = []
    lines.append("#define %s_width %d" % (name, SIZE))
    lines.append("#define %s_height %d" % (name, SIZE))
    lines.append("static unsigned char %s_bits[] = {" % name)
    for index in range(0, len(values), 72):
        lines.append("  " + values[index:index + 72])
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def circle_only(x: int, y: int) -> bool:
    return in_circle(x, y)


def circle_with_cross(x: int, y: int) -> bool:
    return in_circle(x, y) and not in_cross(x, y)


def circle_with_bar(x: int, y: int) -> bool:
    return in_circle(x, y) and not in_bar(x, y)


def circle_with_plus(x: int, y: int) -> bool:
    return in_circle(x, y) and not in_plus(x, y)


def themerc() -> str:
    """The description of the theme in the format that Openbox reads."""
    lines = []
    lines.append("!! helloDesktop theme for Openbox")
    lines.append("!! The window frames follow the macOS class look of helloSystem:")
    lines.append("!! a dark title bar and the three traffic light buttons.")
    lines.append("")
    lines.append("!! General")
    lines.append("border.width: 1")
    lines.append("padding.width: 6")
    lines.append("window.handle.width: 0")
    lines.append("window.client.padding.width: 0")
    lines.append("window.client.padding.height: 0")
    lines.append("menu.overlap: 0")
    lines.append("border.color: #1c1c1c")
    lines.append("window.*.client.color: #1e1e1e")
    lines.append("")
    lines.append("!! Title bar")
    lines.append("window.active.title.bg: flat solid")
    lines.append("window.active.title.bg.color: #2d2d2d")
    lines.append("window.active.label.bg: parentrelative")
    lines.append("window.active.label.text.color: #f2f2f2")
    lines.append("window.active.label.text.justify: center")
    lines.append("window.inactive.title.bg: flat solid")
    lines.append("window.inactive.title.bg.color: #232323")
    lines.append("window.inactive.label.bg: parentrelative")
    lines.append("window.inactive.label.text.color: #8a8a8a")
    lines.append("window.inactive.label.text.justify: center")
    lines.append("")
    lines.append("!! The traffic lights of the buttons")
    lines.append("!! The colour of every button is the colour of its mask, so the")
    lines.append("!! three buttons of the window frame carry their own colour.")
    lines.append("window.active.button.*.bg: parentrelative")
    lines.append("window.active.button.close.unpressed.image.color: " + COLOUR_CLOSE)
    lines.append("window.active.button.close.hover.image.color: " + COLOUR_CLOSE_HOVER)
    lines.append("window.active.button.close.pressed.image.color: " + COLOUR_CLOSE_HOVER)
    lines.append("window.active.button.iconify.unpressed.image.color: " + COLOUR_MINIMIZE)
    lines.append("window.active.button.iconify.hover.image.color: " + COLOUR_MINIMIZE_HOVER)
    lines.append("window.active.button.iconify.pressed.image.color: " + COLOUR_MINIMIZE_HOVER)
    lines.append("window.active.button.max.unpressed.image.color: " + COLOUR_MAXIMIZE)
    lines.append("window.active.button.max.hover.image.color: " + COLOUR_MAXIMIZE_HOVER)
    lines.append("window.active.button.max.pressed.image.color: " + COLOUR_MAXIMIZE_HOVER)
    lines.append("window.active.button.desk.unpressed.image.color: #9a9a9a")
    lines.append("window.active.button.shade.unpressed.image.color: #9a9a9a")
    lines.append("")
    lines.append("!! A window that lost the focus keeps the traffic lights, but")
    lines.append("!! they lose their colour, the way macOS draws them.")
    lines.append("window.inactive.button.*.bg: parentrelative")
    lines.append("window.inactive.button.close.unpressed.image.color: " + COLOUR_UNFOCUSED)
    lines.append("window.inactive.button.close.hover.image.color: " + COLOUR_UNFOCUSED_HOVER)
    lines.append("window.inactive.button.iconify.unpressed.image.color: " + COLOUR_UNFOCUSED)
    lines.append("window.inactive.button.iconify.hover.image.color: " + COLOUR_UNFOCUSED_HOVER)
    lines.append("window.inactive.button.max.unpressed.image.color: " + COLOUR_UNFOCUSED)
    lines.append("window.inactive.button.max.hover.image.color: " + COLOUR_UNFOCUSED_HOVER)
    lines.append("window.inactive.button.desk.unpressed.image.color: #6d6d6d")
    lines.append("window.inactive.button.shade.unpressed.image.color: #6d6d6d")
    lines.append("")
    lines.append("!! Menu")
    lines.append("menu.title.bg: flat solid")
    lines.append("menu.title.bg.color: #2d2d2d")
    lines.append("menu.title.text.color: #f2f2f2")
    lines.append("menu.title.text.justify: center")
    lines.append("menu.items.bg: flat solid")
    lines.append("menu.items.bg.color: #232323")
    lines.append("menu.items.text.color: #d8d8d8")
    lines.append("menu.items.justify: left")
    lines.append("menu.items.disabled.text.color: #6d6d6d")
    lines.append("menu.items.active.bg: flat solid")
    lines.append("menu.items.active.bg.color: #3d6ea5")
    lines.append("menu.items.active.text.color: #ffffff")
    lines.append("menu.border.color: #1c1c1c")
    lines.append("")
    lines.append("!! Fonts")
    lines.append("window.active.label.text.font: DejaVu Sans-10")
    lines.append("window.inactive.label.text.font: DejaVu Sans-10")
    lines.append("menu.items.font: DejaVu Sans-9")
    lines.append("menu.title.text.font: DejaVu Sans-9")
    return "\n".join(lines) + "\n"


def main() -> int:
    THEME_DIR.mkdir(parents=True, exist_ok=True)
    write_xbm(THEME_DIR / "close.xbm", "close", build_mask(circle_only))
    write_xbm(THEME_DIR / "close_hover.xbm", "close_hover", build_mask(circle_with_cross))
    write_xbm(THEME_DIR / "iconify.xbm", "iconify", build_mask(circle_only))
    write_xbm(THEME_DIR / "iconify_hover.xbm", "iconify_hover", build_mask(circle_with_bar))
    write_xbm(THEME_DIR / "max.xbm", "max", build_mask(circle_only))
    write_xbm(THEME_DIR / "max_hover.xbm", "max_hover", build_mask(circle_with_plus))
    (THEME_DIR / "themerc").write_text(themerc(), encoding="utf-8", newline="\n")
    print("helloDesktop theme written to %s" % THEME_DIR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
