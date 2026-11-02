#ifndef MUGEN_SDL2SURFACE_H
#define MUGEN_SDL2SURFACE_H
/*
 * Shared SDL2 scaler entry point (see sdl2surface.c). Requires SDL2 headers.
 * Consumers that cannot use src/sdl2compat.c (Android, Vita SDL2) declare it
 * here and compile sdl2surface.c instead of reimplementing the sampling.
 */
#if defined(MUGEN_SDL2) || defined(ANDROID_PORT) || defined(MUGEN_ANDROID_SW)
/*
 * Guarded for the same reason as sdl2keys.h: this header must be harmless on
 * SDL 1.2 targets, where <SDL_surface.h> does not exist. The .c file already
 * carries the same guard.
 */
#include <SDL_surface.h>

SDL_Surface *Sdl2_ScaleSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);
#ifndef ANDROID_PORT
SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);
#endif

#endif /* SDL2 consumer */

#endif /* MUGEN_SDL2SURFACE_H */
