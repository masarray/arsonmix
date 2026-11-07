#!/usr/bin/env python3
"""Deterministic ArSonMix brand icon generator. Python stdlib only."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"

def chunk(kind, content):
    return (struct.pack(">I", len(content)) + kind + content
            + struct.pack(">I", zlib.crc32(kind + content) & 0xffffffff))

def read_png(source):
    raw = source.read_bytes()
    if not raw.startswith(SIGNATURE):
        raise ValueError("Brand source must be a PNG")
    pos = 8
    parts, palette, transparency = [], b"", b""
    width = height = bpp = None
    while pos < len(raw):
        length = struct.unpack_from(">I", raw, pos)[0]
        kind = raw[pos + 4:pos + 8]
        content = raw[pos + 8:pos + 8 + length]
        crc = struct.unpack_from(">I", raw, pos + 8 + length)[0]
        if zlib.crc32(kind + content) & 0xffffffff != crc:
            raise ValueError("Brand PNG CRC failure")
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, mode, compression, filtering, interlace = struct.unpack(">IIBBBBB", content)
            if depth != 8 or mode not in (2, 3, 6) or compression or filtering or interlace:
                raise ValueError("Unsupported brand PNG encoding")
            bpp = {2: 3, 3: 1, 6: 4}[mode]
        elif kind == b"PLTE":
            palette = content
        elif kind == b"tRNS":
            transparency = content
        elif kind == b"IDAT":
            parts.append(content)
        elif kind == b"IEND":
            break
    if width is None or width != height or not 16 <= width <= 2048:
        raise ValueError("Brand PNG must be a supported square image")
    scan = zlib.decompress(b"".join(parts))
    stride = width * bpp
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filtering = scan[offset]
        row = bytearray(scan[offset + 1:offset + 1 + stride])
        if len(row) != stride:
            raise ValueError("Truncated brand PNG")
        offset += stride + 1
        if filtering not in range(5):
            raise ValueError("Unsupported PNG filter")
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0
            b = previous[i]
            c = previous[i - bpp] if i >= bpp else 0
            if filtering == 1:
                predictor = a
            elif filtering == 2:
                predictor = b
            elif filtering == 3:
                predictor = (a + b) // 2
            elif filtering == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else b if pb <= pc else c
            else:
                predictor = 0
            row[i] = (row[i] + predictor) & 255
        previous = row
        if mode == 3:
            if not palette:
                raise ValueError("Indexed PNG requires palette")
            rgba = bytearray()
            for index in row:
                rgba += palette[index * 3:index * 3 + 3]
                rgba.append(transparency[index] if index < len(transparency) else 255)
            rows.append(bytes(rgba))
        elif mode == 2:
            rgba = bytearray()
            for i in range(0, len(row), 3):
                rgba += row[i:i + 3] + b"\xff"
            rows.append(bytes(rgba))
        else:
            rows.append(bytes(row))
    return width, rows

def weights(length, n, at):
    scale = length / n
    if scale >= 1:
        left, right = at * scale, (at + 1) * scale
        return [(i, (min(right, i + 1) - max(left, i)) / scale)
                for i in range(math.floor(left), math.ceil(right))
                if 0 <= i < length and min(right, i + 1) > max(left, i)]
    position = (at + .5) * scale - .5
    low = math.floor(position)
    pairs = [(min(max(low, 0), length - 1), 1 - (position - low)),
             (min(max(low + 1, 0), length - 1), position - low)]
    combined = {}
    for i, w in pairs:
        combined[i] = combined.get(i, 0) + w
    return list(combined.items())

def resize(rows, n):
    source_size = len(rows)
    xweights = [weights(source_size, n, x) for x in range(n)]
    yweights = [weights(source_size, n, y) for y in range(n)]
    result = []
    for yw in yweights:
        out = bytearray(n * 4)
        for x, xw in enumerate(xweights):
            alpha = red = green = blue = 0.0
            for y, wy in yw:
                row = rows[y]
                for xx, wx in xw:
                    i = xx * 4
                    a = row[i + 3] * wx * wy
                    alpha += a
                    red += row[i] * a
                    green += row[i + 1] * a
                    blue += row[i + 2] * a
            i = x * 4
            if alpha > .00001:
                out[i:i + 4] = bytes((round(red / alpha), round(green / alpha),
                                     round(blue / alpha), round(alpha)))
        result.append(bytes(out))
    return result

def encode_png(rows):
    n = len(rows)
    ihdr = struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0)
    stream = b"".join(b"\0" + row for row in rows)
    return SIGNATURE + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(stream, 9)) + chunk(b"IEND", b"")

def make_ico(pngs):
    # PNG-in-ICO preserves alpha, unlike legacy AND-mask icon frames.
    sizes = (16, 24, 32, 48, 64, 128, 256)
    offset = 6 + 16 * len(sizes)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(sizes)))
    payload = bytearray()
    for size in sizes:
        data = pngs[size]
        directory += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        payload += data
    return bytes(directory + payload)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("assets/icon/brand-source-128.png"))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    size, rows = read_png(args.source)
    if not any(px < 255 for row in rows for px in row[3::4]):
        raise ValueError("Brand source lacks transparent alpha")
    args.out.mkdir(parents=True, exist_ok=True)
    resolutions = (16, 24, 32, 48, 64, 128, 180, 192, 256, 512, 1024)
    pngs = {n: encode_png(resize(rows, n)) for n in resolutions}
    for n in resolutions:
        (args.out / ("arsonmix-%d.png" % n)).write_bytes(pngs[n])
    for alias, n in (("favicon-16x16.png", 16), ("favicon-32x32.png", 32),
                     ("apple-touch-icon.png", 180), ("android-chrome-192x192.png", 192),
                     ("android-chrome-512x512.png", 512)):
        (args.out / alias).write_bytes(pngs[n])
    (args.out / "favicon.ico").write_bytes(make_ico(pngs))
    (args.out / "site.webmanifest").write_text(json.dumps({
        "name": "ArSonMix — Digital Audio Mixer", "short_name": "ArSonMix",
        "start_url": "./", "display": "standalone",
        "background_color": "#092131", "theme_color": "#00d4e7",
        "icons": [{"src": "android-chrome-192x192.png", "sizes": "192x192", "type": "image/png"},
                  {"src": "android-chrome-512x512.png", "sizes": "512x512", "type": "image/png"}]
    }, separators=(",", ":")) + "\n", encoding="utf-8")
    digest = hashlib.sha256(args.source.read_bytes()).hexdigest()
    print("ARSONMIX_BRAND_ICONS_OK source_sha256=" + digest + " outputs=" + str(len(resolutions) + 7))

if __name__ == "__main__":
    main()
