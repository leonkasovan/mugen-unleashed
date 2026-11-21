/* Standalone SFF reader and writer. The layouts and PCX/RLE/LZ codecs
 * follow Sff_Load in src/sff.c and the decoders in src/spritecodec.c. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define seek64 _fseeki64
#define tell64 _ftelli64
#else
#include <unistd.h>
#define seek64 fseeko
#define tell64 ftello
#endif

#define V101 UINT32_C(0x01000100)
#define V200 UINT32_C(0x02000000)
#define V201 UINT32_C(0x02000100)
#define NO_PALETTE UINT32_MAX

enum { HEADER_SIZE = 512, V1_NODE = 32, SPRITE_NODE = 28, PALETTE_NODE = 16 };
enum { RAW = 0, RLE8 = 2, RLE5 = 3, LZ5 = 4, PNG8 = 10, PNG24 = 11,
       PNG32 = 12, PCX = 255, TDATA = 1 };

typedef unsigned char Byte;

typedef struct {
    int group, number, x, y;
    unsigned width, height, depth, format, flags;
    uint32_t offset, size, link, root, palette, effective_palette;
} Sprite;

typedef struct {
    unsigned group, number, count;
    uint32_t offset, size, link, root;
    Byte *colors;                   /* Only the root owns its RGBA bytes. */
} Palette;

typedef struct {
    FILE *file;
    uint32_t size, version, compat, sprite_count, palette_count;
    uint32_t v1_groups;
    unsigned v1_palette_type;
    Sprite *sprites;
    Palette *palettes;
    const char *error;
} Sff;

