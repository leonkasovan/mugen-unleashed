#ifndef MUGEN_SDL2KEYS_H
#define MUGEN_SDL2KEYS_H
/*
 * Shared SDL2 <-> engine key translation (see sdl2keys.c). Consumers that
 * cannot use src/sdl2compat.c (Android, Vita SDL2) include this header and
 * compile sdl2keys.c instead. Requires SDL2 headers for SDL_Keycode.
 */
#include <SDL_keycode.h>

int Sdl2_EngineKey(int sdl_keycode);
SDL_Keycode Sdl2_SdlKeyForEngineKey(int engine_key);

#endif /* MUGEN_SDL2KEYS_H */
