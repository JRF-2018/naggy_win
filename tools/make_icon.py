#!/usr/bin/env python3
# Generates ime/res/naggy.ico (a white "凪" on a rounded blue square).
import sys
from PIL import Image, ImageDraw, ImageFont
FONT = sys.argv[2] if len(sys.argv) > 2 else "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc"
def make(size):
    s = size * 4
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=s // 5, fill=(0x2B, 0x4C, 0x9A, 255))
    f = ImageFont.truetype(FONT, int(s * 0.78), index=0)
    t = "\u51EA"  # 凪
    b = d.textbbox((0, 0), t, font=f)
    d.text(((s - (b[2] - b[0])) / 2 - b[0], (s - (b[3] - b[1])) / 2 - b[1]), t, font=f, fill="white")
    return im.resize((size, size), Image.LANCZOS)
imgs = [make(n) for n in (16, 20, 24, 32, 48, 64, 256)]
imgs[-1].save(sys.argv[1], sizes=[(i.width, i.height) for i in imgs], append_images=imgs[:-1])
