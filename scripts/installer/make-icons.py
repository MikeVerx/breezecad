#!/usr/bin/env python3
"""The file-type icons the installer registers (installer/icons/*.ico).

A sheet of paper with a folded corner, the BreezeCAD mark and the file's
extension on a coloured band — Explorer shows them for documents without a
preview and for .bzmod / .bzlib. Drawn here, not by hand, so they stay in
step with the mark (assets/brand/breezecad-mark.svg). Needs Pillow:

    python3 scripts/installer/make-icons.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "installer", "icons")
FONT = os.path.join(ROOT, "assets", "fonts", "IBMPlexSans-SemiBold.ttf")
BLUE = (0x1D, 0x4E, 0xD8, 255)

# Kind -> extension label and band colour.
KINDS = {
    "part": (".bzc", (0x1D, 0x4E, 0xD8, 255)),
    "assembly": (".bza", (0x0F, 0x76, 0x6E, 255)),
    "module": (".bzmod", (0x6D, 0x28, 0xD9, 255)),
    "library": (".bzlib", (0xB4, 0x53, 0x09, 255)),
}


def cut_line():
    curves = [((38, 63), (49, 54), (59, 53), (69, 57.5)), ((69, 57.5), (79, 62), (86, 60), (100, 54))]
    pts = []
    for c in curves:
        for i in range(25):
            t = i / 24
            u = 1 - t
            a, b, d, e = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
            pts.append((a * c[0][0] + b * c[1][0] + d * c[2][0] + e * c[3][0], a * c[0][1] + b * c[1][1] + d * c[2][1] + e * c[3][1]))
    return pts


CUT = cut_line()


def seg_dist(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l = dx * dx + dy * dy
    t = max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l)) if l else 0
    return math.hypot(a[0] + t * dx - p[0], a[1] + t * dy - p[1])


def in_d(p, x0, y0, x1, y1):
    r, cy = (y1 - y0) / 2, (y0 + y1) / 2
    if p[1] < y0 or p[1] > y1 or p[0] < x0:
        return False
    return p[0] <= x1 or (p[0] - x1) ** 2 + (p[1] - cy) ** 2 <= r * r


def in_mark(p):
    x, y = p
    if 22 <= x <= 40 and 14 <= y <= 106:
        return True
    if not (in_d(p, 47, 14, 69, 58) or in_d(p, 47, 58, 73, 106)):
        return False
    return all(seg_dist(p, CUT[i - 1], CUT[i]) > 3.5 for i in range(1, len(CUT)))


def mark_image(size):
    """The mark alone, `size` pixels high, antialiased by 4x supersampling."""
    s = 4
    h = size * s
    w = int(h * 91 / 108)
    img = Image.new("L", (w, h), 0)
    px = img.load()
    for j in range(h):
        for i in range(w):
            if in_mark((i / h * 108 + 14, j / h * 108 + 6)):
                px[i, j] = 255
    return img.resize((w // s, size), Image.LANCZOS)


def icon(kind, size=256):
    ext, band = KINDS[kind]
    s = 4
    S = size * s
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # The sheet: 76 % wide, folded top right corner.
    x0, x1, y0, y1 = int(S * 0.15), int(S * 0.85), int(S * 0.06), int(S * 0.94)
    fold = int(S * 0.2)
    sheet = [(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)]
    shadow = [(x + int(S * 0.012), y + int(S * 0.018)) for x, y in sheet]
    d.polygon(shadow, fill=(0, 0, 0, 40))
    d.polygon(sheet, fill=(255, 255, 255, 255), outline=(0xC4, 0xCA, 0xD3, 255), width=max(2, S // 128))
    d.polygon([(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)], fill=(0xE6, 0xEE, 0xFB, 255),
              outline=(0xC4, 0xCA, 0xD3, 255), width=max(2, S // 128))
    # The band with the extension.
    by0, by1 = int(S * 0.68), int(S * 0.84)
    d.rectangle([x0 - int(S * 0.05), by0, x1 - int(S * 0.08), by1], fill=band)
    try:
        font = ImageFont.truetype(FONT, int((by1 - by0) * 0.66))
        tw = d.textlength(ext, font=font)
        d.text((x0 - int(S * 0.05) + ((x1 - int(S * 0.08)) - (x0 - int(S * 0.05)) - tw) / 2, by0 + (by1 - by0) * 0.08), ext,
               font=font, fill=(255, 255, 255, 255))
    except OSError:
        pass
    img = img.resize((size, size), Image.LANCZOS)
    # The mark above the band.
    m = mark_image(int(size * 0.42))
    colour = Image.new("RGBA", m.size, BLUE)
    img.paste(colour, ((size - m.size[0]) // 2, int(size * 0.17)), m)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    for kind in KINDS:
        big = icon(kind, 256)
        path = os.path.join(OUT, kind + ".ico")
        big.save(path, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
        print(path)


if __name__ == "__main__":
    sys.exit(main())
