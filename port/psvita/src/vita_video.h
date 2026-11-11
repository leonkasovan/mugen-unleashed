/* Vita owns its GL display; SDL remains the input/audio and System backend. */
#ifndef MUGEN_VITA_VIDEO_H
#define MUGEN_VITA_VIDEO_H
#include "vita_diagnostics.h"
#include "vita_vertex_interface.h"
SDL_Surface *VitaGl_CreateDisplay(int width, int height, int depth);
void VitaGl_ReleaseDisplay(void);
int VitaGl_OwnsDisplay(void);
int VitaGl_HasContext(void);
void VitaGl_SwapBuffers(void);
int VitaGl_SetAttribute(SDL_GLattr attribute, int value);
void VitaGl_DrawQuad(const VitaGlVertexInterface *vertex, const float *corners, int special,
                     float top, float bottom, const DrawParams *params);
#undef SDL_GL_SwapBuffers
#define SDL_GL_SwapBuffers VitaGl_SwapBuffers
#undef SDL_GL_SetAttribute
#define SDL_GL_SetAttribute VitaGl_SetAttribute
#endif
