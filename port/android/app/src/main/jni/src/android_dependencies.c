/* Compile against the real SDL2 companion headers, never SDL 1.2 headers. */
#include "android_platform.h"
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#if SDL_MIXER_MAJOR_VERSION != 2 || SDL_TTF_MAJOR_VERSION != 2
#error Android requires native SDL2_mixer and SDL2_ttf
#endif
#if SDL_TTF_MAJOR_VERSION == 2 && SDL_TTF_MINOR_VERSION == 0 && SDL_TTF_PATCHLEVEL < 12
#error SDL_ttf 2.0.11 and earlier target SDL 1.2, not SDL2
#endif
