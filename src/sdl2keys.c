/*
 * SDL2 <-> engine key translation, shared by every SDL2 consumer.
 *
 * The engine's key identifiers are SDL 1.2 numbering (SDLK_UP is 273).
 * SDL2 numbers every non-printable key as scancode|0x40000000, so an
 * unconverted SDL2 event would index hundreds of megabytes past g_keyState.
 * Sdl2_EngineKey is the one forward conversion (0 = "SDL 1.2 had no such
 * key"); Sdl2_SdlKeyForEngineKey is its exact inverse for focus resync.
 *
 * Extracted from src/sdl2compat.c (PLAN Android B2, desktop SDL2_PLAN Stage 3)
 * so SDL2 consumers that must NOT pull in the desktop window/present
 * implementation (Android, Vita SDL2) can still translate keys identically.
 * No window, renderer, surface or audio state here -- pure tables.
 *
 * The two functions must stay exact inverses: a key pressed through one and
 * released through the other must agree, or focus resync disagrees with event
 * delivery.
 */
#include "sdl2keys.h"

int Sdl2_EngineKey(int sdl_keycode)
{
    /* SDL 1.2's numbering for everything below 256; ASCII is identical. */
    if (sdl_keycode >= 0 && sdl_keycode < 256) {
        return sdl_keycode;
    }
    switch (sdl_keycode) {
    /*
     * SDLK_PAUSE is the one non-ASCII key SDL 1.2 numbered below 256 (19, the
     * PC pause scancode), so the range check above does not cover it. The
     * engine treats it specially: KEY_PAUSE_TOGGLE in keyboard.c toggles
     * g_pauseToggled, and gameloop.c tests it. SDL2 moved it above the mask.
     */
    case SDLK_PAUSE: return 19;         /* SDL 1.2 SDLK_PAUSE */
    /*
     * Keypad. SDL 1.2 numbered the digits 256..265 and the operators
     * 266..272; SDL2 spells all of them scancode|SDLK_SCANCODE_MASK. These
     * must match Sdl2_SdlKeyForEngineKey exactly, or a key can be pressed but
     * not released, and focus resync disagrees with event delivery.
     */
    case SDLK_KP_0: return 256;         /* SDL 1.2 SDLK_KP0 */
    case SDLK_KP_1: return 257;
    case SDLK_KP_2: return 258;
    case SDLK_KP_3: return 259;
    case SDLK_KP_4: return 260;
    case SDLK_KP_5: return 261;
    case SDLK_KP_6: return 262;
    case SDLK_KP_7: return 263;
    case SDLK_KP_8: return 264;
    case SDLK_KP_9: return 265;
    case SDLK_KP_PERIOD: return 266;   /* SDL 1.2 SDLK_KP_PERIOD */
    case SDLK_KP_DIVIDE: return 267;   /* SDL 1.2 SDLK_KP_DIVIDE */
    case SDLK_KP_MULTIPLY: return 268;   /* SDL 1.2 SDLK_KP_MULTIPLY */
    case SDLK_KP_MINUS: return 269;   /* SDL 1.2 SDLK_KP_MINUS */
    case SDLK_KP_PLUS: return 270;   /* SDL 1.2 SDLK_KP_PLUS */
    case SDLK_KP_ENTER: return 271;   /* SDL 1.2 SDLK_KP_ENTER */
    case SDLK_KP_EQUALS: return 272;   /* SDL 1.2 SDLK_KP_EQUALS */
    case SDLK_NUMLOCKCLEAR: return 300;   /* SDL 1.2 SDLK_NUMLOCK */
    case SDLK_CAPSLOCK: return 301;       /* SDL 1.2 SDLK_CAPSLOCK */
    case SDLK_SCROLLLOCK: return 302;     /* SDL 1.2 SDLK_SCROLLOCK */
    case SDLK_UP: return 273;   /* SDL 1.2 SDLK_UP */
    case SDLK_DOWN: return 274;   /* SDL 1.2 SDLK_DOWN */
    case SDLK_RIGHT: return 275;   /* SDL 1.2 SDLK_RIGHT */
    case SDLK_LEFT: return 276;   /* SDL 1.2 SDLK_LEFT */
    case SDLK_INSERT: return 277;   /* SDL 1.2 SDLK_INSERT */
    case SDLK_HOME: return 278;   /* SDL 1.2 SDLK_HOME */
    case SDLK_END: return 279;   /* SDL 1.2 SDLK_END */
    case SDLK_PAGEUP: return 280;   /* SDL 1.2 SDLK_PAGEUP */
    case SDLK_PAGEDOWN: return 281;   /* SDL 1.2 SDLK_PAGEDOWN */
    case SDLK_F1: return 282;   /* SDL 1.2 SDLK_F1 */
    case SDLK_F2: return 283;   /* SDL 1.2 SDLK_F2 */
    case SDLK_F3: return 284;   /* SDL 1.2 SDLK_F3 */
    case SDLK_F4: return 285;   /* SDL 1.2 SDLK_F4 */
    case SDLK_F5: return 286;   /* SDL 1.2 SDLK_F5 */
    case SDLK_F6: return 287;   /* SDL 1.2 SDLK_F6 */
    case SDLK_F7: return 288;   /* SDL 1.2 SDLK_F7 */
    case SDLK_F8: return 289;   /* SDL 1.2 SDLK_F8 */
    case SDLK_F9: return 290;   /* SDL 1.2 SDLK_F9 */
    case SDLK_F10: return 291;   /* SDL 1.2 SDLK_F10 */
    case SDLK_F11: return 292;   /* SDL 1.2 SDLK_F11 */
    case SDLK_F12: return 293;   /* SDL 1.2 SDLK_F12 */
    case SDLK_F13: return 294;   /* SDL 1.2 SDLK_F13 */
    case SDLK_F14: return 295;   /* SDL 1.2 SDLK_F14 */
    case SDLK_F15: return 296;   /* SDL 1.2 SDLK_F15 */
    case SDLK_RSHIFT: return 303;   /* SDL 1.2 SDLK_RSHIFT */
    case SDLK_LSHIFT: return 304;   /* SDL 1.2 SDLK_LSHIFT */
    case SDLK_RCTRL: return 305;   /* SDL 1.2 SDLK_RCTRL */
    case SDLK_LCTRL: return 306;   /* SDL 1.2 SDLK_LCTRL */
    case SDLK_RALT: return 307;   /* SDL 1.2 SDLK_RALT */
    case SDLK_LALT: return 308;   /* SDL 1.2 SDLK_LALT */
    case SDLK_SYSREQ: return 317;   /* SDL 1.2 SDLK_SYSREQ */
    default:
        return 0;
    }
}

