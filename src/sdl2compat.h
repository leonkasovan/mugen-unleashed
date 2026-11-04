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
int Sdl2_ToggleFullScreen(SDL_Surface *screen);
int Sdl2_GL_SetAttribute(SDL_GLattr attr, int value);
void Sdl2_GL_SwapBuffers(void);

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

/* Audio: SDL 1.2's SDL_AudioDriverName; SDL2 only has the no-length form. */
char *Sdl2_AudioDriverName(char *namebuf, int maxlen);

/*
 * SDL2 keycode -> the engine's key identifier (SDL 1.2's numbering).
 *
 * The engine is full of SDL 1.2 keycodes: SDLK_UP == 273, SDLK_RETURN == 13,
 * SDLK_SPACE == 32, SDLK_PAUSE == 19. They index g_keyState, g_keyMap and
 * g_inputState, they are written to mugen.cfg, and they are what the console's
 * key bindings compare against. SDL2 renumbered every non-printable key to
 * scancode|SDLK_SCANCODE_MASK, and SDL2's scancode is NOT SDL 1.2's keycode
 * either (SDL2 SDL_SCANCODE_UP == 82 against SDL 1.2 SDLK_UP == 273), so no
 * shift of the SDL2 value can recover it. The conversion is a name-exact table.
 *
 * Boundaries: the result is either 0 or inside the engine's key space, so
 * callers may index g_keyState with it. A key SDL2 has and SDL 1.2 did not
 * (LGUI, PRINTSCREEN, ...) returns 0, which every consumer already treats as
 * "ignore". Like SDL 1.2, the translation is applied per event and is not
 * remembered: a key down-translated by a layout change can stay up.
 */
/* Key translation (Sdl2_EngineKey, Sdl2_SdlKeyForEngineKey) lives in
 * src/sdl2keys.h so SDL2 consumers without this file (Android, Vita SDL2)
 * share the same tables. Include that header for them. */
#include "sdl2keys.h"

/* SDL 1.2's SDLKey. SDL2 renamed the type to SDL_Keycode and returns *its* key
 * names, so title.c's binding names go through the shim instead. */
typedef int SDLKey;
const char *Sdl2_KeyName(int engine_key);

/* Key repeat and unicode translation are SDL2 platform behaviour. They are
 * kept as no-ops so the reconstructed calls (and their comments) stay. */
int Sdl2_EnableKeyRepeat(int delay, int interval);
int Sdl2_EnableUNICODE(int enable);

/* The zoomSurface sampler lives in src/sdl2surface.h so SDL2 consumers
 * without this file (Android, Vita SDL2) scale identically. */
#include "sdl2surface.h"

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
