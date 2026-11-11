#ifndef MUGEN_SDL2KEYS_H
#define MUGEN_SDL2KEYS_H
#include <SDL_keyboard.h>
/* Saved engine IDs are independent of SDL2 keycodes/scancodes. */
int Sdl2_EngineKey(int sdl_keycode);
SDL_Keycode Sdl2_SdlKeyForEngineKey(int engine_key);
const char *Sdl2_KeyName(int engine_key);
#endif
