#!/usr/bin/env python3
"""
High-Fidelity macOS-Class 48x48 Squircle Icon & Silk Gradient Wallpaper Generator for akaOS
"""
import os
import math
from PIL import Image, ImageDraw, ImageFilter

os.makedirs("build/assets/icons", exist_ok=True)
os.makedirs("build/assets/backgrounds", exist_ok=True)

def create_squircle_base(size=192, radius=48, top_color=(30, 36, 52), bot_color=(15, 18, 28), border_color=(70, 85, 120, 180)):
    """Creates a 192x192 high-res squircle with realistic lighting and subtle inner shadow"""
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)
    
    # Bottom ambient shadow
    shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(shadow)
    sdraw.rounded_rectangle([16, 24, size - 17, size - 8], radius=radius, fill=(0, 0, 0, 90))
    shadow = shadow.filter(ImageFilter.GaussianBlur(8))
    im.paste(shadow, (0, 0), shadow)

    # Gradient squircle body
    body = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bdraw = ImageDraw.Draw(body)
    for y in range(16, size - 16):
        t = (y - 16) / float(size - 32)
        r = int(top_color[0] * (1 - t) + bot_color[0] * t)
        g = int(top_color[1] * (1 - t) + bot_color[1] * t)
        b = int(top_color[2] * (1 - t) + bot_color[2] * t)
        bdraw.line([(16, y), (size - 17, y)], fill=(r, g, b, 255))
        
    mask = Image.new("L", (size, size), 0)
    mdraw = ImageDraw.Draw(mask)
    mdraw.rounded_rectangle([16, 16, size - 17, size - 17], radius=radius, fill=255)
    
    # 1px crisp border
    bdraw.rounded_rectangle([16, 16, size - 17, size - 17], radius=radius, outline=border_color, width=3)
    # Specular top highlight
    bdraw.line([36, 18, size - 37, 18], fill=(255, 255, 255, 65), width=2)
    
    im.paste(body, (0, 0), mask)
    return im

# 1. Terminal Icon (48x48)
# Deep obsidian glass tile (#0f1422), thin cyan accent border (#38bdf8), glossy diagonal window reflection, glowing prompt >_
im_term = create_squircle_base(192, 48, top_color=(20, 26, 40), bot_color=(10, 14, 22), border_color=(56, 189, 248, 160))
draw = ImageDraw.Draw(im_term)

# Diagonal gloss reflection
gloss = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
gdraw = ImageDraw.Draw(gloss)
gdraw.polygon([(16, 16), (120, 16), (16, 120)], fill=(255, 255, 255, 22))
im_term = Image.alpha_composite(im_term, gloss)
draw = ImageDraw.Draw(im_term)

# Glowing cyan prompt >_
# Main prompt stroke
draw.line([52, 70, 82, 100], fill=(56, 189, 248, 255), width=8)
draw.line([82, 100, 52, 130], fill=(56, 189, 248, 255), width=8)
draw.line([96, 130, 140, 130], fill=(56, 189, 248, 255), width=8)
# Inner bright core
draw.line([54, 70, 82, 98], fill=(224, 242, 254, 220), width=3)
draw.line([82, 102, 54, 130], fill=(224, 242, 254, 220), width=3)
draw.line([98, 130, 138, 130], fill=(224, 242, 254, 220), width=3)

icon_term = im_term.resize((48, 48), Image.Resampling.LANCZOS)
icon_term.save("build/assets/icons/terminal.png")

# 2. Files Folder Icon (48x48)
# Authentic macOS-style 3D rounded blue folder with realistic lighting gradient (#60a5fa -> #1d4ed8), document peek, inner bevel
im_files = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
fdraw = ImageDraw.Draw(im_files)

# Bottom shadow
fshadow = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
fsdraw = ImageDraw.Draw(fshadow)
fsdraw.rounded_rectangle([20, 48, 172, 168], radius=24, fill=(0, 0, 0, 85))
fshadow = fshadow.filter(ImageFilter.GaussianBlur(6))
im_files.paste(fshadow, (0, 0), fshadow)

# Back folder tab
fdraw.rounded_rectangle([22, 34, 96, 75], radius=16, fill=(96, 165, 250, 255))
fdraw.rounded_rectangle([22, 44, 170, 150], radius=20, fill=(59, 130, 246, 255))