SDL_Keycode Sdl2_SdlKeyForEngineKey(int engine_key)
{
    if (engine_key > 0 && engine_key < 256) {
        /* SDLK_PAUSE is the exception: nineteen in SDL 1.2, but SDL2 spells it
         * scancode|SDLK_SCANCODE_MASK like the rest of the non-ASCII keys. */
        if (engine_key == 19) {
            return SDLK_PAUSE;
        }
        return (SDL_Keycode)engine_key;
    }
    switch (engine_key) {
    case 256: return SDLK_KP_0;
    case 257: return SDLK_KP_1;
    case 258: return SDLK_KP_2;
    case 259: return SDLK_KP_3;
    case 260: return SDLK_KP_4;
    case 261: return SDLK_KP_5;
    case 262: return SDLK_KP_6;
    case 263: return SDLK_KP_7;
    case 264: return SDLK_KP_8;
    case 265: return SDLK_KP_9;
    case 266: return SDLK_KP_PERIOD;
    case 267: return SDLK_KP_DIVIDE;
    case 268: return SDLK_KP_MULTIPLY;
    case 269: return SDLK_KP_MINUS;
    case 270: return SDLK_KP_PLUS;
    case 271: return SDLK_KP_ENTER;
    case 272: return SDLK_KP_EQUALS;
    case 273: return SDLK_UP;
    case 274: return SDLK_DOWN;
    case 275: return SDLK_RIGHT;
    case 276: return SDLK_LEFT;
    case 277: return SDLK_INSERT;
    case 278: return SDLK_HOME;
    case 279: return SDLK_END;
    case 280: return SDLK_PAGEUP;
    case 281: return SDLK_PAGEDOWN;
    case 282: return SDLK_F1;
    case 283: return SDLK_F2;
    case 284: return SDLK_F3;
    case 285: return SDLK_F4;
    case 286: return SDLK_F5;
    case 287: return SDLK_F6;
    case 288: return SDLK_F7;
    case 289: return SDLK_F8;
    case 290: return SDLK_F9;
    case 291: return SDLK_F10;
    case 292: return SDLK_F11;
    case 293: return SDLK_F12;
    case 294: return SDLK_F13;
    case 295: return SDLK_F14;
    case 296: return SDLK_F15;
    case 300: return SDLK_NUMLOCKCLEAR;
    case 301: return SDLK_CAPSLOCK;
    case 302: return SDLK_SCROLLLOCK;
    case 303: return SDLK_RSHIFT;
    case 304: return SDLK_LSHIFT;
    case 305: return SDLK_RCTRL;
    case 306: return SDLK_LCTRL;
    case 307: return SDLK_RALT;
    case 308: return SDLK_LALT;
    case 317: return SDLK_SYSREQ;
    default: return SDLK_UNKNOWN;
    }
}
