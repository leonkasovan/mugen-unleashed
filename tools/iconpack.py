"""Turn the icons in res/icons/*.ico into what each build step needs.

Usage:
  python tools/iconpack.py ico OUT.ico IN.ico [IN.ico ...]
      Merge the input icons into one multi-size .ico. Windows picks the size
      that matches the DPI and the requested context (16 for the title bar,
      32/48 for the task bar and Alt+Tab, 64 and up for large views), so a
      single-size icon looks scaled-up on most of them. Entries are copied
      verbatim; when two inputs carry the same size, the higher bit depth wins.

  python tools/iconpack.py png OUT.png IN.ico [SIZE] [--indexed] [--flatten [auto|#rrggbb]]
      Write the largest image of IN.ico as a PNG, scaled to SIZE x SIZE
      (default 128, the size the PS Vita wants for sce_sys/icon0.png).

  python tools/iconpack.py canvas OUT.png IN.ico W H [SCALE] [BG] [--indexed] [--flatten [auto|#rrggbb]]
      Compose the artwork onto a W x H canvas, so that it also covers the places
      that want more than a square icon: the Android adaptive-icon foreground
      (108dp canvas, artwork confined to the safe zone) and the PS Vita LiveArea
      artwork (840x500 background, 280x158 start-up image).

      SCALE is the artwork's edge as a percentage of the canvas *height*
      (default 100). BG is `none` for transparent (the default), a `#rrggbb`
      colour, or `auto` for the average colour of the artwork's own opaque
      pixels -- derived from the icon rather than invented, which is what keeps
      a generated LiveArea background recognisably the same artwork.

      --indexed writes an 8-bit palette PNG (colour type 3) instead of RGBA.
      The Vita's package validation requires that format for sce_sys/ artwork:
      RGBA assets are accepted by the packer and then rejected by VitaShell on
      the console with 0x8010113D, which is why port/psvita/cmake/Icon.cmake
      asks for it and its guard fails the configure if a file is not indexed.
      Neither subcommand writes an interlaced PNG.

      --flatten composites the result onto an opaque colour first (`auto` is the
      artwork's average colour), dropping the alpha channel. sce_sys/icon0.png
      and sce_sys/livearea/contents/bg.png must be opaque; the start-up image may
      keep transparency, which --indexed carries as a tRNS chunk.

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

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(_chunk(b"IEND", b""))


def png(src, out, size=128, indexed=False, flatten=None):
    entries = read_ico(src)
    w, h, bits, blob = max(entries, key=lambda e: (e[0] * e[1], e[2]))
    iw, ih, pixels = decode_dib(blob)
    pixels = resize(pixels, iw, ih, size, size)
    emit(out, pixels, size, size, indexed, flatten)
    return w, h, bits


# --------------------------------------------------------------- indexed PNG --
#
# The Vita's package validation only accepts 8-bit *palette* PNGs for the
# sce_sys/ artwork; an RGBA one packs fine and is then refused on the console
# with 0x8010113D. Every VitaSDK sample ships colour type 3 (see
# $VITASDK/share/gcc-arm-vita-eabi/samples/*/sce_sys), which is the reference
# this implementation follows. Python's standard library has no quantiser, so
# the median cut below is the whole of it -- the artwork here is a logo on a
# flat field, so 256 colours is generous, and the alternative (an imaging
# package) is a dependency this project does not take.

def _chunk(kind, body):
    return (struct.pack(">I", len(body)) + kind + body +
            struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))


def flatten(pixels, background):
    """Composite away the alpha channel, over `auto` or a `#rrggbb` colour."""
    if background in (None, "none"):
        return pixels
    if background == "auto":
        fill = average_colour(pixels) + (255,)
    elif background.startswith("#") and len(background) == 7:
        fill = (int(background[1:3], 16), int(background[3:5], 16),
                int(background[5:7], 16), 255)
    else:
        raise ValueError("flatten colour must be auto or #rrggbb, not %r" % background)
    return [[over(fill, px) for px in row] for row in pixels]


def _box_spans(box):
    return [max(c[k] for c, _ in box) - min(c[k] for c, _ in box) for k in range(3)]


def _box_average(box):
    total = sum(n for _, n in box)
    if total <= 0:
        return box[0][0]
    return tuple(max(0, min(255, int(sum(c[k] * n for c, n in box) / total + 0.5)))
                 for k in range(3))


def quantise(histogram, limit):
    """Median-cut `histogram` ({rgb: count}) down to at most `limit` colours.

    Returns a list of (rgb, [(rgb, count)]) boxes. Deterministic: boxes are
    ordered by the palette index they become, ties are broken by the full colour
    tuple, so the same input always produces the same PNG.
    """
    boxes = [sorted(histogram.items())]
    while len(boxes) < limit:
        target, best = -1, -1
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            # Widest usable box wins; multiplying by its population keeps the
            # split where most of the pixels are, not where one stray pixel is.
            score = max(_box_spans(box)) * len(box)
            if score > best:
                target, best = i, score
        if target < 0:
            break
        box = boxes.pop(target)
        spans = _box_spans(box)
        channel = spans.index(max(spans))
        box.sort(key=lambda item: (item[0][channel], item[0]))
        total = sum(n for _, n in box)
        acc, cut = 0, 1
        for j, (_, count) in enumerate(box):
            acc += count
            if acc * 2 >= total:
                cut = min(max(j + 1, 1), len(box) - 1)
                break
        boxes.append(box[:cut])
        boxes.append(box[cut:])
    return [(box[0][0] if len(box) == 1 else _box_average(box), box) for box in boxes]


def write_png_indexed(path, pixels, w, h):
    """Write an 8-bit palette PNG (colour type 3), alpha as a tRNS index."""
    clear = None
    for y in range(h):
        for x in range(w):
            if pixels[y][x][3] < 128:
                clear = (y, x)
                break
        if clear is not None:
            break
    has_alpha = clear is not None

    histogram = {}
    for row in pixels:
        for r, g, b, a in row:
            if a < 128:
                continue
            histogram[(r, g, b)] = histogram.get((r, g, b), 0) + 1

    limit = 255 if has_alpha else 256
    palette, mapping = [], {}
    if has_alpha:
        # Index 0 is the transparent entry; its RGB is never shown.
        palette.append((0, 0, 0))
    for colour, box in quantise(histogram, max(1, limit)) if histogram else []:
        index = len(palette)
        palette.append(colour)
        for member, _count in box:
            mapping[member] = index
    if not palette:
        palette.append((0, 0, 0))

    raw = bytearray()
    for y in range(h):
        raw.append(0)                          # filter: none
        for x in range(w):
            r, g, b, a = pixels[y][x]
            if a < 128:
                raw.append(0)
            else:
                raw.append(mapping[(r, g, b)])

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)))
        f.write(_chunk(b"PLTE", bytes(c for rgb in palette for c in rgb)))
        if has_alpha:
            f.write(_chunk(b"tRNS", b"\x00"))
        f.write(_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(_chunk(b"IEND", b""))
    return len(palette), has_alpha


def emit(out, pixels, w, h, indexed, background):
    """Write `pixels` as RGBA, or as an indexed PNG after --flatten."""
    pixels = flatten(pixels, background)
    if not indexed:
        write_png(out, pixels, w, h)
        return None
    return write_png_indexed(out, pixels, w, h)


def average_colour(pixels):
    """Alpha-weighted mean of the artwork, rounded to #rrggbb.

    Weighting by alpha keeps the transparent surround of a bubble icon from
    dragging the result towards black; the answer is the colour the artwork
    actually looks like.
    """
    total = 0.0
    acc = [0.0, 0.0, 0.0]
    for row in pixels:
        for r, g, b, a in row:
            if a == 0:
                continue
            total += a
            acc[0] += r * a
            acc[1] += g * a
            acc[2] += b * a
    if total <= 0.0:
        return (0, 0, 0)
    return (int(acc[0] / total + 0.5), int(acc[1] / total + 0.5), int(acc[2] / total + 0.5))


def over(dst, src):
    """Source-over compositing of two RGBA tuples."""
    sa = src[3] / 255.0
    if sa <= 0.0:
        return dst
    da = dst[3] / 255.0
    oa = sa + da * (1.0 - sa)
    if oa <= 0.0:
        return (0, 0, 0, 0)
    out = []
    for c in range(3):
        out.append(max(0, min(255, int((src[c] * sa + dst[c] * da * (1.0 - sa)) / oa + 0.5))))
    out.append(max(0, min(255, int(oa * 255 + 0.5))))
    return tuple(out)


def canvas(src, out, w, h, scale=100, background="none", indexed=False, flatten_to=None):
    """Compose the artwork, centred, onto a w x h canvas.

    `scale` is the artwork's edge as a percentage of the canvas height. The
    artwork is scaled to a square, never distorted: a 280x158 start-up image
    therefore gets a square picture with the spare width left as background.
    """
    entries = read_ico(src)
    ew, eh, bits, blob = max(entries, key=lambda e: (e[0] * e[1], e[2]))
    iw, ih, pixels = decode_dib(blob)

    if background == "auto":
        fill = average_colour(pixels) + (255,)
    elif background == "none":
        fill = (0, 0, 0, 0)
    elif background.startswith("#") and len(background) == 7:
        fill = (int(background[1:3], 16), int(background[3:5], 16),
                int(background[5:7], 16), 255)
    else:
        raise ValueError("background must be none, auto or #rrggbb, not %r" % background)

    edge = max(1, int(round(h * scale / 100.0)))
    art = resize(pixels, iw, ih, edge, edge)
    ox = (w - edge) // 2
    oy = (h - edge) // 2

    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            px = fill
            if 0 <= x - ox < edge and 0 <= y - oy < edge:
                px = over(fill, art[y - oy][x - ox])
            row.append(px)
        rows.append(row)
    emit(out, rows, w, h, indexed, flatten_to)
    return ew, eh, bits


def take_options(args):
    """Pull the output-format options out of `args`, wherever they appear.

    Positional arguments keep their documented order, so `--indexed` can be
    written next to the subcommand or after its last argument without changing
    what `[SIZE]` or `[SCALE] [BG]` mean.
    """
    indexed = False
    background = None
    rest = []
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "--indexed":
            indexed = True
        elif arg == "--flatten":
            i += 1
            background = args[i] if i < len(args) else "auto"
        elif arg.startswith("--flatten="):
            background = arg.split("=", 1)[1]
        elif arg in ("--help", "-h"):
            rest.append(arg)
        else:
            rest.append(arg)
        i += 1
    return indexed, background, rest


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cmd = argv[1]
    if cmd == "ico":
        sizes = merge(argv[3:], argv[2])
        print("%s <- %s" % (argv[2], ", ".join("%dx%d" % s for s in sizes)))
        return 0
    indexed, background, rest = take_options(argv[2:])
    if cmd == "png":
        if len(rest) < 2:
            print(__doc__.strip(), file=sys.stderr)
            return 2
        out, src = rest[0], rest[1]
        size = int(rest[2]) if len(rest) > 2 else 128
        w, h, bits = png(src, out, size, indexed, background)
        print("%s <- %s (%dx%d, %d bpp, scaled to %d%s)" %
              (out, src, w, h, bits, size, describe(out, indexed, background)))
        return 0
    if cmd == "canvas":
        if len(rest) < 4:
            print(__doc__.strip(), file=sys.stderr)
            return 2
        out, src = rest[0], rest[1]
        w = int(rest[2])
        h = int(rest[3])
        scale = int(rest[4]) if len(rest) > 4 else 100
        canvas_bg = rest[5] if len(rest) > 5 else "none"
        ew, eh, bits = canvas(src, out, w, h, scale, canvas_bg, indexed, background)
        print("%s <- %s (%dx%d, %d bpp, %d%% of %dx%d, background %s%s)" %
              (out, src, ew, eh, bits, scale, w, h, canvas_bg,
               describe(out, indexed, background)))
        return 0
    print("unknown command %r" % cmd, file=sys.stderr)
    return 2


def describe(path, indexed, background):
    """Say in the log what was written, because that is what gets rejected."""
    if not indexed:
        return ", RGBA"
    note = ", indexed 8-bit"
    if background not in (None, "none"):
        note += ", flattened onto %s" % background
    return note


if __name__ == "__main__":
    sys.exit(main(sys.argv))