static unsigned le16(const Byte *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static int signed16(const Byte *p)
{
    unsigned n = le16(p);
    return n < 32768 ? (int)n : (int)n - 65536;
}

static uint32_t le32(const Byte *p)
{
    return (uint32_t)le16(p) | (uint32_t)le16(p + 2) << 16;
}

static uint32_t be32(const Byte *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put_le16(Byte *p, unsigned n)
{
    p[0] = (Byte)n;
    p[1] = (Byte)(n >> 8);
}

static void put_le32(Byte *p, uint32_t n)
{
    put_le16(p, n & 65535);
    put_le16(p + 2, n >> 16);
}

static void put_be32(Byte *p, uint32_t n)
{
    p[0] = (Byte)(n >> 24);
    p[1] = (Byte)(n >> 16);
    p[2] = (Byte)(n >> 8);
    p[3] = (Byte)n;
}

static int fail(Sff *sff, const char *error)
{
    sff->error = error;
    return 0;
}

static int fits(uint32_t offset, uint64_t length, uint32_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static int read_at(Sff *sff, uint32_t offset, void *data, size_t length)
{
    if (!fits(offset, length, sff->size))
        return fail(sff, "record extends beyond the end of the file");
    if (seek64(sff->file, offset, SEEK_SET) != 0 || fread(data, 1, length, sff->file) != length)
        return fail(sff, "could not read the requested file data");
    return 1;
}

static unsigned channels(unsigned depth)
{
    return (depth + 7) / 8;
}

static size_t pixel_bytes(const Sprite *s)
{
    uint64_t n = (uint64_t)s->width * s->height * channels(s->depth);
    return n <= INT_MAX ? (size_t)n : 0;
}

static int valid_dimensions(const Sprite *s)
{
    /* Sff2_Load passes the unsigned on-disk dimensions through a short. */
    return s->width <= INT16_MAX && s->height <= INT16_MAX && pixel_bytes(s) != 0;
}

static const Byte png_signature[] = {137, 80, 78, 71, 13, 10, 26, 10};

static uint32_t crc_update(uint32_t crc, const Byte *data, size_t size)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (unsigned i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (unsigned bit = 0; bit < 8; ++bit)
                c = (c >> 1) ^ ((c & 1) ? UINT32_C(0xedb88320) : 0);
            table[i] = c;
        }
    }
    while (size--)
        crc = table[(crc ^ *data++) & 255] ^ (crc >> 8);
    return crc;
}

static uint32_t adler_update(uint32_t adler, const Byte *data, size_t size)
{
    uint32_t a = adler & 65535, b = adler >> 16;
    while (size) {
        /* Limit each sum to 32 bits before reducing it. */
        size_t block = size > 5552 ? 5552 : size;
        size -= block;
        while (block--) {
            a += *data++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return b << 16 | a;
}

/* PNG uses zlib streams with stored, fixed-Huffman, or dynamic-Huffman
 * DEFLATE blocks. The caller supplies the exact expected output length. */
typedef struct {
    const Byte *next, *end;
    unsigned bits, available;
    int exhausted;
} BitStream;

typedef struct {
    unsigned count[16];
    unsigned symbol[288];
} Huffman;

static unsigned take_bits(BitStream *b, unsigned n)
{
    while (b->available < n) {
        if (b->next == b->end) {
            b->exhausted = 1;
            return 0;
        }
        b->bits |= (unsigned)*b->next++ << b->available;
        b->available += 8;
    }
    unsigned result = b->bits & ((1u << n) - 1);
    b->bits >>= n;
    b->available -= n;
    return result;
}

static int huffman_build(Huffman *h, const Byte *length, unsigned size)
{
    unsigned next[16];
    int remaining = 1;
    memset(h->count, 0, sizeof(h->count));
    for (unsigned i = 0; i < size; ++i)
        ++h->count[length[i]];
    h->count[0] = 0;
    next[1] = 0;
    for (unsigned bits = 1; bits < 16; ++bits) {
        remaining = remaining * 2 - (int)h->count[bits];
        if (remaining < 0)
            return 0;
        if (bits < 15)
            next[bits + 1] = next[bits] + h->count[bits];
    }
    for (unsigned i = 0; i < size; ++i) {
        if (length[i])
            h->symbol[next[length[i]]++] = i;
    }
    return 1;
}

static int huffman_symbol(BitStream *b, const Huffman *h)
{
    unsigned value = 0, first = 0, index = 0;
    for (unsigned bits = 1; bits < 16; ++bits) {
        value = (value << 1) | take_bits(b, 1);
        if (b->exhausted)
            break;
        if (value < first + h->count[bits])
            return (int)h->symbol[index + value - first];
        index += h->count[bits];
        first = (first + h->count[bits]) << 1;
    }
    return -1;
}

static int deflate_tables(BitStream *b, unsigned type, Huffman *values, Huffman *distances)
{
    Byte lens[288 + 32];
    unsigned value_count = 288, dist_count = 32;
    if (type == 1) {
        memset(lens, 8, 144);
        memset(lens + 144, 9, 112);
        memset(lens + 256, 7, 24);
        memset(lens + 280, 8, 8);
        memset(lens + 288, 5, 32);
    } else {
        static const Byte order[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        Huffman lengths;
        Byte code_lengths[19] = {0};
        value_count = take_bits(b, 5) + 257;
        dist_count = take_bits(b, 5) + 1;
        unsigned code_count = take_bits(b, 4) + 4;
        if (value_count > 286)
            return 0;
        for (unsigned i = 0; i < code_count; ++i)
            code_lengths[order[i]] = (Byte)take_bits(b, 3);
        if (!huffman_build(&lengths, code_lengths, 19))
            return 0;
        unsigned pos = 0, total = value_count + dist_count;
        while (pos < total) {
            int symbol = huffman_symbol(b, &lengths);
            if (symbol < 0)
                return 0;
            if (symbol < 16) {
                lens[pos++] = (Byte)symbol;
                continue;
            }
            if (symbol == 16 && !pos)
                return 0;
            unsigned count = symbol == 16 ? take_bits(b, 2) + 3 :
                             symbol == 17 ? take_bits(b, 3) + 3 : take_bits(b, 7) + 11;
            if (b->exhausted || count > total - pos)
                return 0;
            memset(lens + pos, symbol == 16 ? lens[pos - 1] : 0, count);
            pos += count;
        }
    }
    return lens[256] && huffman_build(values, lens, value_count) &&
           huffman_build(distances, lens + value_count, dist_count);
}

static int inflate_codes(BitStream *b, unsigned type, Byte *out, size_t size, size_t *pos)
{
    static const unsigned length_base[] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    Huffman values, distances;
    if (!deflate_tables(b, type, &values, &distances))
        return 0;
    for (;;) {
        int code = huffman_symbol(b, &values);
        if (code < 0 || code > 285)
            return 0;
        if (code == 256)
            return 1;
        if (code < 256) {
            if (*pos == size)
                return 0;
            out[(*pos)++] = (Byte)code;
            continue;
        }
        unsigned length_code = (unsigned)code - 257;
        unsigned extra = length_code >= 8 && length_code < 28 ? (length_code - 4) / 4 : 0;
        unsigned length = length_base[length_code] + take_bits(b, extra);
        int distance_code = huffman_symbol(b, &distances);
        if (distance_code < 0 || distance_code >= 30)
            return 0;
        unsigned distance = (unsigned)distance_code + 1;
        if (distance_code >= 4) {
            extra = (unsigned)distance_code / 2 - 1;
            distance = 1 + ((2 + ((unsigned)distance_code & 1)) << extra) + take_bits(b, extra);
        }
        if (b->exhausted || distance > *pos || length > size - *pos)
            return 0;
        while (length--) {
            out[*pos] = out[*pos - distance];
            ++*pos;
        }
    }
}

static int inflate_zlib(const Byte *data, size_t length, Byte *out, size_t size)
{
    if (length < 7 || (data[0] & 15) != 8 || (data[0] >> 4) > 7 ||
        ((unsigned)data[0] * 256 + data[1]) % 31 || (data[1] & 32))
        return 0;
    BitStream b = {data + 2, data + length - 4, 0, 0, 0};
    size_t pos = 0;
    unsigned last;
    do {
        last = take_bits(&b, 1);
        unsigned type = take_bits(&b, 2);
        if (b.exhausted || type == 3)
            return 0;
        if (type == 0) {
            b.bits = b.available = 0;  /* Discard bits up to the next byte. */
            if (b.end - b.next < 4)
                return 0;
            unsigned n = le16(b.next);
            if ((n ^ 65535u) != le16(b.next + 2))
                return 0;
            b.next += 4;
            if (n > (size_t)(b.end - b.next) || n > size - pos)
                return 0;
            memcpy(out + pos, b.next, n);
            b.next += n;
            pos += n;
        } else if (!inflate_codes(&b, type, out, size, &pos)) {
            return 0;
        }
    } while (!last);
    return b.next == b.end && pos == size && adler_update(1, out, size) == be32(b.end);
}

/* Adam7 passes followed by the equivalent single pass for non-interlaced data. */
static const Byte png_passes[][4] = {
    {0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
    {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}, {0, 0, 1, 1}
};

static unsigned pass_extent(unsigned length, unsigned start, unsigned step)
{
    return length <= start ? 0 : (length - start + step - 1) / step;
}

static unsigned paeth(int left, int up, int corner)
{
    int p = left + up - corner;
    int dl = abs(p - left), du = abs(p - up), dc = abs(p - corner);
    return (unsigned)(dl <= du && dl <= dc ? left : du <= dc ? up : corner);
}

static int expand_png_pass(Byte **scan, Byte *out, const Sprite *s, const Byte *pass,
                           unsigned depth, unsigned nchan)
{
    unsigned width = pass_extent(s->width, pass[0], pass[2]);
    unsigned height = pass_extent(s->height, pass[1], pass[3]);
    unsigned stride = (width * depth * nchan + 7) / 8;
    Byte *previous = NULL;
    if (!width)
        return 1;
    for (unsigned y = 0; y < height; ++y) {
        unsigned filter = *(*scan)++;
        Byte *row = *scan;
        if (filter > 4)
            return 0;
        for (unsigned i = 0; filter && i < stride; ++i) {
            unsigned left = i >= nchan ? row[i - nchan] : 0;
            unsigned up = previous ? previous[i] : 0;
            unsigned corner = previous && i >= nchan ? previous[i - nchan] : 0;
            unsigned delta = filter == 1 ? left : filter == 2 ? up : filter == 3 ?
                             (left + up) / 2 : paeth((int)left, (int)up, (int)corner);
            row[i] = (Byte)(row[i] + delta);
        }
        Byte *dst = out + ((size_t)(pass[1] + y * pass[3]) * s->width + pass[0]) * nchan;
        for (unsigned x = 0; x < width; ++x) {
            Byte *pixel = dst + (size_t)x * pass[2] * nchan;
            if (depth == 8) {
                memcpy(pixel, row + x * nchan, nchan);
            } else {
                unsigned bit = x * depth;
                *pixel = (Byte)((row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1));
            }
        }
        *scan += stride;
        previous = row;
    }
    return 1;
}

static int decode_png(const Sprite *s, const Byte *data, size_t size, Byte *out)
{
    if (size < 33 || memcmp(data, png_signature, sizeof(png_signature)) ||
        be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4) ||
        be32(data + 16) != s->width || be32(data + 20) != s->height ||
        data[26] || data[27] || data[28] > 1 ||
        ~crc_update(UINT32_MAX, data + 12, 17) != be32(data + 29))
        return 0;
    unsigned depth = data[24], color = data[25], nchan = channels(s->depth);
    unsigned first = data[28] ? 0 : 7, last = data[28] ? 7 : 8;
    if ((s->format == PNG8 && (color != 3 || (depth != 1 && depth != 2 && depth != 4 && depth != 8))) ||
        (s->format == PNG24 && (color != 2 || depth != 8)) ||
        (s->format == PNG32 && (color != 6 || depth != 8)))
        return 0;

    size_t offset = 33, idat_size = 0, idat_start = 0, idat_end = 0;
    unsigned palette_count = 0;
    int ended = 0, after_idat = 0;
    while (size - offset >= 12) {
        const Byte *type = data + offset + 4;
        const Byte *chunk = type + 4;
        uint32_t n = be32(data + offset);
        if (n > INT_MAX || n > size - offset - 12 ||
            ~crc_update(UINT32_MAX, type, (size_t)n + 4) != be32(chunk + n))
            return 0;
        if (!memcmp(type, "IDAT", 4)) {
            if (after_idat || (color == 3 && !palette_count))
                return 0;
            if (!idat_start)
                idat_start = offset;
            idat_size += n;
            idat_end = offset + 12 + n;
        } else {
            after_idat = idat_start != 0;
            if (!memcmp(type, "IEND", 4)) {
                ended = n == 0;
                break;
            }
            if (!memcmp(type, "PLTE", 4)) {
                if (after_idat || palette_count || !n || n > 768 || n % 3 ||
                    (color == 3 && n / 3 > (1u << depth)))
                    return 0;
                palette_count = n / 3;
            } else if (!(type[0] & 32)) {
                return 0;           /* Unknown critical chunk. */
            }
        }
        offset += 12 + n;
    }
    if (!ended || idat_size < 7)
        return 0;

    uint64_t raw_size = 0;
    for (unsigned i = first; i < last; ++i) {
        const Byte *pass = png_passes[i];
        unsigned w = pass_extent(s->width, pass[0], pass[2]);
        unsigned h = pass_extent(s->height, pass[1], pass[3]);
        if (w)
            raw_size += (uint64_t)h * ((w * nchan * depth + 7) / 8 + 1);
    }
    /* A DEFLATE pair encodes at most 258 bytes with at least two bits. */
    if (raw_size > SIZE_MAX || raw_size > (uint64_t)idat_size * 1032)
        return 0;
    const Byte *compressed = data + idat_start + 8;
    Byte *joined = NULL;
    if (idat_end - idat_start != idat_size + 12) {
        joined = malloc(idat_size);
        if (!joined)
            return 0;
        size_t pos = 0;
        for (size_t i = idat_start; i < idat_end;) {
            uint32_t n = be32(data + i);
            memcpy(joined + pos, data + i + 8, n);
            i += 12 + (size_t)n;
            pos += n;
        }
        compressed = joined;
    }
    Byte *raw = malloc((size_t)raw_size);
    int ok = raw && inflate_zlib(compressed, idat_size, raw, (size_t)raw_size);
    free(joined);
    Byte *scan = raw;
    for (unsigned i = first; ok && i < last; ++i)
        ok = expand_png_pass(&scan, out, s, png_passes[i], depth, nchan);
    free(raw);
    return ok;
}

static int pcx_dimensions(Sprite *sprite, const Byte *pcx)
{
    unsigned left = le16(pcx + 4), top = le16(pcx + 6);
    unsigned right = le16(pcx + 8), bottom = le16(pcx + 10);
    unsigned planes = pcx[65], stride = le16(pcx + 66);
    if (pcx[0] != 10 || pcx[2] != 1 || pcx[3] != 8 || right < left || bottom < top ||
        (planes != 1 && planes != 3) || stride > INT16_MAX)
        return 0;
    sprite->width = right - left + 1;
    sprite->height = bottom - top + 1;
    sprite->depth = planes * 8;
    return stride >= sprite->width && valid_dimensions(sprite);
}

static int decode_pcx(const Sprite *s, const Byte *pcx, size_t size, Byte *out, Byte *colors)
{
    unsigned planes = channels(s->depth), stride = le16(pcx + 66);
    unsigned line = stride * planes;
    size_t pos = 128;
    for (unsigned y = 0; y < s->height; ++y) {
        for (unsigned i = 0; i < line;) {
            if (pos == size)
                return 0;
            unsigned value = pcx[pos++], length = 1;
            if ((value & 192) == 192) {
                length = value & 63;
                if (pos == size || !length || length > line - i)
                    return 0;
                value = pcx[pos++];
            }
            while (length--) {
                unsigned x = i % stride, plane = i / stride;
                ++i;
                if (out && x < s->width)
                    out[((size_t)y * s->width + x) * planes + plane] = (Byte)value;
            }
        }
    }
    if (colors) {
        /* Match the engine's 16-color header fallback for missing trailers. */
        const Byte *rgb = pcx + 16;
        unsigned count = 16;
        memset(colors, 0, 1024);
        if (pos < size) {
            const Byte *marker = memchr(pcx + pos, 12, size - pos);
            if (marker) {
                if (pcx + size - marker < 769)
                    return 0;
                rgb = marker + 1;
                count = 256;
            }
        }
        for (unsigned i = 0; i < count; ++i) {
            memcpy(colors + i * 4, rgb + i * 3, 3);
            colors[i * 4 + 3] = i ? 255 : 0;
        }
    }
    return 1;
}

static int decode_rle(unsigned format, const Byte *data, size_t size, Byte *out, size_t length)
{
    size_t i = 0, pos = 0;
    while (i < size) {
        unsigned value = data[i++], count = 1, extra = 0;
        if (format == RLE8 && (value & 192) == 64) {
            if (i == size)
                return 0;
            count = value & 63;
            value = data[i++];
        } else if (format == RLE5) {
            if (i == size)
                return 0;
            count = value + 1;
            unsigned control = data[i++];
            extra = control & 127;
            if ((control & 128) && i == size)
                return 0;
            value = control & 128 ? data[i++] : 0;
        }
        if (count > length - pos || extra > size - i)
            return 0;
        memset(out + pos, (int)value, count);
        pos += count;
        while (extra--) {
            value = data[i++];
            count = (value >> 5) + 1;
            if (count > length - pos)
                return 0;
            memset(out + pos, (int)(value & 31), count);
            pos += count;
        }
    }
    return pos == length;
}

static int decode_lz5(const Byte *data, size_t size, Byte *out, size_t length)
{
    size_t i = 0, pos = 0;
    unsigned recycled = 0, short_copies = 0;
    while (i < size) {
        unsigned flags = data[i++];
        if (i == size)
            return 0;
        for (unsigned bit = 0; bit < 8 && i < size; ++bit, flags >>= 1) {
            unsigned value = data[i++], count;
            if (!(flags & 1)) {
                count = value >> 5;
                if (!count) {
                    if (i == size)
                        return 0;
                    count = data[i++] + 8;
                }
                if (count > length - pos)
                    return 0;
                memset(out + pos, (int)(value & 31), count);
                pos += count;
                continue;
            }
            unsigned distance;
            count = value & 63;
            if (!count) {
                if (size - i < 2)
                    return 0;
                distance = ((value & 192) << 2) + data[i++] + 1;
                count = data[i++] + 3;
            } else {
                ++count;
                recycled = ((recycled << 2) | (value >> 6)) & 255;
                if (++short_copies == 4) {
                    distance = recycled + 1;
                    short_copies = 0;
                } else {
                    if (i == size)
                        return 0;
                    distance = data[i++] + 1;
                }
            }
            if (distance > pos || count > length - pos)
                return 0;
            while (count--) {
                out[pos] = out[pos - distance];
                ++pos;
            }
        }
    }
    return pos == length;
}

static Byte *read_payload(Sff *sff, uint32_t offset, uint32_t size)
{
    Byte *data = malloc(size);
    if (!data) {
        fail(sff, "out of memory reading a record");
    } else if (!read_at(sff, offset, data, size)) {
        free(data);
        data = NULL;
    }
    return data;
}

static int format_matches(const Sprite *s)
{
    if (!s->depth || (s->depth > 8 && s->depth != 24 && s->depth != 32))
        return 0;
    switch (s->format) {
    case RAW: return 1;
    case RLE8: case RLE5: case LZ5: case PNG8: return s->depth <= 8;
    case PNG24: return s->depth == 24;
    case PNG32: return s->depth == 32;
    default: return 0;
    }
}

/* Raw data can be used directly. Keep the encoded data when the optimizer
 * may want to copy it to the new archive. */
static int load_pixels(Sff *sff, const Sprite *s, Byte **encoded, Byte **pixels)
{
    size_t length = pixel_bytes(s);
    *encoded = read_payload(sff, s->offset, s->size);
    *pixels = NULL;
    if (!*encoded)
        return 0;
    const Byte *data = *encoded;
    if (s->format == RAW) {
        if (s->size < length)
            return fail(sff, "raw sprite is shorter than its dimensions");
        *pixels = *encoded;
        return 1;
    }
    if (s->format != PCX && (s->size < 4 || le32(data) != length ||
        (uint64_t)length > (uint64_t)(s->size - 4) * 1032))
        return fail(sff, "invalid decoded sprite length");
    *pixels = malloc(length);
    if (!*pixels)
        return fail(sff, "out of memory decoding a sprite");
    int ok;
    switch (s->format) {
    case PCX: ok = decode_pcx(s, data, s->size, *pixels, NULL); break;
    case RLE8: case RLE5: ok = decode_rle(s->format, data + 4, s->size - 4, *pixels, length); break;
    case LZ5: ok = decode_lz5(data + 4, s->size - 4, *pixels, length); break;
    default: ok = decode_png(s, data + 4, s->size - 4, *pixels); break;
    }
    return ok || fail(sff, "malformed sprite image data");
}

static void free_pixels(Byte *encoded, Byte *pixels)
{
    if (pixels != encoded)
        free(pixels);
    free(encoded);
}

static int allocate_sprites(Sff *sff)
{
    if (sff->sprite_count) {
        sff->sprites = calloc(sff->sprite_count, sizeof(*sff->sprites));
        if (!sff->sprites)
            return fail(sff, "out of memory allocating sprite records");
    }
    return 1;
}

static int allocate_palettes(Sff *sff, uint32_t count)
{
    if (count) {
        sff->palettes = calloc(count, sizeof(*sff->palettes));
        if (!sff->palettes)
            return fail(sff, "out of memory allocating palette records");
    }
    return 1;
}

static void inherit_image(Sprite *s, const Sprite *source)
{
    s->root = source->root;
    s->width = source->width;
    s->height = source->height;
    s->depth = source->depth;
    s->format = source->format;
    s->effective_palette = source->effective_palette;
}

static int load_v1(Sff *sff, const Byte *header)
{
    uint32_t pos = le32(header + 24), last_palette = NO_PALETTE;
    sff->v1_groups = le32(header + 16);
    sff->sprite_count = le32(header + 20);
    sff->v1_palette_type = header[32];
    if (sff->sprite_count > (sff->size - HEADER_SIZE) / V1_NODE)
        return fail(sff, "invalid SFF v1 sprite count");
    if (!allocate_sprites(sff) || !allocate_palettes(sff, sff->sprite_count))
        return 0;
    for (uint32_t i = 0; i < sff->sprite_count; ++i) {
        Sprite *s = &sff->sprites[i];
        Byte node[V1_NODE];
        if (pos < HEADER_SIZE)
            return fail(sff, "invalid SFF v1 subfile offset");
        if (!read_at(sff, pos, node, sizeof(node)))
            return 0;
        uint32_t next = le32(node), limit = next ? next : sff->size;
        if (limit < pos + V1_NODE || limit > sff->size || (!next && i + 1 < sff->sprite_count))
            return fail(sff, "invalid SFF v1 subfile chain");
        s->size = le32(node + 4);
        s->x = signed16(node + 8);
        s->y = signed16(node + 10);
        s->group = signed16(node + 12);
        s->number = signed16(node + 14);
        s->link = le16(node + 16);
        s->effective_palette = NO_PALETTE;
        if (!s->size) {
            if (s->link >= i || s->link > INT16_MAX)
                return fail(sff, "invalid SFF v1 sprite link");
            inherit_image(s, &sff->sprites[s->link]);
        } else {
            int same_palette = node[18] != 0;
            s->offset = pos + V1_NODE;
            if (s->size > INT_MAX)
                return fail(sff, "invalid SFF v1 PCX length");
            /* Older writers sometimes count the omitted shared palette. */
            if (s->size > limit - s->offset)
                s->size = limit - s->offset;
            if (s->size < 128)
                return fail(sff, "truncated PCX record");
            Byte *pcx = read_payload(sff, s->offset, s->size);
            if (!pcx)
                return 0;
            Byte colors[1024];
            int ok = pcx_dimensions(s, pcx);
            int own_palette = ok && s->depth == 8 && (!same_palette || last_palette == NO_PALETTE);
            if (ok)
                ok = decode_pcx(s, pcx, s->size, NULL, own_palette ? colors : NULL);
            free(pcx);
            if (!ok)
                return fail(sff, "malformed PCX sprite");
            s->format = PCX;
            s->root = i;
            if (own_palette) {
                uint32_t index = sff->palette_count++;
                Palette *p = &sff->palettes[index];
                p->number = index;
                p->count = 256;
                p->root = index;
                p->colors = malloc(sizeof(colors));
                if (!p->colors)
                    return fail(sff, "out of memory reading PCX palette");
                memcpy(p->colors, colors, sizeof(colors));
                last_palette = index;
            }
            if (s->depth == 8)
                s->effective_palette = last_palette;
        }
        s->palette = s->effective_palette;
        pos = next;
    }
    return 1;
}

typedef struct {
    uint32_t key, index;
} NodeKey;

static uint32_t node_key(unsigned group, unsigned number)
{
    return (uint32_t)(group & 65535) << 16 | (number & 65535);
}

static int compare_keys(const void *a, const void *b)
{
    const NodeKey *ka = a, *kb = b;
    if (ka->key != kb->key)
        return ka->key < kb->key ? -1 : 1;
    return (ka->index > kb->index) - (ka->index < kb->index);
}

static uint32_t first_with_key(const NodeKey *keys, uint32_t count, uint32_t key)
{
    uint32_t first = 0;
    while (first < count) {
        uint32_t mid = first + (count - first) / 2;
        if (keys[mid].key < key)
            first = mid + 1;
        else
            count = mid;
    }
    return keys[first].index;        /* Caller searches for a key in this table. */
}

static int load_v2_palettes(Sff *sff, uint32_t table, uint32_t ldata, uint32_t lsize)
{
    for (uint32_t i = 0; i < sff->palette_count; ++i) {
        Palette *p = &sff->palettes[i];
        Byte node[PALETTE_NODE];
        if (!read_at(sff, table + i * PALETTE_NODE, node, sizeof(node)))
            return 0;
        p->group = le16(node);
        p->number = le16(node + 2);
        p->count = le16(node + 4);
        p->link = le16(node + 6);
        uint32_t offset = le32(node + 8);
        p->size = le32(node + 12);
        if (!p->size) {
            if (p->link >= i)
                return fail(sff, "invalid palette link");
            p->root = sff->palettes[p->link].root;
            p->count = sff->palettes[p->root].count;
            continue;
        }
        if (!p->count || p->count > 256 || p->size < p->count * 4 || !fits(offset, p->size, lsize))
            return fail(sff, "invalid palette data range or color count");
        p->offset = ldata + offset;
        p->root = i;
        p->colors = read_payload(sff, p->offset, p->count * 4);
        if (!p->colors)
            return 0;
        if (sff->version == V200) {
            for (unsigned j = 0; j < p->count; ++j)
                p->colors[j * 4 + 3] = j ? 255 : 0;
        }
    }
    return 1;
}

static int load_v2_sprites(Sff *sff, const Byte *header)
{
    uint32_t table = le32(header + 36);
    uint32_t block[] = {le32(header + 52), le32(header + 60)};
    uint32_t block_size[] = {le32(header + 56), le32(header + 64)};
    for (uint32_t i = 0; i < sff->sprite_count; ++i) {
        Sprite *s = &sff->sprites[i];
        Byte node[SPRITE_NODE];
        if (!read_at(sff, table + i * SPRITE_NODE, node, sizeof(node)))
            return 0;
        s->group = (int)le16(node);
        s->number = (int)le16(node + 2);
        s->width = le16(node + 4);
        s->height = le16(node + 6);
        s->x = signed16(node + 8);
        s->y = signed16(node + 10);
        s->link = le16(node + 12);
        s->format = node[14];
        s->depth = node[15];
        uint32_t offset = le32(node + 16);
        s->size = le32(node + 20);
        s->palette = le16(node + 24);
        s->flags = le16(node + 26);
        s->effective_palette = NO_PALETTE;
        if (!s->size) {
            if (s->link >= i)
                return fail(sff, "invalid SFF v2 sprite link");
            inherit_image(s, &sff->sprites[s->link]);
        } else {
            unsigned translated = (s->flags & TDATA) != 0;
            if (!fits(offset, s->size, block_size[translated]))
                return fail(sff, "sprite data extends beyond its SFF v2 block");
            if (!format_matches(s))
                return fail(sff, "unsupported sprite compression or color depth");
            if (!valid_dimensions(s))
                return fail(sff, "invalid SFF v2 sprite dimensions");
            s->offset = block[translated] + offset;
            s->root = i;
        }
        /* The depth in a linked node controls whether it requests a palette
         * override, even when the image's actual depth comes from its source. */
        if (node[15] > 8 || s->palette >= sff->palette_count)
            s->palette = NO_PALETTE;
    }
    return 1;
}

static int resolve_v2_palettes(Sff *sff)
{
    if (!sff->palette_count)
        return 1;
    NodeKey *keys = calloc(sff->palette_count, sizeof(*keys));
    if (!keys)
        return fail(sff, "out of memory indexing palette IDs");
    for (uint32_t i = 0; i < sff->palette_count; ++i) {
        const Palette *p = &sff->palettes[i];
        keys[i].key = node_key(p->group, p->number);
        keys[i].index = i;
    }
    qsort(keys, sff->palette_count, sizeof(*keys), compare_keys);
    for (uint32_t i = 0; i < sff->sprite_count; ++i) {
        Sprite *s = &sff->sprites[i];
        if (!s->size)
            s->effective_palette = sff->sprites[s->link].effective_palette;
        if (s->depth <= 8 && s->palette != NO_PALETTE) {
            const Palette *p = &sff->palettes[s->palette];
            uint32_t slot = first_with_key(keys, sff->palette_count, node_key(p->group, p->number));
            if (sff->palettes[slot].count == (1u << s->depth))
                s->effective_palette = slot;
        }
    }
    free(keys);
    return 1;
}

static int load_v2(Sff *sff, const Byte *header)
{
    uint32_t sprite_table = le32(header + 36), palette_table = le32(header + 44);
    uint32_t ldata = le32(header + 52), lsize = le32(header + 56);
    uint32_t tdata = le32(header + 60), tsize = le32(header + 64);
    sff->compat = le32(header + 24);
    sff->sprite_count = le32(header + 40);
    sff->palette_count = le32(header + 48);
    if (sff->compat > V201)
        return fail(sff, "unsupported SFF compatibility version");
    if ((sff->sprite_count && (sprite_table < HEADER_SIZE ||
         !fits(sprite_table, (uint64_t)sff->sprite_count * SPRITE_NODE, sff->size))) ||
        (sff->palette_count && (palette_table < HEADER_SIZE ||
         !fits(palette_table, (uint64_t)sff->palette_count * PALETTE_NODE, sff->size))) ||
        (lsize && (ldata < HEADER_SIZE || !fits(ldata, lsize, sff->size))) ||
        (tsize && (tdata < HEADER_SIZE || !fits(tdata, tsize, sff->size))))
        return fail(sff, "invalid SFF v2 table or data block range");
    return allocate_sprites(sff) && allocate_palettes(sff, sff->palette_count) &&
           load_v2_palettes(sff, palette_table, ldata, lsize) && load_v2_sprites(sff, header) &&
           resolve_v2_palettes(sff);
}

static int open_sff(Sff *sff, const char *path)
{
    sff->file = fopen(path, "rb");
    if (!sff->file)
        return fail(sff, strerror(errno));
    if (seek64(sff->file, 0, SEEK_END) != 0)
        return fail(sff, "could not determine input size");
    int64_t size = tell64(sff->file);
    if (size < HEADER_SIZE || size > UINT32_MAX)
        return fail(sff, "input size is outside the SFF file range");
    sff->size = (uint32_t)size;
    Byte header[HEADER_SIZE];
    if (!read_at(sff, 0, header, sizeof(header)))
        return 0;
    if (memcmp(header, "ElecbyteSpr", 12))
        return fail(sff, "invalid SFF signature");
    sff->version = le32(header + 12);
    if (sff->version == V101)
        return load_v1(sff, header);
    if (sff->version == V200 || sff->version == V201)
        return load_v2(sff, header);
    return fail(sff, "unsupported SFF version");
}

static void close_sff(Sff *sff)
{
    if (sff->file)
        fclose(sff->file);
    if (sff->palettes) {
        for (uint32_t i = 0; i < sff->palette_count; ++i)
            free(sff->palettes[i].colors);
    }
    free(sff->palettes);
    free(sff->sprites);
}

static int write_bytes(FILE *file, const void *data, size_t size)
{
    return !size || fwrite(data, 1, size, file) == size;
}

static int png_chunk(FILE *file, const char *type, const Byte *data, unsigned size)
{
    Byte header[8], checksum[4];
    memcpy(header + 4, type, 4);
    put_be32(header, size);
    uint32_t crc = crc_update(UINT32_MAX, header + 4, 4);
    put_be32(checksum, ~crc_update(crc, data, size));
    return write_bytes(file, header, sizeof(header)) && write_bytes(file, data, size) &&
           write_bytes(file, checksum, sizeof(checksum));
}

static void rgba_row(Byte *row, const Byte *pixels, const Sprite *s, const Palette *p)
{
    unsigned step = channels(s->depth);
    for (unsigned i = 0; i < s->width; ++i, row += 4, pixels += step) {
        if (s->depth <= 8) {
            unsigned index = *pixels;
            if (index && p && index < p->count)
                memcpy(row, p->colors + index * 4, 4);
            else
                memset(row, 0, 4);
        } else {
            memcpy(row, pixels, 3);
            row[3] = step == 4 ? pixels[3] : (pixels[0] || pixels[1] || pixels[2] ? 255 : 0);
        }
    }
}

/* A zlib stream of stored blocks, split across IDAT chunks. Reuse a single
 * RGBA scanline and write each block as it is produced. */
static int write_png(FILE *out, const Sprite *s, const Byte *pixels, const Palette *palette)
{
    Byte ihdr[13] = {0}, checksum[4];
    static const Byte zlib_header[] = {0x78, 0x01};
    unsigned stride = s->width * 4 + 1;
    Byte *row = malloc(stride);
    Byte *block = malloc(65535 + 5);
    if (!row || !block) {
        free(row);
        free(block);
        return 0;
    }
    put_be32(ihdr, s->width);
    put_be32(ihdr + 4, s->height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    int ok = write_bytes(out, png_signature, sizeof(png_signature)) &&
             png_chunk(out, "IHDR", ihdr, sizeof(ihdr)) &&
             png_chunk(out, "IDAT", zlib_header, sizeof(zlib_header));
    uint32_t adler = 1;
    unsigned used = 0;
    row[0] = 0;
    for (unsigned y = 0; ok && y < s->height; ++y) {
        rgba_row(row + 1, pixels + (size_t)y * s->width * channels(s->depth), s, palette);
        adler = adler_update(adler, row, stride);
        for (unsigned pos = 0; ok && pos < stride;) {
            unsigned n = stride - pos;
            if (n > 65535 - used)
                n = 65535 - used;
            memcpy(block + 5 + used, row + pos, n);
            used += n;
            pos += n;
            int last = pos == stride && y + 1 == s->height;
            if (used == 65535 || last) {
                block[0] = (Byte)last;
                put_le16(block + 1, used);
                put_le16(block + 3, used ^ 65535u);
                ok = png_chunk(out, "IDAT", block, used + 5);
                used = 0;
            }
        }
    }
    put_be32(checksum, adler);
    ok = ok && png_chunk(out, "IDAT", checksum, sizeof(checksum)) && png_chunk(out, "IEND", NULL, 0);
    free(row);
    free(block);
    return ok;
}

static const char *format_name(unsigned format)
{
    switch (format) {
    case RAW: return "raw";
    case RLE8: return "RLE8";
    case RLE5: return "RLE5";
    case LZ5: return "LZ5";
    case PNG8: return "PNG8";
    case PNG24: return "PNG24";
    case PNG32: return "PNG32";
    case PCX: return "PCX";
    default: return "unknown";
    }
}

static const char *version_name(uint32_t version)
{
    switch (version) {
    case V101: return "1.01";
    case V200: return "2.00";
    case V201: return "2.01";
    default: return "unspecified";
    }
}

static int inspect_sff(Sff *sff, const char *path)
{
    /* V1 records were already decoded while collecting the PCX palettes. */
    if (sff->version != V101) {
        for (uint32_t i = 0; i < sff->sprite_count; ++i) {
            const Sprite *s = &sff->sprites[i];
            if (s->size) {
                Byte *encoded, *pixels;
                int ok = load_pixels(sff, s, &encoded, &pixels);
                free_pixels(encoded, pixels);
                if (!ok)
                    return 0;
            }
        }
    }
    printf("%s\nSFF version %s (0x%08" PRIx32 ")\n", path, version_name(sff->version), sff->version);
    if (sff->version == V101) {
        printf("Groups: %" PRIu32 " | Header palette type: %u\n", sff->v1_groups, sff->v1_palette_type);
    } else {
        printf("Compatibility version: %s (0x%08" PRIx32 ")\n", version_name(sff->compat), sff->compat);
    }
    printf("File size: %" PRIu32 " bytes\nSprites: %" PRIu32 "\n", sff->size, sff->sprite_count);
    for (uint32_t i = 0; i < sff->sprite_count; ++i) {
        const Sprite *s = &sff->sprites[i];
        printf("  [%" PRIu32 "] %d,%d  %ux%u  axis %d,%d  %u-bit %s  %" PRIu32 " bytes",
               i, s->group, s->number, s->width, s->height, s->x, s->y, s->depth,
               format_name(s->format), s->size);
        if (!s->size)
            printf("  link [%" PRIu32 "]", s->link);
        else if (sff->version != V101)
            printf("  %s", s->flags & TDATA ? "tdata" : "ldata");
        if (s->palette != NO_PALETTE && s->palette != s->effective_palette)
            printf("  requested palette [%" PRIu32 "]", s->palette);
        if (s->effective_palette != NO_PALETTE) {
            const Palette *p = &sff->palettes[s->effective_palette];
            printf("  palette [%" PRIu32 "] %u,%u", s->effective_palette, p->group, p->number);
        } else if (s->depth <= 8) {
            printf("  palette unassigned");
        }
        putchar('\n');
    }
    printf("Palettes: %" PRIu32 "\n", sff->palette_count);
    for (uint32_t i = 0; i < sff->palette_count; ++i) {
        const Palette *p = &sff->palettes[i];
        const Byte *colors = sff->palettes[p->root].colors;
        printf("  [%" PRIu32 "] %u,%u  %u colors", i, p->group, p->number, p->count);
        if (p->root != i)
            printf("  link [%" PRIu32 "]", p->link);
        else if (sff->version != V101)
            printf("  %" PRIu32 " bytes", p->size);
        for (unsigned j = 0; j < p->count; ++j) {
            if (j % 8 == 0)
                printf("\n    %3u:", j);
            const Byte *c = colors + j * 4;
            printf(" %02X%02X%02X%02X", (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3]);
        }
        putchar('\n');
    }
    return (fflush(stdout) == 0 && !ferror(stdout)) || fail(sff, "could not write the inspection report");
}

static int path_separator(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static const char *filename(const char *path)
{
    const char *name = path;
#ifdef _WIN32
    if (path[0] && path[1] == ':')
        name += 2;
#endif
    for (const char *p = name; *p; ++p) {
        if (path_separator(*p))
            name = p + 1;
    }
    return name;
}

static char *output_path(Sff *sff, const char *input, int optimize)
{
    const char *name = filename(input);
    const char *extension = strrchr(name, '.');
    size_t length = strlen(input);
    size_t prefix = (size_t)(name - input);
    if (!extension || extension == name || strlen(extension) != 4 ||
        (extension[1] != 's' && extension[1] != 'S') ||
        (extension[2] != 'f' && extension[2] != 'F') ||
        (extension[3] != 'f' && extension[3] != 'F')) {
        fail(sff, "input must have a filename stem and .sff extension");
        return NULL;
    }
    if (!optimize)
        length -= 4;
    char *out = malloc(length + (optimize ? 5 : 1));
    if (!out) {
        fail(sff, "out of memory constructing the output path");
        return NULL;
    }
    memcpy(out, input, prefix);
    if (optimize) {
        memcpy(out + prefix, "opt.", 4);
        memcpy(out + prefix + 4, name, length - prefix);
        out[length + 4] = '\0';
    } else {
        memcpy(out + prefix, name, length - prefix);
        out[length] = '\0';
    }
    return out;
}

static int make_directory(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

static void remove_directory(const char *path)
{
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static FILE *create_file(const char *path)
{
#ifdef _WIN32
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
    if (fd < 0)
        return NULL;
#ifdef _WIN32
    FILE *file = _fdopen(fd, "wb");
#else
    FILE *file = fdopen(fd, "wb");
#endif
    if (!file) {
        int error = errno;
#ifdef _WIN32
        _close(fd);
#else
        close(fd);
#endif
        remove(path);
        errno = error;
    }
    return file;
}

static void png_filename(char *name, const Sprite *sprite, uint32_t index, int duplicate)
{
    if (duplicate)
        snprintf(name, 40, "/%d_%d_%" PRIu32 ".png", sprite->group, sprite->number, index);
    else
        snprintf(name, 40, "/%d_%d.png", sprite->group, sprite->number);
}

static int duplicate_keys(Sff *sff, Byte *duplicate)
{
    if (!sff->sprite_count)
        return 1;
    NodeKey *keys = calloc(sff->sprite_count, sizeof(*keys));
    if (!keys)
        return fail(sff, "out of memory indexing sprite IDs");
    for (uint32_t i = 0; i < sff->sprite_count; ++i) {
        keys[i].key = node_key((unsigned)sff->sprites[i].group, (unsigned)sff->sprites[i].number);
        keys[i].index = i;
    }
    qsort(keys, sff->sprite_count, sizeof(*keys), compare_keys);
    for (uint32_t i = 1; i < sff->sprite_count; ++i) {
        if (keys[i].key == keys[i - 1].key)
            duplicate[keys[i].index] = duplicate[keys[i - 1].index] = 1;
    }
    free(keys);
    return 1;
}

static int export_sprite(Sff *sff, const Sprite *s, const char *name)
{
    Byte *encoded, *pixels;
    int ok = load_pixels(sff, &sff->sprites[s->root], &encoded, &pixels);
    if (ok) {
        const Palette *palette = NULL;
        if (s->effective_palette != NO_PALETTE)
            palette = &sff->palettes[sff->palettes[s->effective_palette].root];
        FILE *out = create_file(name);
        if (!out) {
            ok = fail(sff, strerror(errno));
        } else {
            ok = write_png(out, s, pixels, palette);
            if (fclose(out) != 0)
                ok = 0;
            if (!ok) {
                remove(name);
                fail(sff, "could not write PNG data");
            }
        }
    }
    free_pixels(encoded, pixels);
    return ok;
}

static int export_sff(Sff *sff, const char *dir)
{
    size_t len = strlen(dir);
    char *name = malloc(len + 40);
    Byte *duplicate = calloc(sff->sprite_count ? sff->sprite_count : 1, 1);
    int ok = 0;
    if (!name || !duplicate) {
        fail(sff, "out of memory preparing PNG export");
    } else if (duplicate_keys(sff, duplicate)) {
        if (make_directory(dir) != 0) {
            fail(sff, strerror(errno));
        } else {
            uint32_t i;
            memcpy(name, dir, len);
            for (i = 0; i < sff->sprite_count; ++i) {
                png_filename(name + len, &sff->sprites[i], i, duplicate[i]);
                if (!export_sprite(sff, &sff->sprites[i], name))
                    break;
            }
            ok = i == sff->sprite_count;
            if (ok) {
                printf("Wrote %" PRIu32 " PNGs to %s\n", sff->sprite_count, dir);
            } else {
                while (i) {
                    --i;
                    png_filename(name + len, &sff->sprites[i], i, duplicate[i]);
                    remove(name);
                }
                remove_directory(dir);
            }
        }
    }
    free(name);
    free(duplicate);
    return ok;
}

/* Return zero once RLE8 cannot fit within the best known payload size.
 * RLE8 reserves 0x40..0x7f for run markers, even for a one-pixel run. */
static size_t encode_rle8(const Byte *pixels, size_t length, Byte *out, size_t limit)
{
    if (limit < 4)
        return 0;
    size_t size = 4;
    if (out)
        put_le32(out, (uint32_t)length);
    for (size_t i = 0; i < length;) {
        unsigned count = 1;
        Byte value = pixels[i];
        while (count < 63 && count < length - i && pixels[i + count] == value)
            ++count;
        unsigned bytes = count > 1 || (value & 192) == 64 ? 2 : 1;
        if (bytes > limit - size)
            return 0;
        if (out) {
            if (bytes == 2)
                out[size] = (Byte)(64 + count);
            out[size + bytes - 1] = value;
        }
        size += bytes;
        i += count;
    }
    return size;
}

typedef struct {
    uint32_t offset, size;
    unsigned format, link;
} PackedRecord;

typedef struct {
    FILE *file;
    uint32_t pos;
    Sff *sff;
} ArchiveWriter;

static int append_data(ArchiveWriter *writer, const Byte *data, size_t size)
{
    if (size > UINT32_MAX - writer->pos)
        return fail(writer->sff, "optimized archive exceeds the SFF v2 size limit");
    if (!write_bytes(writer->file, data, size))
        return fail(writer->sff, "could not write archive data");
    writer->pos += (uint32_t)size;
    return 1;
}

static int pack_palettes(ArchiveWriter *writer, uint32_t ldata, PackedRecord *records)
{
    Sff *sff = writer->sff;
    if (!sff->palette_count)
        return 1;
    /* Only earlier records whose index fits the link field can be targets. */
    size_t target_count = sff->palette_count <= 65536 ? sff->palette_count : 65536;
    size_t capacity = 2;
    while (capacity < 2 * target_count)
        capacity *= 2;
    uint32_t *index = calloc(capacity, sizeof(*index));
    if (!index)
        return fail(sff, "out of memory deduplicating palettes");
    int ok = 1;
    for (uint32_t i = 0; ok && i < sff->palette_count; ++i) {
        const Palette *p = &sff->palettes[i];
        PackedRecord *r = &records[i];
        if (p->root != i) {
            r->link = p->link;
            continue;
        }
        unsigned bytes = p->count * 4;
        size_t bucket = crc_update(UINT32_MAX, p->colors, bytes) & (capacity - 1);
        while (index[bucket]) {
            const Palette *earlier = &sff->palettes[index[bucket] - 1];
            if (earlier->count == p->count && !memcmp(earlier->colors, p->colors, bytes))
                break;
            bucket = (bucket + 1) & (capacity - 1);
        }
        if (index[bucket]) {
            r->link = index[bucket] - 1;
        } else {
            if (i < target_count)
                index[bucket] = i + 1;
            r->offset = writer->pos - ldata;
            r->size = bytes;
            ok = append_data(writer, p->colors, bytes);
        }
    }
    free(index);
    return ok;
}

static int pack_sprite(ArchiveWriter *writer, const Sprite *s, uint32_t base, PackedRecord *r)
{
    Byte *encoded, *pixels, *rle = NULL;
    int ok = load_pixels(writer->sff, s, &encoded, &pixels);
    if (ok) {
        const Byte *best = encoded;
        size_t length = pixel_bytes(s), size = s->size;
        unsigned format = s->format;
        if (format == PCX || (s->depth <= 8 && length < size)) {
            best = pixels;
            size = length;
            format = RAW;
        }
        if (s->depth <= 8) {
            size_t rle_size = encode_rle8(pixels, length, NULL, size);
            if (rle_size) {
                rle = malloc(rle_size);
                if (rle) {
                    encode_rle8(pixels, length, rle, rle_size);
                    best = rle;
                    size = rle_size;
                    format = RLE8;
                } else {
                    ok = fail(writer->sff, "out of memory encoding RLE8");
                }
            }
        }
        r->offset = writer->pos - base;
        r->size = (uint32_t)size;
        r->format = format;
        if (ok)
            ok = append_data(writer, best, size);
    }
    free(rle);
    free_pixels(encoded, pixels);
    return ok;
}

static int pack_sprite_block(ArchiveWriter *writer, uint32_t base, unsigned block, PackedRecord *records)
{
    for (uint32_t i = 0; i < writer->sff->sprite_count; ++i) {
        const Sprite *s = &writer->sff->sprites[i];
        if (s->size && (s->flags & TDATA) == block && !pack_sprite(writer, s, base, &records[i]))
            return 0;
    }
    return 1;
}

static int write_tables(FILE *out, const Sff *sff, const PackedRecord *palettes, const PackedRecord *sprites)
{
    for (uint32_t i = 0; i < sff->palette_count; ++i) {
        const Palette *p = &sff->palettes[i];
        const PackedRecord *r = &palettes[i];
        Byte node[PALETTE_NODE];
        put_le16(node, p->group);
        put_le16(node + 2, p->number);
        put_le16(node + 4, p->count);
        put_le16(node + 6, r->link);
        put_le32(node + 8, r->offset);
        put_le32(node + 12, r->size);
        if (!write_bytes(out, node, sizeof(node)))
            return 0;
    }
    for (uint32_t i = 0; i < sff->sprite_count; ++i) {
        const Sprite *s = &sff->sprites[i];
        const PackedRecord *r = &sprites[i];
        Byte node[SPRITE_NODE];
        put_le16(node, (unsigned)s->group);
        put_le16(node + 2, (unsigned)s->number);
        put_le16(node + 4, s->width);
        put_le16(node + 6, s->height);
        put_le16(node + 8, (unsigned)s->x);
        put_le16(node + 10, (unsigned)s->y);
        put_le16(node + 12, s->size ? 0 : s->link);
        node[14] = (Byte)sprites[s->root].format;
        node[15] = (Byte)s->depth;
        put_le32(node + 16, r->offset);
        put_le32(node + 20, r->size);
        put_le16(node + 24, s->palette == NO_PALETTE ? UINT16_MAX : s->palette);
        put_le16(node + 26, s->flags);
        if (!write_bytes(out, node, sizeof(node)))
            return 0;
    }
    return 1;
}

static int write_archive(Sff *sff, FILE *out, uint32_t ldata, uint32_t *out_size)
{
    PackedRecord *palettes = calloc(sff->palette_count ? sff->palette_count : 1, sizeof(*palettes));
    PackedRecord *sprites = calloc(sff->sprite_count ? sff->sprite_count : 1, sizeof(*sprites));
    ArchiveWriter writer = {out, ldata, sff};
    int ok = 0;
    if (!palettes || !sprites) {
        fail(sff, "out of memory preparing the output tables");
    } else if (seek64(out, ldata, SEEK_SET) != 0) {
        fail(sff, "could not reserve space for the output tables");
    } else if (pack_palettes(&writer, ldata, palettes) && pack_sprite_block(&writer, ldata, 0, sprites)) {
        /* Sff2_Load reads ldata as a single item even if the archive uses
         * only tdata. One padding byte makes that fread work on file streams. */
        static const Byte padding = 0;
        uint32_t tdata = ldata;
        if (writer.pos != ldata || append_data(&writer, &padding, 1)) {
            tdata = writer.pos;
            if (pack_sprite_block(&writer, tdata, TDATA, sprites)) {
                Byte header[HEADER_SIZE] = {0};
                memcpy(header, "ElecbyteSpr", 12);
                put_le32(header + 12, V201);
                put_le32(header + 24, V201);
                put_le32(header + 36, HEADER_SIZE + sff->palette_count * PALETTE_NODE);
                put_le32(header + 40, sff->sprite_count);
                put_le32(header + 44, HEADER_SIZE);
                put_le32(header + 48, sff->palette_count);
                put_le32(header + 52, ldata);
                put_le32(header + 56, tdata - ldata);
                put_le32(header + 60, tdata);
                put_le32(header + 64, writer.pos - tdata);
                ok = seek64(out, 0, SEEK_SET) == 0 && write_bytes(out, header, sizeof(header)) &&
                     write_tables(out, sff, palettes, sprites);
                if (!ok)
                    fail(sff, "could not write the archive header or tables");
            }
        }
    }
    *out_size = writer.pos;
    free(palettes);
    free(sprites);
    return ok;
}

static int optimize_sff(Sff *sff, const char *path)
{
    uint64_t tables_end = HEADER_SIZE + (uint64_t)sff->palette_count * PALETTE_NODE +
                          (uint64_t)sff->sprite_count * SPRITE_NODE;
    if (tables_end >= UINT32_MAX || sff->palette_count > 65536)
        return fail(sff, "archive tables exceed the SFF v2 sprite and palette field limits");
    for (uint32_t i = 0; i < sff->sprite_count; ++i) {
        const Sprite *s = &sff->sprites[i];
        if (s->group < 0 || s->number < 0)
            return fail(sff, "SFF v2 cannot preserve a negative v1 sprite ID");
        if (s->depth <= 8 && s->palette == NO_PALETTE && sff->palette_count > UINT16_MAX)
            return fail(sff, "SFF v2 cannot encode this sprite without a palette assignment");
    }
    FILE *out = create_file(path);
    if (!out)
        return fail(sff, strerror(errno));
    uint32_t out_size;
    int ok = write_archive(sff, out, (uint32_t)tables_end, &out_size);
    if (fclose(out) != 0 && ok)
        ok = fail(sff, "could not finish writing the archive");
    if (!ok) {
        remove(path);
        return 0;
    }
    printf("Wrote %s (SFF 2.01, %" PRIu32 " -> %" PRIu32 " bytes)\n", path, sff->size, out_size);
    return 1;
}

static void usage(FILE *out)
{
    fputs("Usage:\n"
          "  sffcli                         Show usage\n"
          "  sffcli input.sff               List sprites and palettes\n"
          "  sffcli --export input.sff      Export PNGs to input/ beside the archive\n"
          "  sffcli --optimize input.sff    Write opt.input.sff as SFF v2.01 beside the archive\n",
          out);
}

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")))) {
        usage(stdout);
        return 0;
    }
    int exporting = argc == 3 && !strcmp(argv[1], "--export");
    int optimizing = argc == 3 && !strcmp(argv[1], "--optimize");
    if (!exporting && !optimizing && (argc != 2 || argv[1][0] == '-')) {
        usage(stderr);
        return 2;
    }
    const char *input = argv[argc - 1];
    char *output = NULL;
    Sff sff = {0};
    int ok = open_sff(&sff, input);
    if (ok) {
        if (exporting || optimizing) {
            output = output_path(&sff, input, optimizing);
            ok = output && (exporting ? export_sff(&sff, output) : optimize_sff(&sff, output));
        } else {
            ok = inspect_sff(&sff, input);
        }
    }
    if (!ok)
        fprintf(stderr, "sffcli: %s: %s\n", output ? output : input, sff.error);
    free(output);
    close_sff(&sff);
    return ok ? 0 : 1;
}
