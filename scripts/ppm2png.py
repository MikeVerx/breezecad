#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Converts a binary PPM (P6, what BZ_SCREENSHOT writes) to PNG without
third-party packages, so CI can show the start check's frame as a picture.

Usage: ppm2png.py in.ppm out.png [--crop X,Y,W,H] [--half] [--annotate TITLE]
  --crop      keep that rectangle (pixels of the frame)
  --half      halve the size (2x2 averaging)
  --annotate  also print the PNG as base64 in GitHub notices, a few lines
              of 60 000 characters: logs and artifacts cannot always be
              downloaded, annotations can (gh api …/check-runs/<id>/annotations)
"""
import argparse
import base64
import struct
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise SystemExit("not an 8-bit P6 PPM")
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[pos + 1:pos + 1 + width * height * 3]


def crop(width, height, rgb, x, y, w, h):
    x, y = max(0, x), max(0, y)
    w, h = min(w, width - x), min(h, height - y)
    rows = [rgb[((y + r) * width + x) * 3:((y + r) * width + x + w) * 3] for r in range(h)]
    return w, h, b"".join(rows)


def half(width, height, rgb):
    w, h = width // 2, height // 2
    out = bytearray(w * h * 3)
    for y in range(h):
        a, b = (2 * y) * width * 3, (2 * y + 1) * width * 3
        for x in range(w):
            for c in range(3):
                i = 6 * x + c
                out[(y * w + x) * 3 + c] = (rgb[a + i] + rgb[a + i + 3] + rgb[b + i] + rgb[b + i + 3]) // 4
    return w, h, bytes(out)


def png_bytes(width, height, rgb):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    return png + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("ppm")
    ap.add_argument("png")
    ap.add_argument("--crop")
    ap.add_argument("--half", action="store_true")
    ap.add_argument("--annotate")
    args = ap.parse_args()
    w, h, pixels = read_ppm(args.ppm)
    if args.crop:
        w, h, pixels = crop(w, h, pixels, *[int(v) for v in args.crop.split(",")])
    if args.half:
        w, h, pixels = half(w, h, pixels)
    png = png_bytes(w, h, pixels)
    open(args.png, "wb").write(png)
    print(f"{args.png}: {w}x{h}, {len(png)} bytes")
    if args.annotate:
        text = base64.b64encode(png).decode()
        parts = [text[i:i + 60000] for i in range(0, len(text), 60000)]
        for n, part in enumerate(parts, 1):
            print(f"::notice title={args.annotate} png {n}/{len(parts)}::{part}")
