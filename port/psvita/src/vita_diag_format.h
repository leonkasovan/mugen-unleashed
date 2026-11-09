/* Portable screenshot format helpers: no Vita or graphics driver needed. */
#ifndef MUGEN_VITA_DIAG_FORMAT_H
#define MUGEN_VITA_DIAG_FORMAT_H
#include <stdint.h>
#include <string.h>
static inline int VitaDiag_ValidCapture(const void *base, uint32_t width, uint32_t height, uint32_t pitch)
{
    return base != NULL && width > 0 && width <= 960 && height > 0 && height <= 544 &&
           pitch >= width && pitch <= 1024;
}
/* Presentation requires native API success and the actual active descriptor;
   in-memory drawing or a callback alone does not establish panel scanout. */
static inline int VitaDiag_PresentationAccepted(int set_result, int get_result,
    const void *submitted, const void *active, uint32_t width, uint32_t height,
    uint32_t pitch, uint32_t expected_width, uint32_t expected_height, uint32_t expected_pitch)
{
    return set_result >= 0 && get_result >= 0 && submitted != NULL && active == submitted &&
           width == expected_width && height == expected_height && pitch == expected_pitch &&
           VitaDiag_ValidCapture(active,width,height,pitch);
}
static inline void VitaDiag_BmpPut32(unsigned char *p, uint32_t v)
{
    for (int i=0; i<4; i++) p[i]=(unsigned char)(v>>(i*8));
}
static inline int VitaDiag_BmpHeader(unsigned char header[54], uint32_t width, uint32_t height)
{
    uint32_t bytes;
    if (width == 0 || width > 960 || height == 0 || height > 544) return 0;
    bytes=width*height*4;
    memset(header,0,54);
    header[0]='B';header[1]='M';header[26]=1;header[28]=32;
    VitaDiag_BmpPut32(header+2,54+bytes);VitaDiag_BmpPut32(header+10,54);
    VitaDiag_BmpPut32(header+14,40);VitaDiag_BmpPut32(header+18,width);
    VitaDiag_BmpPut32(header+22,height);VitaDiag_BmpPut32(header+34,bytes);
    return 1;
}
static inline void VitaDiag_BmpRow(unsigned char *dst, const unsigned char *src, uint32_t width)
{
    for (uint32_t x=0; x<width; x++) {
        dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4];dst[x*4+3]=255;
    }
}
#endif
