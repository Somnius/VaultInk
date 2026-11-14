#!/usr/bin/env python3
"""Writes fixtures.inc for PngSlowDecoderTest: small PNGs encoded here by hand (Adam7 passes, every
filter type, split IDAT chunks) with the gray value each pixel must decode to. Pillow, when it is
installed, opens every file as a check that the fixtures are real PNGs.

    python3 test/png_slow_decoder/make_fixtures.py
"""

import io
import os
import random
import struct
import zlib

ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def chunk(name, data):
    return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data) & 0xFFFFFFFF)


def pack_row(samples, depth):
    """samples: flat list of channel values for one scanline."""
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    if depth == 8:
        return bytes(samples)
    out = bytearray((len(samples) * depth + 7) // 8)
    for i, s in enumerate(samples):
        bit = i * depth
        out[bit >> 3] |= s << (8 - depth - (bit & 7))
    return bytes(out)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_row(kind, row, prev, bpp):
    out = bytearray([kind])
    for i, x in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        pred = [0, a, b, (a + b) >> 1, paeth(a, b, c)][kind]
        out.append((x - pred) & 0xFF)
    return bytes(out)


def encode(width, height, color, depth, interlace, pixels, palette=None, trns=None, idat_piece=None, extra=False):
    """pixels[y][x] is a tuple of channel samples."""
    channels = CHANNELS[color]
    bpp = max(1, channels * depth // 8)
    raw = bytearray()
    row_number = 0
    for xs, ys, xstep, ystep in ADAM7 if interlace else [(0, 0, 1, 1)]:
        cols = range(xs, width, xstep)
        rows = range(ys, height, ystep)
        if not cols or not rows:
            continue
        prev = bytes((len(cols) * channels * depth + 7) // 8)
        for y in rows:
            samples = [s for x in cols for s in pixels[y][x]]
            row = pack_row(samples, depth)
            raw += filter_row(row_number % 5, row, prev, bpp)
            prev = row
            row_number += 1
    data = zlib.compress(bytes(raw), 9)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, interlace))
    if extra:
        out += chunk(b"tEXt", b"Comment\x00" + b"x" * 3000)  # longer than the decoder's read buffer
    if palette is not None:
        out += chunk(b"PLTE", bytes(v for entry in palette for v in entry))
    if trns is not None:
        out += chunk(b"tRNS", trns)
    if idat_piece:
        out += chunk(b"IDAT", b"")
        for i in range(0, len(data), idat_piece):
            out += chunk(b"IDAT", data[i : i + idat_piece])
    else:
        out += chunk(b"IDAT", data)
    return out + chunk(b"IEND", b"")


def luma(r, g, b):
    return (r * 77 + g * 150 + b * 29) >> 8


def over_white(gray, alpha):
    return (gray * alpha + 255 * (255 - alpha)) // 255


def expected_gray(color, depth, px, palette, trns):
    hi = [s >> 8 for s in px] if depth == 16 else list(px)
    if color == 0:
        if trns is not None and px[0] == struct.unpack(">H", trns[:2])[0]:
            return 255
        return hi[0] if depth >= 8 else px[0] * 255 // ((1 << depth) - 1)
    if color == 2:
        if trns is not None and tuple(px) == struct.unpack(">HHH", trns[:6]):
            return 255
        return luma(*hi)
    if color == 3:
        alpha = trns[px[0]] if trns is not None and px[0] < len(trns) else 255
        return over_white(luma(*palette[px[0]]), alpha)
    if color == 4:
        return over_white(hi[0], hi[1])
    return over_white(luma(*hi[:3]), hi[3])


def make(name, width, height, color, depth, interlace, rng, **kwargs):
    top = (1 << depth) - 1
    palette = trns = None
    if color == 3:
        palette = [tuple(rng.randrange(256) for _ in range(3)) for _ in range(1 << depth)]
        trns = bytes(rng.randrange(256) for _ in range((1 << depth) - 1))  # the last entry stays opaque
    pixels = [[tuple(rng.randrange(top + 1) for _ in range(CHANNELS[color])) for _ in range(width)] for _ in range(height)]
    if kwargs.pop("color_key", False):
        key = pixels[0][0]
        pixels[height - 1][width - 1] = key
        trns = b"".join(struct.pack(">H", s) for s in key)
    png = encode(width, height, color, depth, interlace, pixels, palette, trns, **kwargs)
    gray = [expected_gray(color, depth, pixels[y][x], palette, trns) for y in range(height) for x in range(width)]
    try:
        from PIL import Image

        image = Image.open(io.BytesIO(png))
        image.load()
        assert image.size == (width, height), name
    except ImportError:
        pass
    return name, width, height, color, depth, interlace, png, gray


def c_array(values):
    lines = []
    for i in range(0, len(values), 20):
        lines.append("    " + ", ".join(str(v) for v in values[i : i + 20]) + ",")
    return "\n".join(lines)


def main():
    rng = random.Random(20261114)
    cases = [
        make("Rgb8Interlaced", 13, 11, 2, 8, 1, rng),
        make("Rgba16Interlaced", 13, 11, 6, 16, 1, rng),
        make("Gray16", 13, 11, 0, 16, 0, rng),
        make("Rgb16SplitIdat", 9, 10, 2, 16, 0, rng, idat_piece=7, extra=True),
        make("Indexed2Interlaced", 13, 11, 3, 2, 1, rng),
        make("Indexed8Interlaced", 21, 5, 3, 8, 1, rng),
        make("GrayAlpha8Interlaced", 10, 17, 4, 8, 1, rng),
        make("GrayAlpha16Interlaced", 7, 9, 4, 16, 1, rng, idat_piece=5),
        make("Gray1Interlaced", 19, 9, 0, 1, 1, rng),
        make("Gray4Interlaced", 6, 6, 0, 4, 1, rng),
        make("Gray8KeyInterlaced", 8, 8, 0, 8, 1, rng, color_key=True),
        make("Rgb16KeyInterlaced", 8, 8, 2, 16, 1, rng, color_key=True),
        make("OnePixelInterlaced", 1, 1, 6, 8, 1, rng),
        make("ThreeByTwoInterlaced", 3, 2, 2, 8, 1, rng),  # several Adam7 passes hold no pixels
        make("TwoByFiveInterlaced", 2, 5, 0, 16, 1, rng),
        make("Rgba8", 12, 7, 6, 8, 0, rng),
    ]
    out = ["// Written by make_fixtures.py: do not edit.", ""]
    for name, width, height, color, depth, interlace, png, gray in cases:
        out.append(f"const uint8_t k{name}Png[] = {{\n{c_array(png)}\n}};")
        out.append(f"const uint8_t k{name}Gray[] = {{\n{c_array(gray)}\n}};")
    out.append("")
    out.append("const Fixture kFixtures[] = {")
    for name, width, height, color, depth, interlace, png, gray in cases:
        out.append(
            f'    {{"{name}", {width}, {height}, {color}, {depth}, {interlace}, k{name}Png, sizeof(k{name}Png), k{name}Gray}},'
        )
    out.append("};")
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures.inc")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"{path}: {len(cases)} pictures")


if __name__ == "__main__":
    main()
