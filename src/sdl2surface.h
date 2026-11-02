#ifndef MUGEN_SDL2SURFACE_H
#define MUGEN_SDL2SURFACE_H
/*
 * Shared SDL2 scaler entry point (see sdl2surface.c). Requires SDL2 headers.
 * Consumers that cannot use src/sdl2compat.c (Android, Vita SDL2) declare it
 * here and compile sdl2surface.c instead of reimplementing the sampling.
 */
#include <SDL_surface.h>

SDL_Surface *Sdl2_ScaleSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);
#ifndef ANDROID_PORT
SDL_Surface *zoomSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);
#endif

#endif /* MUGEN_SDL2SURFACE_H */
