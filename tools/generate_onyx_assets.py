#!/usr/bin/env python3
import os
import math
from PIL import Image, ImageDraw

os.makedirs("build/assets/icons", exist_ok=True)
os.makedirs("build/assets/backgrounds", exist_ok=True)

# 1. Terminal Icon (36x36): Dark obsidian squircle tile with cyan border & prompt
img_term = Image.new("RGBA", (36, 36), (0, 0, 0, 0))
draw = ImageDraw.Draw(img_term)
draw.rounded_rectangle([2, 2, 33, 33], radius=8, fill=(16, 20, 32, 245), outline=(56, 189, 248, 220), width=1)
draw.line([6, 4, 29, 4], fill=(255, 255, 255, 50), width=1)
# Cyan prompt >_
draw.line([9, 13, 15, 18], fill=(56, 189, 248, 255), width=2)
draw.line([15, 18, 9, 23], fill=(56, 189, 248, 255), width=2)
draw.line([18, 23, 26, 23], fill=(56, 189, 248, 255), width=2)
img_term.save("build/assets/icons/terminal.png")

# 2. Files Folder Icon (36x36): Modern macOS blue folder with sky-blue tab & document peek
img_files = Image.new("RGBA", (36, 36), (0, 0, 0, 0))
draw = ImageDraw.Draw(img_files)
# Back tab
draw.rounded_rectangle([4, 6, 18, 15], radius=3, fill=(96, 165, 250, 255))
# White document peek
draw.rounded_rectangle([11, 8, 26, 20], radius=2, fill=(248, 250, 252, 230))
# Front folder body
draw.rounded_rectangle([3, 12, 32, 32], radius=5, fill=(37, 99, 235, 255), outline=(30, 64, 175, 200), width=1)
draw.line([6, 13, 29, 13], fill=(147, 197, 253, 140), width=1)
img_files.save("build/assets/icons/files.png")

# 3. Settings Gear Icon (36x36): Dark metallic squircle with multi-tooth silver gear
img_gear = Image.new("RGBA", (36, 36), (0, 0, 0, 0))
draw = ImageDraw.Draw(img_gear)
draw.rounded_rectangle([2, 2, 33, 33], radius=8, fill=(36, 42, 56, 245), outline=(100, 116, 139, 180), width=1)
cx, cy = 18, 18
for i in range(8):
    angle = i * (math.pi / 4.0)
    tx = int(cx + 9 * math.cos(angle))
    ty = int(cy + 9 * math.sin(angle))
    draw.rectangle([tx - 1, ty - 1, tx + 1, ty + 1], fill=(203, 213, 225, 255))
draw.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=(203, 213, 225, 255), outline=(148, 163, 184, 255), width=2)
draw.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(24, 28, 40, 255))
img_gear.save("build/assets/icons/settings.png")

# 4. Trash Can Icon (36x36): Translucent frosted glass cylinder with metallic rim & ribs
img_trash = Image.new("RGBA", (36, 36), (0, 0, 0, 0))
draw = ImageDraw.Draw(img_trash)
# Top metallic rim
draw.ellipse([8, 6, 27, 11], fill=(148, 163, 184, 255), outline=(203, 213, 225, 255), width=1)
# Cylinder body
draw.polygon([(9, 8), (26, 8), (24, 31), (11, 31)], fill=(45, 55, 75, 190), outline=(100, 116, 139, 200))
draw.ellipse([11, 29, 24, 33], fill=(45, 55, 75, 200), outline=(100, 116, 139, 200), width=1)
# Vertical ribs
for rx in (14, 18, 22):
    draw.line([rx, 11, int(rx - (rx - 18)*0.2), 29], fill=(148, 163, 184, 180), width=1)
img_trash.save("build/assets/icons/trash.png")

# 5. Wi-Fi Glyph (24x24): Modern emerald green network arcs
img_wifi = Image.new("RGBA", (24, 24), (0, 0, 0, 0))
draw = ImageDraw.Draw(img_wifi)
draw.ellipse([10, 16, 13, 19], fill=(52, 211, 153, 255))
draw.arc([7, 11, 16, 20], start=210, end=330, fill=(52, 211, 153, 255), width=2)
draw.arc([4, 6, 19, 21], start=215, end=325, fill=(52, 211, 153, 255), width=2)
img_wifi.save("build/assets/icons/wifi.png")

# 6. High-Resolution Wallpaper (1280x800): Deep Obsidian Monterey Dunes
img_wall = Image.new("RGB", (1280, 800))
draw_wall = ImageDraw.Draw(img_wall)
for y in range(800):
    ratio = y / 800.0
    r = int(9 * (1 - ratio) + 15 * ratio)
    g = int(11 * (1 - ratio) + 18 * ratio)
    b = int(18 * (1 - ratio) + 32 * ratio)
    draw_wall.line([(0, y), (1280, y)], fill=(r, g, b))

for x in range(1280):
    d1 = int(480 + math.sin(x * 0.005) * 45 + math.cos(x * 0.002) * 20)
    for y in range(d1, 800):
        ratio = (y - d1) / (800 - d1)
        r = int(14 + ratio * 8)
        g = int(18 + ratio * 10)
        b = int(32 + ratio * 24)
        draw_wall.point((x, y), fill=(r, g, b))

    d2 = int(580 + math.sin(x * 0.007 + 1.4) * 40)
    for y in range(d2, 800):
        ratio = (y - d2) / (800 - d2)
        r = int(18 + ratio * 6)
        g = int(22 + ratio * 8)
        b = int(40 + ratio * 18)
        draw_wall.point((x, y), fill=(r, g, b))

img_wall.save("build/assets/backgrounds/onyx-default.png")

# 7. Generate C Header with RGBA Buffers for Lumen
with open("userspace/bin/lumen/dock_icons.h", "w") as f:
    f.write("/* Auto-generated 32-bit RGBA icon buffers for Lumen Compositor */\n")
    f.write("#ifndef LUMEN_DOCK_ICONS_H\n#define LUMEN_DOCK_ICONS_H\n#include <stdint.h>\n\n")
    
    def dump_image(name, im):
        w, h = im.size
        f.write(f"#define ICON_{name.upper()}_W {w}\n")
        f.write(f"#define ICON_{name.upper()}_H {h}\n")
        f.write(f"static const uint32_t icon_{name}[{w * h}] = {{\n")
        for y in range(h):
            for x in range(w):
                r, g, b, a = im.getpixel((x, y))
                val = (a << 24) | (r << 16) | (g << 8) | b
                f.write(f"0x{val:08x},")
            f.write("\n")
        f.write("};\n\n")

    dump_image("terminal", img_term)
    dump_image("files", img_files)
    dump_image("settings", img_gear)
    dump_image("trash", img_trash)
    dump_image("wifi", img_wifi)
    f.write("#endif\n")

print("Generated modern PNG icons and dock_icons.h successfully!")
