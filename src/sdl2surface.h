#ifndef MUGEN_SDL2SURFACE_H
#define MUGEN_SDL2SURFACE_H
#include <SDL_surface.h>
/* Shared engine scaler; palette/alpha/sampling behavior is regression-tested. */
SDL_Surface *Sdl2_ScaleSurface(SDL_Surface *src, double zoomx, double zoomy, int smooth);
#endif