# White document peek
doc = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
ddraw = ImageDraw.Draw(doc)
ddraw.rounded_rectangle([52, 42, 140, 110], radius=8, fill=(248, 250, 252, 240), outline=(203, 213, 225, 255), width=2)
ddraw.line([68, 62, 124, 62], fill=(148, 163, 184, 220), width=3)
ddraw.line([68, 76, 110, 76], fill=(148, 163, 184, 220), width=3)
im_files.paste(doc, (0, 0), doc)

# Front folder body with rich lighting gradient
fbody = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
fbdraw = ImageDraw.Draw(fbody)
for y in range(65, 165):
    t = (y - 65) / 100.0
    # From royal blue to deep sapphire
    r = int(59 * (1 - t) + 29 * t)
    g = int(130 * (1 - t) + 78 * t)
    b = int(246 * (1 - t) + 216 * t)
    fbdraw.line([(18, y), (174, y)], fill=(r, g, b, 255))

fmask = Image.new("L", (192, 192), 0)
fmdraw = ImageDraw.Draw(fmask)
fmdraw.rounded_rectangle([18, 65, 174, 162], radius=24, fill=255)
# Top bevel highlight & outline
fbdraw.rounded_rectangle([18, 65, 174, 162], radius=24, outline=(30, 64, 175, 220), width=2)
fbdraw.line([34, 67, 158, 67], fill=(191, 219, 254, 160), width=2)

im_files.paste(fbody, (0, 0), fmask)
icon_files = im_files.resize((48, 48), Image.Resampling.LANCZOS)
icon_files.save("build/assets/icons/files.png")

# 3. Settings Gear Icon (48x48)
# Brushed dark titanium metallic squircle with smoothly anti-aliased multi-tooth gear with radial metallic gradient and sunken center hub
im_gear = create_squircle_base(192, 48, top_color=(45, 52, 68), bot_color=(24, 28, 38), border_color=(100, 116, 139, 160))
gdraw = ImageDraw.Draw(im_gear)

cx, cy = 96, 96
# 8 Gear Teeth
for i in range(8):
    ang = i * (math.pi / 4.0)
    tx = cx + 46 * math.cos(ang)
    ty = cy + 46 * math.sin(ang)
    gdraw.rounded_rectangle([tx - 10, ty - 10, tx + 10, ty + 10], radius=4, fill=(203, 213, 225, 255))

# Outer gear ring
gdraw.ellipse([cx - 42, cy - 42, cx + 42, cy + 42], fill=(226, 232, 240, 255), outline=(148, 163, 184, 255), width=3)
# Sunken inner metallic track
gdraw.ellipse([cx - 30, cy - 30, cx + 30, cy + 30], fill=(30, 36, 48, 255), outline=(15, 18, 24, 255), width=3)
# Central hub
gdraw.ellipse([cx - 16, cy - 16, cx + 16, cy + 16], fill=(148, 163, 184, 255), outline=(226, 232, 240, 255), width=2)
gdraw.ellipse([cx - 7, cy - 7, cx + 7, cy + 7], fill=(15, 18, 24, 255))

icon_gear = im_gear.resize((48, 48), Image.Resampling.LANCZOS)
icon_gear.save("build/assets/icons/settings.png")

# 4. Trash Icon (48x48)
# Translucent dark frosted acrylic cylinder with rounded polished metallic rim, subtle refraction highlights, and frosted body
im_trash = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
tdraw = ImageDraw.Draw(im_trash)

# Shadow
tshadow = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
tsdraw = ImageDraw.Draw(tshadow)
tsdraw.ellipse([46, 148, 146, 172], fill=(0, 0, 0, 75))
tshadow = tshadow.filter(ImageFilter.GaussianBlur(6))
im_trash.paste(tshadow, (0, 0), tshadow)

# Cylinder body
tbody = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
tbdraw = ImageDraw.Draw(tbody)
tbdraw.polygon([(48, 42), (144, 42), (134, 156), (58, 156)], fill=(32, 40, 58, 210), outline=(71, 85, 115, 180))
tbdraw.ellipse([58, 146, 134, 166], fill=(32, 40, 58, 220), outline=(71, 85, 115, 180))

