"""Turn the icons in res/icons/*.ico into what each build step needs.

Usage:
  python tools/iconpack.py ico OUT.ico IN.ico [IN.ico ...]
      Merge the input icons into one multi-size .ico. Windows picks the size
      that matches the DPI and the requested context (16 for the title bar,
      32/48 for the task bar and Alt+Tab, 64 and up for large views), so a
      single-size icon looks scaled-up on most of them. Entries are copied
      verbatim; when two inputs carry the same size, the higher bit depth wins.

  python tools/iconpack.py png OUT.png IN.ico [SIZE]
      Write the largest image of IN.ico as a PNG, scaled to SIZE x SIZE
      (default 128, the size the PS Vita wants for sce_sys/icon0.png).

Only the standard library is used, so the build needs no imaging package.
The .ico files here are BMP/DIB entries (not PNG-compressed) with an AND mask;
that is what the decoder below handles.
"""
import struct
import sys
import zlib

ICONDIR = struct.Struct("<HHH")          # reserved, type, count
ICONDIRENTRY = struct.Struct("<BBBBHHII")  # w, h, colors, reserved, planes, bpp, size, offset


def read_ico(path):
    """Returns a list of (width, height, bitcount, raw_entry_bytes)."""
    data = open(path, "rb").read()
    reserved, kind, count = ICONDIR.unpack_from(data, 0)
    if reserved != 0 or kind != 1:
        raise ValueError("%s: not an icon file" % path)
    out = []
    for i in range(count):
        w, h, _colors, _res, _planes, bpp, size, offset = ICONDIRENTRY.unpack_from(data, 6 + i * 16)
        w = w or 256
        h = h or 256
        blob = data[offset:offset + size]
        if len(blob) != size:
            raise ValueError("%s: entry %d is truncated" % (path, i))
        if blob[:4] == b"\x89PNG":
            # A PNG-compressed entry carries its own depth; record it as 32 so a
            # bitmap and a PNG of the same size are not mistaken for each other.
            bits = 32
        else:
            bits = struct.unpack_from("<H", blob, 14)[0] if blob[:4] == b"\x28\x00\x00\x00" else 0
        out.append((w, h, bits, blob))
    return out


