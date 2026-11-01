#ifndef MUGEN_SDL2COMPAT_H
#define MUGEN_SDL2COMPAT_H

/*
 * SDL 1.2 over SDL2: what the engine still expects to find.
 *
 * This header is included by mugen.h right after <SDL.h> when the build defines
 * MUGEN_SDL2 (Makefile SDL_BACKEND=2). It is not used by the SDL 1.2 build and
 * not by the Android or Vita ports, which have their own layers.
 *
 * Why a layer instead of editing the call sites: src/video.c, src/gfx.c,
 * src/system.c and src/sdlmain.c are faithful reconstructions of the original
 * machine code (see AGENTS.md), and their SDL 1.2 calls carry address comments
 * that must keep matching the original. The removed entry points are therefore
 * reimplemented in src/sdl2compat.c and reached through the macros at the
 * bottom of this file, so those modules keep their SDL 1.2 shape.
 *
 * The one thing that is deliberately NOT mirrored is the entry point: SDL2's
 * SDLmain would add a second WinMain beside the reconstructed one in
 * src/sdlmain.c. That is handled with SDL_MAIN_HANDLED in the build, not here.
 */

#ifdef MUGEN_SDL2

/* SDL 1.2 surface/window flags the engine still uses. The values are SDL
 * 1.2's, and Sdl2_SetVideoMode interprets them itself -- they are never handed
 * to SDL2 as window or surface flags, so there is no collision with SDL2's
 * differently valued SDL_WINDOW_* bits. */
#ifndef SDL_SWSURFACE
#define SDL_SWSURFACE 0x00000000
#endif
#ifndef SDL_HWSURFACE
#define SDL_HWSURFACE 0x00000001
#endif
#ifndef SDL_OPENGL
#define SDL_OPENGL 0x00000002
#endif
#ifndef SDL_RESIZABLE
#define SDL_RESIZABLE 0x00000010
#endif
#ifndef SDL_NOFRAME
#define SDL_NOFRAME 0x00000020
#endif
#ifndef SDL_HWACCEL
#define SDL_HWACCEL 0x00000100
#endif
#ifndef SDL_DOUBLEBUF
#define SDL_DOUBLEBUF 0x40000000
#endif
#ifndef SDL_FULLSCREEN
#define SDL_FULLSCREEN 0x80000000
#endif
/* Accepted and ignored by SDL2: surfaces are always software now. */
#ifndef SDL_ANYFORMAT
#define SDL_ANYFORMAT 0x10000000
#endif
#ifndef SDL_HWPALETTE
#define SDL_HWPALETTE 0x20000000
#endif
/* SDL2 dropped these two spellings of "on" and "off". */
#ifndef SDL_ENABLE
#define SDL_ENABLE 1
#endif
#ifndef SDL_DISABLE
#define SDL_DISABLE 0
#endif

/* Key names SDL2 renamed or removed. The engine's reconstructed tables use the
 * SDL 1.2 spellings (a scroll-lock key that unpauses a fight, and the "highest
 * keycode" test that decides whether a binding has a printable name). */
#ifndef SDLK_SCROLLOCK
#define SDLK_SCROLLOCK SDLK_SCROLLLOCK
#endif
#ifndef SDLK_LAST
#define SDLK_LAST 0x140      /* SDL 1.2's last keycode; SDL2 has no such count */
#endif
/* SDL 1.2's key type. SDL2 only has SDL_Keycode (Sint32), which is what every
 * Site that names SDLKey actually passes. */
typedef SDL_Keycode SDLKey;

/*
 * SDL_GL_SWAP_CONTROL was an SDL 1.2 attribute passed to SDL_GL_SetAttribute
 * before the context existed. SDL2 has no such attribute: vsync is set on the
 * context afterwards. The value below is not an SDL2 attribute; Sdl2_GL_SetAttribute
 * records it and Sdl2_SetVideoMode applies it once the context is current.
 */
#define SDL_GL_SWAP_CONTROL ((SDL_GLattr)0x7FFF)

/* --- the shims (src/sdl2compat.c) ---------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Video: one window, one GL context or one streaming texture, created lazily. */
SDL_Surface *Sdl2_SetVideoMode(int width, int height, int bpp, Uint32 flags);
void Sdl2_Flip(SDL_Surface *surface);
void Sdl2_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
void Sdl2_VideoQuit(void);
void Sdl2_SetCaption(const char *title, const char *icon);
int Sdl2_ToggleFullScreen(SDL_Surface *screen);
int Sdl2_GL_SetAttribute(SDL_GLattr attr, int value);
void Sdl2_GL_SwapBuffers(void);

/* The window's native handle (HWND on Windows), or NULL when there is no
 * window yet. Replaces SDL 1.2's SDL_GetWMInfo for the three call sites that
 * only ever read info.window. */
void *Sdl2_GetWindowHandle(void);

/* Audio: SDL 1.2's SDL_AudioDriverName; SDL2 only has the no-length form. */
char *Sdl2_AudioDriverName(char *namebuf, int maxlen);

/* Key repeat and unicode translation are SDL2 platform behaviour. They are
 * kept as no-ops so the reconstructed calls (and their comments) stay. */
int Sdl2_EnableKeyRepeat(int delay, int interval);
int Sdl2_EnableUNICODE(int enable);

/*
 * SDL_gfx's zoomSurface: SDL2_gfx is not part of the x86devkit, so the one
 * entry the engine uses is implemented here. Contract is SDL_gfx's: a new
 * surface of (src->w * zoomx, src->h * zoomy), the source's pixel format and
 * colour-key/alpha state preserved, freed by SDL_FreeSurface. smooth != 0 is
 * refused rather than silently approximated: no call site asks for it.
 */
SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);

#ifdef __cplusplus
}
#endif

/* --- names the reconstructed modules use --------------------------------- */
#define SDL_SetVideoMode Sdl2_SetVideoMode
#define SDL_Flip Sdl2_Flip
#define SDL_UpdateRect Sdl2_UpdateRect
#define SDL_WM_SetCaption Sdl2_SetCaption
#define SDL_WM_ToggleFullScreen Sdl2_ToggleFullScreen
#define SDL_GL_SetAttribute Sdl2_GL_SetAttribute
#define SDL_GL_SwapBuffers Sdl2_GL_SwapBuffers
#define SDL_AudioDriverName Sdl2_AudioDriverName
#define SDL_EnableKeyRepeat Sdl2_EnableKeyRepeat
#define SDL_EnableUNICODE Sdl2_EnableUNICODE
/* SDL 1.2's SDL_GetKeyState and SDL2's SDL_GetKeyboardState have the same
 * shape; only the index space differs, and keyboard.c translates that. */
#define SDL_GetKeyState(numkeys) SDL_GetKeyboardState(numkeys)

#endif /* MUGEN_SDL2 */

#endif /* MUGEN_SDL2COMPAT_H */
