#ifndef MUGEN_SDL2VIDEO_H
#define MUGEN_SDL2VIDEO_H
/* Engine-owned display operations implemented using native SDL2.
 * These are engine APIs, never replacements for SDL's public symbols.
 * Android supplies its own window/lifecycle backend. VitaGL owns its GL
 * display separately; SDL2 owns only its System presenter. */
#include <SDL.h>

/* Video: one window, one GL context or one streaming texture, created lazily.
 *
 * Ownership (plan section 5.2 step 10): the backend owns the display window,
 * renderer, texture and display surface; the engine borrows the display
 * surface pointer (g_videoSurface/g_screenSurface) and must not free it --
 * Sdl2_VideoQuit/Sdl2_DestroyVideo is the only release path. SpriteSets own
 * their own screen/sprite surfaces. On failure or shutdown no path frees
 * the same surface twice or leaves a borrowed alias dangling: failed
 * resizes return NULL with the old pair intact, and VideoQuit drops every
 * backend allocation together. */
SDL_Surface *Sdl2_SetVideoMode(int width, int height, int bpp, Uint32 flags);
void Sdl2_Flip(SDL_Surface *surface);
void Sdl2_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
void Sdl2_VideoQuit(void);
/* Native Vita display ownership query. */
int Sdl2_HasWindow(void);
/* Drops the cached renderer and texture after SDL reports a device or targets
 * reset, so the next Sdl2_Present recreates them. SDL requires every texture to
 * be recreated; keeping the old one renders into a dead handle (a black or
 * frozen window after a GPU driver reset, a remote-desktop session change or a
 * display mode switch). */
void Sdl2_ResetRenderer(void);
/* Frame-boundary flush (M3): presents if UpdateRect dirtied the display. */
void Sdl2_PresentFrame(void);
void Sdl2_MarkDisplayDirty(void);
void Sdl2_SetCaption(const char *title, const char *icon);
int Sdl2_SetFullscreen(SDL_Surface *screen, int fullscreen);
int Sdl2_ToggleFullScreen(SDL_Surface *screen);
int Sdl2_GL_SetAttribute(SDL_GLattr attr, int value);
void Sdl2_GL_SwapBuffers(void);

/*
 * Size of the drawable the GL context renders into, in pixels.
 *
 * Not the same as the window size on a high-DPI or scaled display, and the GLES
 * presenter (src/gles_render.c Gl_Present) must clear and viewport the drawable,
 * not the logical quad, or the picture lands in a corner. Android answers the
 * same question with its own ANativeWindow call; this is the SDL2 desktop
 * equivalent, kept here because src/ owns no window.
 *
 * Writes 0/0 when there is no window.
 */
void Sdl2_GLDrawableSize(int *w, int *h);

/* The window's native handle (HWND on Windows), or NULL when there is no
 * window yet. Replaces SDL 1.2's SDL_GetWMInfo for the three call sites that
 * only ever read info.window. */
void *Sdl2_GetWindowHandle(void);

/* Diagnostic counters and the independent texture check, for the M0/M3
 * harness. Only declared when the test build defines MUGEN_SDL2_DIAG; the
 * engine never does. */
void Sdl2_DiagReset(void);
void Sdl2_DiagCounters(int *present, int *tex_create, int *tex_destroy);
int Sdl2_DiagTextureSize(int *w, int *h, Uint32 *format);
/* which: 1 = fail the next texture creation, 2 = fail the next surface
 * creation, 3 = fail the next SDL_CreateRGBSurfaceWithFormat (the
 * OpenGLScreen placeholder). One-shot; consumed by the next creation call. */
void Sdl2_DiagFailNext(int which);

/* Placeholder lifetime identity (R02). Frees a surface's allocation from the
 * gate's point of view without changing its address, so an early free is
 * detectable on a build without AddressSanitizer. */
int Sdl2_DiagPlaceholderSerial(void);
void Sdl2_DiagPlaceholderCounts(unsigned long *allocs, unsigned long *frees);
int Sdl2_DiagSurfaceIsLive(const void *surface, int serial);

int Sdl2_SetSwapInterval(int interval);
#endif
