/* Host mocks exercise the actual Vita display adapter and vertex submission.
 * They cannot validate GXM, the runtime shader compiler or displayed pixels. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUGEN_H /* Supply only the adapter's dependencies, without a Vita SDK. */
typedef unsigned GLuint;
typedef int GLint, SDL_GLattr;
typedef float GLfloat;
typedef struct { int w, h; } SDL_Surface;
typedef struct DrawParams { float top_offset, bottom_offset, width_ratio; } DrawParams;
enum { GL_FALSE, GL_TRUE, GL_PROJECTION_MATRIX, GL_MODELVIEW_MATRIX,
       GL_ARRAY_BUFFER, GL_FLOAT, GL_TRIANGLE_STRIP, SDL_GL_SWAP_CONTROL };
#define SDL_SWSURFACE 0
#define SCE_GXM_MULTISAMPLE_NONE 0
static int allocations, frees, inits, swaps, draws, enables, disables;
static int allocation_failure, init_failure, missing_attribute, last_vsync = -1;
static SDL_Surface *sdl_display;
static const float *position_pointer, *texcoord_pointer;
static float uploaded_mvp[16], submitted[24];

static SDL_Surface *SDL_CreateRGBSurface(int flags, int w, int h, int d,
                                         unsigned r, unsigned g, unsigned b, unsigned a)
{
    SDL_Surface *s;
    assert(flags == 0 && d == 32 && r == 0xff && g == 0xff00 && b == 0xff0000 && a == 0xff000000);
    if (allocation_failure) return NULL;
    s = malloc(sizeof(*s)); assert(s); s->w = w; s->h = h; ++allocations; return s;
}
static void SDL_FreeSurface(SDL_Surface *s) { assert(s); ++frees; free(s); }
static SDL_Surface *SDL_GetVideoSurface(void) { return sdl_display; }
static void SDL_SetError(const char *format, ...) { (void)format; }
static void Log_Printf(const char *format, ...) { (void)format; }
static int vglInitWithCustomSizes(int pool, int w, int h, int ram, int cdram,
                                 int phy, int dialog, int msaa)
{
    assert(pool == 1024*1024 && w == 640 && h == 480);
    assert(ram == 16*1024*1024 && cdram == 64*1024*1024 && !phy && !dialog && !msaa);
    ++inits; return !init_failure;
}
static void vglWaitVblankStart(int value) { last_vsync = value; }
static void vglSwapBuffers(int dialog) { assert(!dialog); ++swaps; }
static GLint glGetAttribLocation(GLuint p, const char *name)
{ assert(p == 7); return missing_attribute ? -1 : (strcmp(name, "aPosition") == 0 ? 2 : 5); }
static GLint glGetUniformLocation(GLuint p, const char *name)
{ assert(p == 7 && strcmp(name, "uMvp") == 0); return 9; }
static void glGetFloatv(int name, float *m)
{
    int i;
    for (i = 0; i < 16; ++i) m[i] = i % 5 == 0 ? 1.0f : 0.0f;
    if (name == GL_PROJECTION_MATRIX) { m[0] = 2; m[5] = 3; }
    else { assert(name == GL_MODELVIEW_MATRIX); m[12] = 4; m[13] = 5; }
}
static void glUniformMatrix4fv(int loc, int count, int transpose, const float *m)
{ assert(loc == 9 && count == 1 && !transpose); memcpy(uploaded_mvp,m,sizeof(uploaded_mvp)); }
static void glBindBuffer(int target, int buffer) { assert(target == GL_ARRAY_BUFFER && !buffer); }
static void glVertexAttribPointer(unsigned loc, int size, int type, int norm, int stride, const void *ptr)
{
    assert(type == GL_FLOAT && !norm && stride == 6*(int)sizeof(float));
    if (loc == 2) { assert(size == 2); position_pointer = ptr; }
    else { assert(loc == 5 && size == 4); texcoord_pointer = ptr; }
}
static void glEnableVertexAttribArray(unsigned loc) { assert(loc == 2 || loc == 5); ++enables; }
static void glDisableVertexAttribArray(unsigned loc) { assert(loc == 2 || loc == 5); ++disables; }
static void glDrawArrays(int mode, int first, int count)
{
    assert(mode == GL_TRIANGLE_STRIP && first == 0 && count == 4);
    assert(texcoord_pointer == position_pointer + 2);
    memcpy(submitted,position_pointer,sizeof(submitted)); ++draws;
}

