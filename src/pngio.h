#ifndef MUGEN_PNGIO_H
#define MUGEN_PNGIO_H

/*
 * PNG reading and writing on top of libpng (module 0x477140-0x477ABF):
 * screenshots and the PNG formats (10-12) of SFF v2 sprites.
 */

/*
 * Growable byte buffer of the sprite codec module (spritecodec.c, the SFF v2
 * RLE/LZ5 encoders); the PNG encoder writes into one.
 */
typedef struct GrowBuf {
    int size;               /* 0x00 bytes allocated */
    int grow_step;          /* 0x04 sizes are rounded up to a multiple of this when growing */
    unsigned char *data;    /* 0x08 */
} GrowBuf;
MUGEN_ASSERT_SIZE(GrowBuf, 0x0C);

/* Size of the PNG data prefix of SFF v2 sprites: the decoded size (int). */
#define PNG_SPRITE_PREFIX 4

/* mugen-unleashed: any PNG to 8-bit RGBA (textures of 3D models). */
unsigned char *Png_DecodeRGBA(const void *data, size_t size, int *width_out, int *height_out);

#endif /* MUGEN_PNGIO_H */
