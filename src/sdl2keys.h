#ifndef MUGEN_SDL2KEYS_H
#define MUGEN_SDL2KEYS_H
/*
 * Shared SDL2 <-> engine key translation (see sdl2keys.c). Consumers that
 * cannot use src/sdl2compat.c (Android, Vita SDL2) include this header and
 * compile sdl2keys.c instead. Requires SDL2 headers for SDL_Keycode.
 */
#if defined(MUGEN_SDL2) || defined(ANDROID_PORT) || defined(MUGEN_ANDROID_SW)
/*
 * Guarded because this header is also reached from SDL 1.2 builds: the
 * Makefile globs C sources under src/, so a translation unit that includes this header on
 * a non-SDL2 target would need <SDL_keycode.h>, which does not exist there.
 * With the guard the whole header is empty on those targets, matching the
 * guard the .c file already carries.
 */
#include <SDL_keycode.h>

int Sdl2_EngineKey(int sdl_keycode);
SDL_Keycode Sdl2_SdlKeyForEngineKey(int engine_key);

#endif /* SDL2 consumer */

#endif /* MUGEN_SDL2KEYS_H */