def write_ico(path, entries):
    entries = sorted(entries, key=lambda e: (-e[0], -e[2]))
    head = ICONDIR.pack(0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    dirs = []
    blobs = []
    for w, h, bits, blob in entries:
        dirs.append(ICONDIRENTRY.pack(w % 256, h % 256, 0, 0, 1, bits if bits < 256 else 32,
                                      len(blob), offset))
        blobs.append(blob)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(head)
        for d in dirs:
            f.write(d)
        for b in blobs:
            f.write(b)


def merge(paths, out):
    best = {}
    for p in paths:
        for w, h, bits, blob in read_ico(p):
            key = (w, h)
            if key not in best or bits > best[key][0]:
                best[key] = (bits, blob)
    if not best:
        raise ValueError("no icons to merge")
    write_ico(out, [(w, h, bits, blob) for (w, h), (bits, blob) in best.items()])
    return sorted(best)


def decode_dib(blob):
    """Decode one BMP/DIB icon entry into (w, h, RGBA rows top-down)."""
    hdr_size, w, h2, planes, bits, comp = struct.unpack_from("<IiiHHI", blob, 0)
    if hdr_size < 40 or comp != 0:
        raise ValueError("unsupported icon image (header %d, compression %d)" % (hdr_size, comp))
    if planes != 1:
        raise ValueError("unsupported planes %d" % planes)
    h = h2 // 2                      # the height in the header counts image + mask
    if (w, h) == (0, 0) or w > 4096:
        raise ValueError("implausible icon size %dx%d" % (w, h))

    pixels = []
    if bits == 32:
        stride = w * 4
        base = hdr_size
        for y in range(h):
            row = blob[base + (h - 1 - y) * stride: base + (h - y) * stride]
            line = []
            for x in range(w):
                b, g, r, a = row[x * 4:x * 4 + 4]
                line.append((r, g, b, a))
            pixels.append(line)
    elif bits == 24:
        stride = (w * 3 + 3) & ~3
        base = hdr_size
        for y in range(h):
            row = blob[base + (h - 1 - y) * stride: base + (h - y) * stride]
            line = []
            for x in range(w):
                b, g, r = row[x * 3:x * 3 + 3]
                line.append((r, g, b, 255))
            pixels.append(line)
    elif bits in (1, 4, 8):
        palette_count = struct.unpack_from("<I", blob, 32)[0] or (1 << bits)
        pal_off = hdr_size
        palette = []
        for i in range(palette_count):
            b, g, r, _ = blob[pal_off + i * 4: pal_off + i * 4 + 4]
            palette.append((r, g, b))
        stride = ((w * bits + 31) // 32) * 4
        base = pal_off + palette_count * 4
        for y in range(h):
            row = blob[base + (h - 1 - y) * stride: base + (h - y) * stride]
            line = []
            for x in range(w):
                if bits == 8:
                    idx = row[x]
                elif bits == 4:
                    idx = (row[x // 2] >> (4 if x % 2 == 0 else 0)) & 0x0F
                else:
                    idx = (row[x // 8] >> (7 - x % 8)) & 0x01
                r, g, b = palette[idx] if idx < len(palette) else (0, 0, 0)
                line.append((r, g, b, 255))
            pixels.append(line)
    else:
        raise ValueError("unsupported bit depth %d" % bits)

    # The AND mask makes a transparent pixel where the image itself is opaque
    # black (the 8-bit entries rely on it; 32-bit ones usually have alpha).
    if bits != 32:
        stride = ((w + 31) // 32) * 4
        base = hdr_size + (((w * bits + 31) // 32) * 4) * h
        if bits in (1, 4, 8):
            palette_count = struct.unpack_from("<I", blob, 32)[0] or (1 << bits)
            base += palette_count * 4
        for y in range(h):
            row = blob[base + (h - 1 - y) * stride: base + (h - y) * stride]
            if len(row) < stride:
                break
            for x in range(w):
                if (row[x // 8] >> (7 - x % 8)) & 0x01:
                    r, g, b, _ = pixels[y][x]
                    pixels[y][x] = (r, g, b, 0)
    return w, h, pixels


def resize(pixels, sw, sh, dw, dh):
    """Bilinear scale. Exact size is returned untouched."""
    if (sw, sh) == (dw, dh):
        return pixels
    out = []
    for y in range(dh):
        fy = (y + 0.5) * sh / dh - 0.5
        y0 = max(0, min(sh - 1, int(fy)))
        y1 = max(0, min(sh - 1, y0 + 1))
        wy = fy - int(fy)
        if fy < 0:
            wy = 0.0
        line = []
        for x in range(dw):
            fx = (x + 0.5) * sw / dw - 0.5
            x0 = max(0, min(sw - 1, int(fx)))
            x1 = max(0, min(sw - 1, x0 + 1))
            wx = fx - int(fx)
            if fx < 0:
                wx = 0.0
            p00 = pixels[y0][x0]
            p01 = pixels[y0][x1]
            p10 = pixels[y1][x0]
            p11 = pixels[y1][x1]
            # Weight colour by alpha so transparent pixels do not darken the
            # edge of the artwork.
            a = (p00[3] * (1 - wx) * (1 - wy) + p01[3] * wx * (1 - wy) +
                 p10[3] * (1 - wx) * wy + p11[3] * wx * wy)
            if a <= 0.0:
                line.append((0, 0, 0, 0))
                continue
            ch = []
            for c in range(3):
                v = (p00[c] * p00[3] * (1 - wx) * (1 - wy) + p01[c] * p01[3] * wx * (1 - wy) +
                     p10[c] * p10[3] * (1 - wx) * wy + p11[c] * p11[3] * wx * wy)
                ch.append(max(0, min(255, int(v / a + 0.5))))
            line.append((ch[0], ch[1], ch[2], max(0, min(255, int(a + 0.5)))))
        out.append(line)
    return out


def write_png(path, pixels, w, h):
    raw = bytearray()
    for y in range(h):
        raw.append(0)                     # filter: none
        for x in range(w):
            r, g, b, a = pixels[y][x]
            raw += bytes((r, g, b, a))

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def png(src, out, size=128):
    entries = read_ico(src)
    w, h, bits, blob = max(entries, key=lambda e: (e[0] * e[1], e[2]))
    iw, ih, pixels = decode_dib(blob)
    pixels = resize(pixels, iw, ih, size, size)
    write_png(out, pixels, size, size)
    return w, h, bits


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cmd = argv[1]
    if cmd == "ico":
        sizes = merge(argv[3:], argv[2])
        print("%s <- %s" % (argv[2], ", ".join("%dx%d" % s for s in sizes)))
        return 0
    if cmd == "png":
        size = int(argv[4]) if len(argv) > 4 else 128
        w, h, bits = png(argv[3], argv[2], size)
        print("%s <- %s (%dx%d, %d bpp, scaled to %d)" % (argv[2], argv[3], w, h, bits, size))
        return 0
    print("unknown command %r" % cmd, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
