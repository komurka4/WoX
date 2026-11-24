#!/usr/bin/env python3
"""Phase161 (frontend, owner-requested): the executable's icons.

Reads the owner's artwork icon/wox_icon.bmp (a 256x256 32-bit Windows 3.x
BMP; never modified) and draws the boss key's calculator icon in code, then
writes, into OUT_DIR:

  wox.ico, calc.ico   multi-size Windows icons (every size a PNG image, which
                      Windows Vista and later read), embedded into the .exe
                      by xeen_icons.rc
  xeen_icons.rc       the resource script (1 = WoX, 2 = calculator)
  xeen_icons.c        ARGB tables (16..64) for the X11 window icon
                      (_NET_WM_ICON); Windows builds load the resource instead

Smaller sizes are area-averaged from the 256x256 source (no detail is
invented). Usage: make_icons.py SOURCE_BMP OUT_DIR. Standard library only.

Phase 333: make_icons.py --single SOURCE_BMP OUT_DIR NAME writes only NAME.ico
(the same sizes) and NAME.rc (resource 1 = that icon): the setup tool's icon
from the owner's icon/setup_icon.bmp.
"""
import os
import struct
import sys
import zlib

ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
TABLE_SIZES = (16, 24, 32, 48, 64)


def read_bmp(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise SystemExit("%s: not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    (hsize, width, height, planes, bpp, compression) = struct.unpack_from("<IiiHHI", data, 14)
    if bpp not in (24, 32) or compression not in (0, 3) or width <= 0 or height == 0:
        raise SystemExit("%s: unsupported BMP (%d bpp, compression %d)" % (path, bpp, compression))
    top_down = height < 0
    height = abs(height)
    stride = (width * bpp // 8 + 3) & ~3
    rgba = []
    alpha_seen = False
    for y in range(height):
        row = y if top_down else height - 1 - y
        base = offset + row * stride
        for x in range(width):
            i = base + x * bpp // 8
            b, g, r = data[i], data[i + 1], data[i + 2]
            a = data[i + 3] if bpp == 32 else 255
            alpha_seen = alpha_seen or a != 0
            rgba.append((r, g, b, a))
    if not alpha_seen:  # an all-zero alpha channel means "no alpha"
        rgba = [(r, g, b, 255) for (r, g, b, _) in rgba]
    return width, height, rgba


def resample(width, height, rgba, size):
    """Area average (alpha-weighted) of a width x height image to size x size."""
    if width == size and height == size:
        return list(rgba)

    def weights(n_in, n_out):
        out = []
        scale = n_in / n_out
        for o in range(n_out):
            lo, hi = o * scale, (o + 1) * scale
            w = []
            i = int(lo)
            while i < hi and i < n_in:
                cover = min(hi, i + 1) - max(lo, i)
                if cover > 0:
                    w.append((i, cover))
                i += 1
            out.append(w)
        return out

    wx, wy = weights(width, size), weights(height, size)
    result = []
    for oy in range(size):
        for ox in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            total = 0.0
            for (iy, cy) in wy[oy]:
                for (ix, cx) in wx[ox]:
                    r, g, b, a = rgba[iy * width + ix]
                    w = cy * cx
                    acc[0] += r * a * w
                    acc[1] += g * a * w
                    acc[2] += b * a * w
                    acc[3] += a * w
                    total += w
            a = acc[3] / total if total else 0
            if acc[3] > 0:
                px = tuple(int(round(acc[k] / acc[3])) for k in range(3))
            else:
                px = (0, 0, 0)
            result.append(px + (int(round(a)),))
    return result


def calculator(size=256):
    """The boss key's calculator icon: a slate body, a pale display and a
    4 x 4 key grid (operators orange, '=' blue). Symbolic, made here."""
    img = [(0, 0, 0, 0)] * (size * size)

    def rounded(x0, y0, x1, y1, radius, colour):
        for y in range(y0, y1):
            for x in range(x0, x1):
                dx = max(x0 + radius - x - 0.5, 0, x - (x1 - radius) + 0.5)
                dy = max(y0 + radius - y - 0.5, 0, y - (y1 - radius) + 0.5)
                if dx * dx + dy * dy <= radius * radius:
                    img[y * size + x] = colour

    rounded(28, 8, 228, 248, 26, (40, 46, 56, 255))        # outline
    rounded(34, 14, 222, 242, 22, (66, 74, 88, 255))       # body
    rounded(50, 30, 206, 86, 8, (196, 214, 190, 255))      # display
    # a "0" on the display
    rounded(160, 40, 192, 46, 2, (40, 52, 44, 255))
    rounded(160, 70, 192, 76, 2, (40, 52, 44, 255))
    rounded(156, 44, 162, 72, 2, (40, 52, 44, 255))
    rounded(190, 44, 196, 72, 2, (40, 52, 44, 255))
    for row in range(4):
        for col in range(4):
            x0 = 50 + col * 40
            y0 = 102 + row * 36
            colour = (226, 228, 232, 255)
            if col == 3:
                colour = (240, 146, 48, 255)
            if col == 3 and row == 3:
                colour = (70, 130, 220, 255)
            rounded(x0, y0, x0 + 34, y0 + 30, 6, colour)
    return resample(size, size, img, size) if size != 256 else img


def png(size, rgba):
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            raw.extend(rgba[y * size + x])

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def dib(size, rgba):
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0)
    pixels = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            r, g, b, a = rgba[y * size + x]
            pixels.extend((b, g, r, a))
    stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        row = bytearray(stride)
        for x in range(size):
            if rgba[y * size + x][3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask.extend(row)
    return header + bytes(pixels) + bytes(mask)


def ico(images):
    head = struct.pack("<HHH", 0, 1, len(images))
    entries, blobs = b"", b""
    offset = 6 + 16 * len(images)
    for size, rgba in images:
        blob = png(size, rgba)
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        blobs += blob
        offset += len(blob)
    return head + entries + blobs


def c_table(name, images):
    lines = []
    for size, rgba in images:
        lines.append("static const uint32_t %s_%u[%u]={" % (name, size, size * size))
        words = ["0x%08xu" % ((a << 24) | (r << 16) | (g << 8) | b) for (r, g, b, a) in rgba]
        for i in range(0, len(words), 8):
            lines.append("    " + ",".join(words[i:i + 8]) + ",")
        lines.append("};")
    lines.append("static const xeen_icon_image %s[]={%s};" % (
        name, ",".join("{%u,%s_%u}" % (s, name, s) for s, _ in images)))
    return "\n".join(lines)


def write(path, data):
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(path + ".tmp", mode) as f:
        f.write(data)
    os.replace(path + ".tmp", path)


def single(source, out, name):
    """NAME.ico from SOURCE and the resource script NAME.rc naming it as icon 1."""
    os.makedirs(out, exist_ok=True)
    width, height, rgba = read_bmp(source)
    images = [(s, resample(width, height, rgba, s)) for s in ICO_SIZES if s <= max(width, height)]
    path = os.path.join(out, name + ".ico")
    write(path, ico(images))
    write(os.path.join(out, name + ".rc"),
          "/* Generated by src/icon/make_icons.py --single (Phase 333). */\n"
          "1 ICON \"%s\"\n" % path.replace("\\", "/"))


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--single":
        single(sys.argv[2], sys.argv[3], sys.argv[4])
        return
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_icons.py SOURCE_BMP OUT_DIR | --single SOURCE_BMP OUT_DIR NAME")
    source, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    width, height, rgba = read_bmp(source)
    if width != height:
        print("make_icons: %s is %dx%d (not square); the image is scaled to squares" % (source, width, height))
    wox = {s: resample(width, height, rgba, s) for s in sorted(set(ICO_SIZES) | set(TABLE_SIZES)) if s <= max(width, height)}
    calc256 = calculator(256)
    calc = {s: resample(256, 256, calc256, s) for s in sorted(set(ICO_SIZES) | set(TABLE_SIZES))}
    write(os.path.join(out, "wox.ico"), ico([(s, wox[s]) for s in ICO_SIZES if s in wox]))
    write(os.path.join(out, "calc.ico"), ico([(s, calc[s]) for s in ICO_SIZES]))
    write(os.path.join(out, "xeen_icons.rc"),
          "/* Generated by src/icon/make_icons.py (Phase161). */\n"
          "1 ICON \"%s\"\n2 ICON \"%s\"\n" % (os.path.join(out, "wox.ico").replace("\\", "/"),
                                             os.path.join(out, "calc.ico").replace("\\", "/")))
    body = ["/* Generated by src/icon/make_icons.py (Phase161) from %s; do not edit. */" % os.path.basename(source),
            "#include \"xeen/platform.h\"",
            c_table("wox_icons", [(s, wox[s]) for s in TABLE_SIZES if s in wox]),
            c_table("calc_icons", [(s, calc[s]) for s in TABLE_SIZES]),
            "const xeen_icon_image *xeen_platform_icon_images(int which,size_t *count)",
            "{",
            "    if(count)*count=which?sizeof calc_icons/sizeof calc_icons[0]:sizeof wox_icons/sizeof wox_icons[0];",
            "    return which?calc_icons:wox_icons;",
            "}", ""]
    write(os.path.join(out, "xeen_icons.c"), "\n".join(body))


if __name__ == "__main__":
    main()
