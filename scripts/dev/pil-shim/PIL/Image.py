"""Tiny stand-in for Pillow (sandbox without PyPI): open a binary PPM (P6), save PNG, crop."""
import struct, zlib

class _Img:
    def __init__(self, w, h, rgb):
        self.size = (w, h); self.width = w; self.height = h; self._rgb = rgb
    def crop(self, box):
        x0, y0, x1, y1 = box; w = self.width
        rows = [self._rgb[(y * w + x0) * 3:(y * w + x1) * 3] for y in range(y0, y1)]
        return _Img(x1 - x0, y1 - y0, b"".join(rows))
    def save(self, path):
        w, h = self.size
        raw = b"".join(b"\x00" + self._rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
        def chunk(t, data):
            c = struct.pack(">I", len(data)) + t + data
            return c + struct.pack(">I", zlib.crc32(t + data) & 0xffffffff)
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
        import builtins
        builtins.open(path, "wb").write(png)

def open(path):
    import builtins
    data = builtins.open(path, "rb").read()
    tokens, pos = [], 0
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace(): pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""): pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace(): pos += 1
        tokens.append(data[start:pos])
    assert tokens[0] == b"P6", "only binary PPM"
    w, h = int(tokens[1]), int(tokens[2])
    return _Img(w, h, data[pos + 1:pos + 1 + w * h * 3])
