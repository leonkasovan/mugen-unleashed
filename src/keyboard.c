/*
 * Keyboard (original module 0x46E290-0x46E42F): the state of every key
 * (g_keyState), the keyboard code queue that menus read (g_keyboardQueue)
 * and the SDL key event callback of the console.
 *
 * Key changes reach the input engine through g_keyHandler (Input_OnKey).
 * The pause key toggles g_pauseToggled on each press. Shutdown is inlined in
 * System_Deinit.
 */
/* <SDL.h> comes from mugen.h, which pulls it in after glad.h. */

#include "mugen.h"
#include "sdl2keys.h"

/* Port fix: physical Return state is separate from gameplay state. A consumed
 * chord stays consumed until Return-up, even if Alt is released first. */
static int g_fullscreenReturnHeld;
static int g_fullscreenReturnConsumed;

/*
 * 0x0046E290: a key went down (pressed != 0) or up. Key 0 is ignored. The
 * change is queued, releases with KEYBOARD_RELEASE unless g_keyboardNoReleases
 * (then only presses are queued).
 */
void Keyboard_HandleKey(int key, int pressed)
{
    /* Android port fix: OS keycodes must never escape the configuration ID space. */
#ifdef ANDROID_PORT
    if (key <= 0 || key >= INPUT_KEY_COUNT || key >= (int)sizeof(g_keyState)) {
#else
    if (key == 0) {
#endif
        return;
    }
    g_keyModState = SDL_GetModState();
    if (key == SDLK_RETURN) {
        if (!pressed) {
            g_fullscreenReturnHeld = 0;
            if (g_fullscreenReturnConsumed) {
                g_fullscreenReturnConsumed = 0;
                return;
            }
        } else if (!g_fullscreenReturnHeld) {
            g_fullscreenReturnHeld = 1;
            if (g_game != NULL && !g_game->safe_mode &&
                (g_keyState[ENGINE_KEY_LALT] || g_keyState[ENGINE_KEY_RALT])) {
                int shortcut = SDLK_RETURN;
                g_fullscreenReturnConsumed = 1;
                /* SDL's event pump runs on the video thread. Capture before
                 * g_keyHandler updates g_inputState and queues menu/gameplay. */
                Video_HandleFullscreenKey(&shortcut);
                return;
            }
        } else if (g_fullscreenReturnConsumed) {
            return;
        }
    }
    if (key == KEY_PAUSE_TOGGLE && pressed) {
        g_pauseToggled ^= 1;
    }
    g_keyState[key] = (unsigned char)pressed;
    if (g_keyboardNoReleases == 0) {
        Keyboard_QueueCode(pressed ? key : key | KEYBOARD_RELEASE);
    } else if (pressed) {
        Keyboard_QueueCode(key);
    }
    if (g_keyHandler != NULL) {
        g_keyHandler(key, pressed);
    }
}

/*
 * 0x0046E2F0: clears the key state, the key handler and the queue. Returns 0
 * when the keyboard is already active. Key repeat is turned off unless
 * g_keyboardKeepRepeat is set.
 */
int Keyboard_Init(void)
{
    if (g_keyboardActive != 0) {
        return 0;
    }
    memset(g_keyState, 0, sizeof(g_keyState));
    g_fullscreenReturnHeld = g_fullscreenReturnConsumed = 0;
    g_keyHandler = NULL;
    /* SDL2 repeat is delivered in events; there is no global repeat switch. */
    g_keyboardQueueWrite = 0;
    g_keyboardQueueRead = 0;
    g_keyboardActive = 1;
    return 1;
}

/*
 * 0x0046E350: copies SDL's key state into g_keyState (focus regained). At
 * most 0x143 keys are copied: the "more than 0x200" clamp can never apply.
 */
void Keyboard_SyncState(void)
{
    int count;
    const Uint8 *state = SDL_GetKeyboardState(&count);
    int i;

    /* Both SDL2 targets use the shared inverse config-ID table. Native SDL2
     * keyboard state is indexed by scancode, never by the engine key number. */
    memset(g_keyState, 0, sizeof(g_keyState));
    for (i = 1; i < INPUT_KEY_COUNT && i < (int)sizeof(g_keyState); i++) {
        SDL_Keycode key = Sdl2_SdlKeyForEngineKey(i);
        SDL_Scancode sc = SDL_GetScancodeFromKey(key);

        if (sc != SDL_SCANCODE_UNKNOWN && (int)sc < count) {
            g_keyState[i] = state[sc];
        }
    }
    g_fullscreenReturnHeld = g_keyState[SDLK_RETURN] != 0;
    if (!g_fullscreenReturnHeld) g_fullscreenReturnConsumed = 0;
    if (g_fullscreenReturnConsumed) g_keyState[SDLK_RETURN] = 0;

}

/*
 * 0x0046E3B0: installs the callback that receives every key event (the
 * console). Fails (0) when one is installed and fn is not NULL; NULL
 * removes it. SDL's unicode translation is turned on for a callback (and
 * never off again).
 */
int Keyboard_SetEventHandler(KeyEventFn fn)
{
    if (g_keyEventHandler != NULL && fn != NULL) {
        return 0;
    }
    g_keyEventHandler = fn;
#ifdef ANDROID_PORT
    if (fn != NULL) {
        /* Keep physical text/debug bindings active without opening the mobile
         * IME at startup. Console_Toggle enables it while editing. */
        SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
#else
    if (fn != NULL) {
        SDL_StartTextInput();
    }
#endif
    return 1;
}

/* 0x0046E3E0: appends a code to g_keyboardQueue, dropping the oldest one when full. */
void Keyboard_QueueCode(int code)
{
    g_keyboardQueue[g_keyboardQueueWrite] = code;
    g_keyboardQueueWrite++;
    if (g_keyboardQueueWrite >= INPUT_RING_SIZE) {
        g_keyboardQueueWrite = 0;
    }
    if (g_keyboardQueueWrite == g_keyboardQueueRead) {
        g_keyboardQueueRead++;
        if (g_keyboardQueueRead >= INPUT_RING_SIZE) {
            g_keyboardQueueRead = 0;
        }
    }
}