#include "../port/psvita/src/vita_video.c"

int main(int argc, char **argv)
{
    SDL_Surface *s;
    DrawParams dp = { .top_offset = 0, .bottom_offset = 0, .width_ratio = 2 };
    float corners[8] = {0,0,10,0,0,20,10,20};
    if (argc > 1 && strcmp(argv[1], "alloc-fail") == 0) allocation_failure = 1;
    if (argc > 1 && strcmp(argv[1], "init-fail") == 0) init_failure = 1;
    if (argc > 1 && strcmp(argv[1], "sdl-owned") == 0) {
        SDL_Surface existing = {640,480};
        sdl_display = &existing;
        assert(!VitaGl_CreateDisplay(640,480,32));
        assert(!VitaGl_OwnsDisplay() && !inits && !allocations);
        puts("ok: existing SDL owner prevents a second GXM owner"); return 0;
    }
    assert(!VitaGl_OwnsDisplay() && !VitaGl_HasContext());
    VitaGl_SwapBuffers(); assert(!swaps);
    VitaGl_DrawQuad(7, corners, 0, 1, 1, &dp); assert(!draws);
    assert(!VitaGl_CreateDisplay(0, 480, 32));
    assert(!VitaGl_CreateDisplay(640, 480, 16));
    assert(!inits && !VitaGl_OwnsDisplay());
    VitaGl_SetAttribute(SDL_GL_SWAP_CONTROL, 0);
    s = VitaGl_CreateDisplay(640,480,32);
    if (allocation_failure) {
        assert(!s && !inits && !VitaGl_OwnsDisplay());
        allocation_failure = 0; s = VitaGl_CreateDisplay(640,480,32); assert(s);
    }
    if (init_failure) {
        assert(!s && inits == 1 && allocations == frees);
        assert(VitaGl_OwnsDisplay() && !VitaGl_HasContext());
        assert(!VitaGl_CreateDisplay(640,480,32) && inits == 1);
        assert(allocations == frees); puts("ok: partial init cannot retry GXM"); return 0;
    }
    assert(s && inits == 1 && last_vsync == 0 && VitaGl_HasContext());
    assert(VitaGl_CreateDisplay(640,480,32) == s && inits == 1);
    assert(!VitaGl_CreateDisplay(480,272,32));
    VitaGl_SwapBuffers(); assert(swaps == 1);
    VitaGl_SetAttribute(SDL_GL_SWAP_CONTROL, 1); assert(last_vsync == 1);
    VitaGl_DrawQuad(7,corners,1,2,1,&dp);
    assert(draws == 1 && enables == 2 && disables == 2);
    assert(uploaded_mvp[0] == 2 && uploaded_mvp[5] == 3);
    assert(uploaded_mvp[12] == 8 && uploaded_mvp[13] == 15);
    assert(submitted[0] == 0 && submitted[6] == 10 && submitted[19] == 20);
    assert(submitted[2] == -0.5f && submitted[8] == 1.5f);
    assert(submitted[17] == 1 && submitted[23] == 1); /* Q = (2/1)/2 */
    dp.width_ratio = 1;
    VitaGl_DrawQuad(7,corners,1,2,1,&dp);
    assert(submitted[17] == 2 && submitted[23] == 2); /* Q survives submission */
    missing_attribute = 1; VitaGl_DrawQuad(7,corners,0,1,1,&dp); assert(draws == 2);
    VitaGl_ReleaseDisplay(); VitaGl_ReleaseDisplay(); assert(allocations == frees);
    VitaGl_SwapBuffers(); assert(swaps == 1);
    assert(!VitaGl_CreateDisplay(480,272,32));
    assert(VitaGl_CreateDisplay(640,480,32) && inits == 1);
    VitaGl_ReleaseDisplay(); assert(allocations == frees);
    puts("ok: Vita display ownership, swap, MVP and quad submission");
    return 0;
}