# Metallic vertical ribs with highlights
for rx in (74, 96, 118):
    bot_x = rx - int((rx - 96) * 0.1)
    tbdraw.line([rx, 48, bot_x, 152], fill=(148, 163, 184, 170), width=4)
    tbdraw.line([rx - 1, 48, bot_x - 1, 152], fill=(241, 245, 249, 120), width=2)

im_trash.paste(tbody, (0, 0), tbody)

# Top metallic rim
tdraw.ellipse([42, 30, 150, 52], fill=(148, 163, 184, 255), outline=(226, 232, 240, 255), width=3)
tdraw.ellipse([54, 35, 138, 47], fill=(20, 24, 34, 255))
tdraw.arc([46, 32, 146, 50], start=180, end=360, fill=(255, 255, 255, 180), width=2)

icon_trash = im_trash.resize((48, 48), Image.Resampling.LANCZOS)
icon_trash.save("build/assets/icons/trash.png")

# 5. Wi-Fi Glyph (24x24)
# Smoothly curved anti-aliased emerald green Wi-Fi waves with rounded cap styling
im_wifi = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
wdraw = ImageDraw.Draw(im_wifi)
wdraw.ellipse([42, 66, 54, 78], fill=(52, 211, 153, 255))
wdraw.arc([28, 46, 68, 86], start=215, end=325, fill=(52, 211, 153, 255), width=8)
wdraw.arc([14, 26, 82, 94], start=220, end=320, fill=(52, 211, 153, 255), width=8)
icon_wifi = im_wifi.resize((24, 24), Image.Resampling.LANCZOS)
icon_wifi.save("build/assets/icons/wifi.png")

print("Generated high-fidelity 48x48 squircle PNG icons!")

# 6. Ultra-Smooth 1280x800 Silk Gradient Wallpaper (No Sawtooth / No Jagged Curves)
# Deep midnight obsidian radial/bilinear gradient (#06080e -> #121626 -> #1b2034)
img_wall = Image.new("RGB", (1280, 800))
wdraw = ImageDraw.Draw(img_wall)

# Bilinear base gradient
for y in range(800):
    ty = y / 799.0
    # Hermite smoothstep
    sy = ty * ty * (3.0 - 2.0 * ty)
    r = int(6.0 * (1.0 - sy) + 27.0 * sy)
    g = int(8.0 * (1.0 - sy) + 32.0 * sy)
    b = int(14.0 * (1.0 - sy) + 52.0 * sy)
    wdraw.line([(0, y), (1279, y)], fill=(r, g, b))

# Add smooth radial ambient indigo glow centered at (640, 320)
glow = Image.new("RGBA", (1280, 800), (0, 0, 0, 0))
gdraw = ImageDraw.Draw(glow)
gcx, gcy = 640, 300
max_rad = 600.0
for rad in range(600, 0, -20):
    ratio = rad / max_rad
    alpha = int((1.0 - ratio) * 75)
    gdraw.ellipse([gcx - rad, gcy - int(rad * 0.65), gcx + rad, gcy + int(rad * 0.65)], fill=(18, 24, 46, alpha))

glow = glow.filter(ImageFilter.GaussianBlur(32))
img_wall.paste(glow, (0, 0), glow)
img_wall.save("build/assets/backgrounds/onyx-default.png")
print("Saved 1280x800 Silk Monterey Gradient Wallpaper!")

# 7. Generate userspace/bin/lumen/dock_icons.h with 48x48 RGBA arrays
with open("userspace/bin/lumen/dock_icons.h", "w") as f:
    f.write("/* Auto-generated 48x48 32-bit RGBA icon buffers for Lumen Compositor */\n")
    f.write("#ifndef LUMEN_DOCK_ICONS_H\n#define LUMEN_DOCK_ICONS_H\n#include <stdint.h>\n\n")

    def dump_rgba(name, im):
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

    dump_rgba("terminal", icon_term)
    dump_rgba("files", icon_files)
    dump_rgba("settings", icon_gear)
    dump_rgba("trash", icon_trash)
    dump_rgba("wifi", icon_wifi)
    f.write("#endif\n")

print("Generated userspace/bin/lumen/dock_icons.h with genuine 48x48 32-bit RGBA buffers!")
