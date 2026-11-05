#ifndef MUGEN_ANDROID_PLATFORM_H
#define MUGEN_ANDROID_PLATFORM_H

#ifdef MUGEN_GLES
#include <glad.h>
#endif
#include <SDL.h>
#if SDL_MAJOR_VERSION != 2
#error Android requires the native SDL2 headers
#endif
#if defined(MUGEN_SDL2) || defined(MUGEN_VITA_GL)
#error Android must not compile the desktop SDL compatibility layer or the Vita backend
#endif
#if defined(SDL_SetVideoMode) || defined(SDL_Flip) || defined(SDL_UpdateRect) || \
    defined(SDL_GetKeyState) || defined(SDL_AudioDriverName) || defined(SDL_WM_SetCaption) || \
    defined(SDL_EnableUNICODE) || defined(SDL_EnableKeyRepeat) || defined(SDL_putenv)
#error SDL 1.2 compatibility aliases must not be active on Android
#endif

/* Native SDL2 engine operations; no SDL 1.2 aliases, flags or fake types.
 * Video operations run on the SDL main thread. */
SDL_Surface *Android_CreateDisplay(int width, int height, int depth, int keep_aspect);
#ifdef MUGEN_GLES
/* GLES presentation path; see android_video.c for the window ownership rule. */
SDL_Surface *Android_CreateDisplayGL(int width, int height, int depth, int keep_aspect);
int Android_GLPresent(void);
void Android_GLDrawableSize(int *w, int *h);
/* Context loss (RENDERING_PLAN 12.6): rebuild after Android destroyed it. */
int Android_GLRecreateContext(void);
int Android_GLContextValid(void);
/* Test hook: destroy the context as Android would (loss simulation). */
int Android_GLForceContextLoss(void);
/* Tears the GL window and context down for the System fallback. */
void Android_DestroyGLContext(void);
/* Requests the GL -> System recovery after a failed context restore. */
void Android_RequestRendererFallback(void);
#endif
int Android_PresentFrame(void);
void Android_MarkDisplayDirty(SDL_Surface *surface);
void Android_SetBackground(int background);
void Android_ResetRenderer(void);
void Android_VideoQuit(void);
int Android_SetFullscreen(int fullscreen);
void Android_PadDeviceEvent(const SDL_Event *event);

#endif
