#!/usr/bin/env python3
"""Draws res/app.ico from the palette the CV template itself uses.

No third-party modules: the shapes are rasterised by hand at 8x and boxed down
for antialiasing, PNG comes out of zlib, and the .ico container is written byte
by byte. Run it after changing the palette or the geometry below:

    python tools/make_icon.py

The icon is a page from the dark template - accent bar down the left edge, a
name line, section headings in accent green, body lines in grey. Sizes below
32 px drop to a simplified drawing, because at that scale the real line rhythm
turns into mush.
"""

import binascii
import struct
import zlib
from pathlib import Path

# ------------------------------------------------------------------- palette
# Straight out of Theme::Theme() in src/model.cpp.
RIM = (0x2A, 0x2F, 0x3A)
PAGE = (0x12, 0x16, 0x1C)
HEADING = (0xE8, 0xEC, 0xF2)
SUBTLE = (0x7A, 0x82, 0x92)
BODY = (0x6B, 0x73, 0x83)
ACCENT = (0x4A, 0xDE, 0x80)

SS = 8  # supersampling factor
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
PNG_FROM = 128  # sizes at or above this go in as PNG, smaller ones as DIB


class Canvas:
    """An RGBA framebuffer that only knows how to fill axis-aligned shapes."""

    def __init__(self, size):
        self.size = size
        self.buf = bytearray(size * size * 4)  # transparent black

    def span(self, y, x0, x1, colour):
        """Fills [x0, x1) on row y, clipped to the canvas."""
        x0 = max(0, x0)
        x1 = min(self.size, x1)
        if x1 <= x0 or not 0 <= y < self.size:
            return
        row = y * self.size * 4
        pixel = bytes((colour[0], colour[1], colour[2], 255))
        self.buf[row + x0 * 4:row + x1 * 4] = pixel * (x1 - x0)

    def rect(self, x0, y0, x1, y1, colour):
        for y in range(max(0, int(y0)), min(self.size, int(y1))):
            self.span(y, int(x0), int(x1), colour)

    def round_rect(self, x0, y0, x1, y1, radius, colour, round_left=True,
                   round_right=True):
        """Fills a rectangle whose corners are quarter circles of `radius`."""
        x0, y0, x1, y1 = float(x0), float(y0), float(x1), float(y1)
        r = max(0.0, min(radius, (x1 - x0) / 2, (y1 - y0) / 2))
        for y in range(max(0, int(y0)), min(self.size, int(y1) + 1)):
            cy = y + 0.5
            if cy < y0 or cy > y1:
                continue
            # How far this row pulls in to stay inside the corner arc.
            inset = 0.0
            if r > 0.0 and cy < y0 + r:
                dy = y0 + r - cy
                inset = r - (r * r - dy * dy) ** 0.5
            elif r > 0.0 and cy > y1 - r:
                dy = cy - (y1 - r)
                inset = r - (r * r - dy * dy) ** 0.5
            left = x0 + (inset if round_left else 0.0)
            right = x1 - (inset if round_right else 0.0)
            self.span(y, int(round(left)), int(round(right)), colour)

    def downsample(self, factor):
        """Box-filters by `factor`, dividing colour by the covered samples only
        so that partly covered edge pixels keep their hue instead of going
        muddy towards black."""
        out_size = self.size // factor
        out = bytearray(out_size * out_size * 4)
        src, total = self.buf, factor * factor
        for oy in range(out_size):
            for ox in range(out_size):
                r = g = b = covered = 0
                for sy in range(oy * factor, (oy + 1) * factor):
                    base = (sy * self.size + ox * factor) * 4
                    for i in range(base, base + factor * 4, 4):
                        if src[i + 3]:
                            r += src[i]
                            g += src[i + 1]
                            b += src[i + 2]
                            covered += 1
                o = (oy * out_size + ox) * 4
                if covered:
                    out[o] = r // covered
                    out[o + 1] = g // covered
                    out[o + 2] = b // covered
                    out[o + 3] = covered * 255 // total
        return out, out_size


