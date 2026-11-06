/* Port fix: checked image arithmetic shared by GL uploads and readback.
 * The engine's pixel counts and public conversion helpers still use int,
 * so a payload must fit INT_MAX even on a host with wider pointers. */
#ifndef MUGEN_GL_SIZE_H
#define MUGEN_GL_SIZE_H
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static __inline int GlImage_Size(int w, int h, size_t bpp, size_t *row, size_t *bytes)
{
    size_t stride, total;
    if (w <= 0 || h <= 0 || bpp == 0 || (size_t)w > SIZE_MAX / bpp) {
        return 0;
    }
    stride = (size_t)w * bpp;
    if ((size_t)h > SIZE_MAX / stride) {
        return 0;
    }
    total = stride * (size_t)h;
    if (total > INT_MAX) {
        return 0;
    }
    if (row != NULL) *row = stride;
    if (bytes != NULL) *bytes = total;
    return 1;
}
#endif
