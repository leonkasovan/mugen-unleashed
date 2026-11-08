/*
 * Port fix: SDL 1.2's Vita driver does not create a vitaGL context. Never use
 * its SceGxm presenter alongside vitaGL. The GL display is process-owned:
 * the pinned vitaGL has no public shutdown API (eglTerminate is a no-op).
 * Engine objects and the metadata surface are released, but a failed GL
 * startup must exit instead of starting a second GXM driver as a fallback.
 */
#include "mugen.h"
#include "vita_video.h"
#include "gl_mvp.h"

static SDL_Surface *display;
static int attempted, ready;
static int vsync = 1;
static int display_width, display_height;

int VitaGl_OwnsDisplay(void) { return attempted; }
int VitaGl_HasContext(void) { return ready; }

SDL_Surface *VitaGl_CreateDisplay(int width, int height, int depth)
{
    SDL_Surface *surface;

    if (width <= 0 || height <= 0 || width > 960 || height > 544 || depth != 32) {
        SDL_SetError("VitaGL requires a display up to 960x544 at depth 32");
        return NULL;
    }
    if (display != NULL) {
        if (display->w == width && display->h == height) return display;
        SDL_SetError("Restart required to change the VitaGL display size");
        return NULL;
    }
    if (ready && (width != display_width || height != display_height)) {
        SDL_SetError("Restart required to change the VitaGL display size");
        return NULL;
    }
    if (!ready && SDL_GetVideoSurface() != NULL) {
        SDL_SetError("Restart required to switch from SDL System to vitaGL");
        return NULL;
    }
    /* SDL surface metadata is used by the engine, not by SDL's presenter. */
    surface = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32,
                                 0xff, 0xff00, 0xff0000, 0xff000000);
    if (surface == NULL) return NULL;
    if (!ready) {
        if (attempted) {
            SDL_FreeSurface(surface);
            SDL_SetError("VitaGL initialization previously failed");
            return NULL;
        }
        attempted = 1;
        /* Keep the 64 MiB newlib heap separate from bounded GPU pools. */
        ready = vglInitWithCustomSizes(1024 * 1024, width, height,
                                      16 * 1024 * 1024, 64 * 1024 * 1024,
                                      0, 0, SCE_GXM_MULTISAMPLE_NONE) == GL_TRUE;
        if (!ready) {
            SDL_FreeSurface(surface);
            SDL_SetError("vglInitWithCustomSizes failed");
            Log_Printf("GL: vitaGL context creation failed\n");
            return NULL;
        }
        vglWaitVblankStart(vsync ? GL_TRUE : GL_FALSE);
        display_width = width;
        display_height = height;
        Log_Printf("GL: vitaGL owns %ix%i display (SDL presenter bypassed)\n", width, height);
    }
    display = surface;
    return display;
}

void VitaGl_ReleaseDisplay(void)
{
    if (display != NULL) SDL_FreeSurface(display);
    display = NULL;
}

void VitaGl_SwapBuffers(void)
{
    if (ready && display != NULL) vglSwapBuffers(GL_FALSE);
}

int VitaGl_SetAttribute(SDL_GLattr attribute, int value)
{
    if (attribute == SDL_GL_SWAP_CONTROL) {
        vsync = value != 0;
        if (ready) vglWaitVblankStart(vsync ? GL_TRUE : GL_FALSE);
    }
    return 0;
}

/*
 * vitaGL immediate mode uses its own FFP program, ignoring glUseProgram.
 * Submit explicit attributes to the programmable path, with a real MVP and
 * all four texture coordinates (including projective Q). The shared CPU
 * builder preserves the original quad arithmetic and triangle-strip order.
 */
void VitaGl_DrawQuad(GLuint program, const float *corners, int special,
                     float top, float bottom, const DrawParams *params)
{
    GlQuadVerts quad;
    GlMvp projection, modelview, mvp;
    GLfloat vertices[GL_QUAD_VERTEX_COUNT * GL_QUAD_VERTEX_FLOATS];
    GLint position, texcoord, matrix;

    if (!ready || display == NULL || program == 0) return;
    position = glGetAttribLocation(program, "aPosition");
    texcoord = glGetAttribLocation(program, "aTexCoord");
    matrix = glGetUniformLocation(program, "uMvp");
    if (position < 0 || texcoord < 0 || matrix < 0) return;
    GlQuad_Build(&quad, corners, special, top, bottom,
                 params->top_offset, params->bottom_offset, params->width_ratio);
    GlQuad_Interleave(vertices, &quad);
    glGetFloatv(GL_PROJECTION_MATRIX, projection.m);
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview.m);
    GlMvp_Multiply(&mvp, &projection, &modelview);
    glUniformMatrix4fv(matrix, 1, GL_FALSE, mvp.m);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE,
                          GL_QUAD_VERTEX_FLOATS * sizeof(GLfloat), vertices);
    glVertexAttribPointer((GLuint)texcoord, 4, GL_FLOAT, GL_FALSE,
                          GL_QUAD_VERTEX_FLOATS * sizeof(GLfloat), vertices + 2);
    glEnableVertexAttribArray((GLuint)position);
    glEnableVertexAttribArray((GLuint)texcoord);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray((GLuint)position);
    glDisableVertexAttribArray((GLuint)texcoord);
}