def draw(size):
    """Renders one icon at `size` px, supersampled and boxed back down."""
    c = Canvas(size * SS)
    s = size * SS  # every coordinate below is a fraction of the canvas

    # The page: an A4-ish portrait rectangle, centred, with a hairline rim so
    # that a nearly black page still has an edge on a dark taskbar.
    px0, px1 = 0.185 * s, 0.815 * s
    py0, py1 = 0.050 * s, 0.950 * s
    radius = 0.055 * s
    rim = max(SS, round(size * 0.018) * SS)  # a hairline, never below a pixel

    c.round_rect(px0, py0, px1, py1, radius, RIM)
    c.round_rect(px0 + rim, py0 + rim, px1 - rim, py1 - rim, radius - rim, PAGE)

    # Accent bar down the left edge, rounded on its outer side only.
    bar = px0 + rim + (px1 - px0) * 0.105
    c.round_rect(px0 + rim, py0 + rim, bar, py1 - rim, radius - rim, ACCENT,
                 round_right=False)

    # The text column, as a stack of bars measured in fractions of the page.
    tx = bar + (px1 - px0) * 0.11
    right = px1 - rim - (px1 - px0) * 0.11
    width = right - tx

    def line(top, height, frac, colour):
        y0 = py0 + top * (py1 - py0)
        y1 = py0 + (top + height) * (py1 - py0)
        c.rect(tx, y0, tx + width * frac, max(y1, y0 + SS), colour)

    if size >= 32:
        line(0.085, 0.075, 1.00, HEADING)  # the name
        line(0.190, 0.045, 0.62, SUBTLE)   # the role under it
        line(0.310, 0.050, 0.55, ACCENT)   # a section heading
        line(0.395, 0.042, 1.00, BODY)
        line(0.462, 0.042, 1.00, BODY)
        line(0.529, 0.042, 0.72, BODY)
        line(0.645, 0.050, 0.45, ACCENT)   # the next section heading
        line(0.730, 0.042, 1.00, BODY)
        line(0.797, 0.042, 0.80, BODY)
    else:
        # Four fat strokes read as "a page with writing on it"; nine do not.
        line(0.110, 0.115, 1.00, HEADING)
        line(0.330, 0.095, 0.70, ACCENT)
        line(0.520, 0.095, 1.00, BODY)
        line(0.710, 0.095, 0.85, BODY)

    return c.downsample(SS)


# ------------------------------------------------------------------ encoding
def png(pixels, size):
    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter: none
        raw += pixels[y * size * 4:(y + 1) * size * 4]
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def dib(pixels, size):
    """A 32-bit BMP the way an .ico wants it: a doubled height in the header,
    bottom-up BGRA rows, then an AND mask that the alpha channel makes
    redundant but the format still requires."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            o = (y * size + x) * 4
            body += bytes((pixels[o + 2], pixels[o + 1], pixels[o],
                           pixels[o + 3]))
    stride = ((size + 31) // 32) * 4
    return header + bytes(body) + bytes(stride * size)


def main():
    images = []
    for size in SIZES:
        pixels, actual = draw(size)
        blob = png(pixels, actual) if size >= PNG_FROM else dib(pixels, actual)
        images.append((size, blob))

    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, blob in images:
        # 256 is written as 0: the field is a single byte.
        out += struct.pack("<BBBBHHII", size & 0xFF, size & 0xFF, 0, 0, 1, 32,
                           len(blob), offset)
        offset += len(blob)
    for _, blob in images:
        out += blob

    path = Path(__file__).resolve().parent.parent / "res" / "app.ico"
    path.write_bytes(bytes(out))
    print(f"res/app.ico: {len(out)} bytes, {len(images)} sizes "
          f"({', '.join(str(s) for s, _ in images)})")


if __name__ == "__main__":
    main()
