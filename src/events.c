/*
 * SDL event pump and input state (original module 0x46B210-0x46B79F).
 *
 * Key and joystick button changes reach Input_OnKey / Input_OnPadButton
 * through g_keyHandler / g_padHandler (installed by Events_Init), which keep
 * g_inputState and queue the changes in g_keyQueue for menus and key
 * configuration (Input_CodePending, Input_Translate*).
 */
/* <SDL.h> comes from mugen.h, which pulls it in after glad.h. */

#include "mugen.h"

#ifdef MUGEN_VITA_GL
/*
 * PS Vita port (port/psvita): make the gamepad drive the keyboard.
 *
 * M.U.G.E.N's menus are keyboard-only. Joystick input goes through
 * Pads_HandleEvent into the pad input codes (Input_TranslateJoy adds
 * INPUT_PAD_BASE), which the fight engine reads -- the system screens never
 * look at them. They read screen->key, which comes from the keyboard queue. On
 * a desktop that is fine: there is a keyboard with F1, Escape and arrows. The
 * Vita has none, so the first-run info box ("Press F1 to continue.") could not
 * be dismissed and the game sat there.
 *
 * The indices below are not guesses. SDL's Vita joystick driver
 * (SDL_sysjoystick.o in VitaSDK's libSDL.a) reports 12 buttons, 4 axes and
 * NO hats -- the d-pad arrives as ordinary buttons, not as a hat -- and its
 * update function maps SCE_CTRL bits to indices like this:
 *
 *     index  bit     button        index  bit     button
 *       0   0x1000   Triangle       6    0x40    Down
 *       1   0x2000   Circle         7    0x80    Left
 *       2   0x4000   Cross          8    0x10    Up
 *       3   0x8000   Square         9    0x20    Right
 *       4   0x100    L             10    0x1     Select
 *       5   0x200    R             11    0x8     Start
 *
 * Cross and Circle follow the console's own convention as configured on the
 * device: Cross = cancel/back, Circle = confirm. Getting this backwards makes
 * every menu feel wrong, so it is stated explicitly rather than inferred.
 */

#define VITA_PAD_TRIANGLE  0
#define VITA_PAD_CIRCLE    1
#define VITA_PAD_CROSS     2
#define VITA_PAD_SQUARE    3
#define VITA_PAD_L         4
#define VITA_PAD_R         5
#define VITA_PAD_DOWN      6
#define VITA_PAD_LEFT      7
#define VITA_PAD_UP        8
#define VITA_PAD_RIGHT     9
#define VITA_PAD_SELECT   10
#define VITA_PAD_START    11

/* How far the analog stick must move before it counts as a press. */
#define VITA_AXIS_DEADZONE 0x4000

/*
 * Character-select overrides.
 *
 * Every menu confirms on any A..Z bit of `screen->input`
 * (SCREEN_IN_BUTTONS = 0x7F0), so a pad button cannot mean two things while it
 * stays in joy_keys. Rather than weaken the fight bindings, the four buttons in
 * question are handled per screen:
 *
 *   in the character select   Cross and Square emit Escape (back), L and R emit
 *                             nothing, and Pads_HandleEvent is skipped for them
 *                             so they contribute no A..Z bit and cannot also
 *                             confirm;
 *   anywhere else             nothing changes: the pad channel supplies them
 *                             exactly as configured, so A/X/Y/Z all work in a
 *                             fight.
 *
 * Skipping the pad event is what makes "back" unambiguous: without it the press
 * would both leave the screen and confirm whatever was under the cursor.
 */
static int VitaInput_InCharSelect(void)
{
    return g_game != NULL && g_game->screen_kind == 2;
}

static int VitaInput_PadOverridden(int button)
{
    if (!VitaInput_InCharSelect()) {
        return 0;
    }
    return button == VITA_PAD_CROSS || button == VITA_PAD_SQUARE || button == VITA_PAD_L ||
           button == VITA_PAD_R;
}

static int g_vitaAxisX;   /* analog X direction currently held: -1, 0, 1 */
static int g_vitaAxisY;
static unsigned char g_vitaKeyLogged[16];   /* buttons whose mapping has been logged */

/* The keyboard key a pad button stands for, or 0 for none. */
static int VitaInput_KeyForButton(int button)
{
    switch (button) {
    case VITA_PAD_CIRCLE:
        /*
         * The built-in "C" key (SDLK_SLASH, 47), not Return.
         *
         * The two screens accept different masks. The title screen uses
         * TITLE_IN_BUTTONS (0xFF0), so any button bit confirms -- Return's
         * KEY_START (0x400) included. The character select instead has explicit
         * branches for 0x010..0x200 only, with no case for 0x400:
         *
         *     if (input & 0x010) result = 0;   // A
         *     ...
         *     if (input & 0x200) result = 5;   // Z
         *
         * In the original, Start is a *modifier* there (palettes 7..12), not an
         * accept. So Circle emitting Return confirmed the mode select and did
         * nothing in the character grid, which is exactly the reported symptom.
         *
         * Emitting C gives 0x040, which both masks accept, and matches the
         * conventional pad layout (Square/Triangle/Circle/Cross = A/B/C/X), so
         * Circle is also the C attack in a fight.
         */
        return SDLK_SLASH;
    case VITA_PAD_START:
        return SDLK_RETURN;
    case VITA_PAD_CROSS:
    case VITA_PAD_SQUARE:
        /*
         * Back, in the character select only.
         *
         * SDLK_ESCAPE is the engine's cancel key and the only way to leave a
         * screen: Screen_ReadInput sets `screen->done = 1` on 27. Everywhere
         * else these buttons return 0 here and fall through to the pad channel,
         * which still supplies Cross = X and Square = A in a fight.
         */
        return VitaInput_InCharSelect() ? SDLK_ESCAPE : 0;
    case VITA_PAD_SELECT:
        /*
         * Cancel/back lives on Select, not Cross.
         *
         * M.U.G.E.N has no "back" key: Escape is the cancel key, and on the
         * title screen `Title_Update` turns Escape into "choose Exit" --
         *
         *     if (key == SDLK_ESCAPE || g_quitRequested) {
         *         st->item = TITLE_ITEM_EXIT;
         *
         * so mapping Cross to Escape made Cross quit the game from the mode
         * select, which is worse than having no cancel button there. Cross is
         * the X attack in a fight, and it stays unbound in menus.
         */
        return SDLK_ESCAPE;
    case VITA_PAD_UP:
        return SDLK_UP;
    case VITA_PAD_DOWN:
        return SDLK_DOWN;
    case VITA_PAD_LEFT:
        return SDLK_LEFT;
    case VITA_PAD_RIGHT:
        return SDLK_RIGHT;
    default:
        /*
         * Triangle, Square and L/R are unbound.
         *
         * Triangle used to map to F1, which opens the title screen's info box.
         * That was removed on request: on a console the info panel interrupts
         * the mode select, and the flag that opens it ([Config] FirstRun) is
         * now packed as 0 as well. Square and L/R have no menu function in
         * M.U.G.E.N at all; binding something arbitrary is worse than leaving
         * them inert. All four still work in a fight (A/B/Y/Z) through the
         * joystick bindings in src/config.c.
         */
        return 0;
    }
}

/* One axis of the analog stick as a direction, edge triggered. */
static void VitaInput_SetAxis(int *held, int value, int negative, int positive)
{
    int dir = 0;

    if (value < -VITA_AXIS_DEADZONE) {
        dir = -1;
    } else if (value > VITA_AXIS_DEADZONE) {
        dir = 1;
    }
    if (dir != *held) {
        if (*held == -1) {
            Keyboard_HandleKey(negative, 0);
        } else if (*held == 1) {
            Keyboard_HandleKey(positive, 0);
        }
        if (dir == -1) {
            Keyboard_HandleKey(negative, 1);
        } else if (dir == 1) {
            Keyboard_HandleKey(positive, 1);
        }
        *held = dir;
    }
}

/* Turns a joystick event into keyboard events. */
static void VitaInput_TranslateEvent(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int button = ev->jbutton.button;
        int pressed = ev->type == SDL_JOYBUTTONDOWN;
        int key = VitaInput_KeyForButton(button);

        /* Log each button's key once, so mugen.log shows that the bridge
         * actually fired for a given button rather than leaving "the button
         * does nothing" to guesswork. */
        if (pressed && button >= 0 && button < (int)sizeof(g_vitaKeyLogged) && g_vitaKeyLogged[button] == 0) {
            g_vitaKeyLogged[button] = 1;
            Log_Printf("pad: button %d -> key %d\n", button, key);
        }
        if (key != 0) {
            Keyboard_HandleKey(key, pressed);
        }
        break;
    }
    case SDL_JOYAXISMOTION:
        /* Axes 0 and 1 are the left stick (LX, LY). The right stick, 2 and 3,
         * is left alone: it has no menu meaning and would only fight the
         * player's thumb on the left stick. */
        if (ev->jaxis.axis == 0) {
            VitaInput_SetAxis(&g_vitaAxisX, ev->jaxis.value, SDLK_LEFT, SDLK_RIGHT);
        } else if (ev->jaxis.axis == 1) {
            VitaInput_SetAxis(&g_vitaAxisY, ev->jaxis.value, SDLK_UP, SDLK_DOWN);
        }
        break;
    default:
        break;
    }
}
#endif /* MUGEN_VITA_GL */

/* 0x0046B210: handles every pending SDL event. */
void Events_Poll(void)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
#ifdef ANDROID_PORT
        /*
         * Android port: SDL_ACTIVEEVENT is gone. SDL2 reports the same thing as
         * SDL_APP_WILLENTERBACKGROUND / SDL_APP_DIDENTERFOREGROUND /
         * SDL_APP_TERMINATING. Only the focus callback and the mod-state refresh
         * are reproduced; SDL_ShowCursor has no meaning on a touch device.
         */
        case SDL_APP_WILLENTERBACKGROUND:
            if (g_onActivate != NULL) {
                g_onActivate(0);
            }
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            g_keyModState = SDL_GetModState();
            if (g_onActivate != NULL) {
                g_onActivate(1);
            }
            break;
#elif defined(MUGEN_SDL2)
        /*
         * SDL2 desktop: SDL_ACTIVEEVENT and SDL_VIDEORESIZE became
         * SDL_WINDOWEVENT sub-types. The focus case reproduces the SDL 1.2
         * block below exactly -- mod state, cursor visibility, the engine
         * callback -- because those are what the original reacts to; the resize
         * case feeds the same Video_OnResize the original's resize event did.
         */
        case SDL_WINDOWEVENT:
            switch (ev.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                Video_OnResize(ev.window.data1, ev.window.data2);
                break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:
            case SDL_WINDOWEVENT_FOCUS_LOST: {
                int gain = ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;

                if (gain) {
                    g_keyModState = SDL_GetModState();
                }
                SDL_ShowCursor((g_videoSdlFlags & SDL_FULLSCREEN) && gain ? SDL_DISABLE : SDL_ENABLE);
                if (g_onActivate != NULL) {
                    g_onActivate(gain);
                }
                break;
            }
            default:
                break;
            }
            break;
#else
        case SDL_ACTIVEEVENT:
            if (ev.active.state & SDL_APPINPUTFOCUS) {
                int gain = ev.active.gain;
                if (gain) {
                    g_keyModState = SDL_GetModState();
                }
                SDL_ShowCursor((g_videoSdlFlags & SDL_FULLSCREEN) && gain ? SDL_DISABLE : SDL_ENABLE);
                if (g_onActivate != NULL) {
                    g_onActivate(gain);
                }
            }
            break;
#endif
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            /*
             * The engine's key identifiers are SDL 1.2's keycodes (SDLK_UP is
             * 273 and indexes g_keyState). SDL2's are scancode|0x40000000 for
             * every non-printable key, so an unconverted event would write
             * hundreds of megabytes past the array. Sdl2_EngineKey() is the one
             * conversion, and 0 means "a key SDL 1.2 did not have".
             */
#ifdef MUGEN_SDL2
            int engine_key = Sdl2_EngineKey(ev.key.keysym.sym);
#else
            int engine_key = ev.key.keysym.sym;
#endif
            if (engine_key == 0) {
                break;
            }
            Keyboard_HandleKey(engine_key, ev.type == SDL_KEYDOWN);
            if (g_keyEventHandler != NULL) {
#if defined(MUGEN_SDL2_API)
                /*
                 * SDL2 moved the unicode character out of SDL_Keysym into
                 * SDL_TextInputEvent, and SDL_KEYDOWN no longer carries it.
                 * Feeding the console twice would double every typed
                 * character, so the two halves are kept apart: control keys
                 * (everything outside printable ASCII) are delivered here, and
                 * printable characters arrive through SDL_TEXTINPUT below.
                 *
                 * SDL 1.2 delivered both in one event; for ASCII,
                 * keysym.sym == unicode, which is why the text-input branch
                 * passes the codepoint as the key too.
                 */
                int sym = engine_key;

                if (sym < 0x20 || sym > 0x7E) {
                    g_keyEventHandler(0, sym, ev.type == SDL_KEYDOWN);
                }
#else
                /* The original passes the dword at the unicode field, padding included. */
                unsigned int unicode;
                memcpy(&unicode, &ev.key.keysym.unicode, sizeof(unicode));
                g_keyEventHandler(unicode, engine_key, ev.type == SDL_KEYDOWN);
#endif
            }
            break;
        }
#ifdef MUGEN_SDL2
        /*
         * SDL2 delivers typed text separately from the key event. Decoding the
         * first UTF-8 sequence reproduces what SDL 1.2 put in keysym.unicode:
         * a single codepoint, which is what the console indexes by
         * (Console_HandleKey truncates it to 16 bits for its line buffer).
         */
        case SDL_TEXTINPUT:
            if (g_keyEventHandler != NULL) {
                /*
                 * SDL2 allows several codepoints in one SDL_TEXTINPUT (it only
                 * splits at SDL_TEXTINPUTEVENT_TEXT_SIZE), and it never splits
                 * in the middle of a sequence. Decoding just the first one lost
                 * "b" from a payload like "ab" -- fast typing, an IME commit or
                 * a paste all produce those. Each codepoint goes to the console
                 * separately, which is what SDL 1.2 effectively did by sending
                 * one event per keystroke.
                 */
                const unsigned char *s = (const unsigned char *)ev.text.text;

                while (*s != '\0') {
                    unsigned int cp = 0;
                    int extra = 0;
                    int i;

                    if (*s < 0x80) {
                        cp = *s;
                    } else if ((*s & 0xE0) == 0xC0) {
                        cp = (unsigned int)(*s & 0x1F);
                        extra = 1;
                    } else if ((*s & 0xF0) == 0xE0) {
                        cp = (unsigned int)(*s & 0x0F);
                        extra = 2;
                    } else if ((*s & 0xF8) == 0xF0) {
                        cp = (unsigned int)(*s & 0x07);
                        extra = 3;
                    } else {
                        /* A continuation byte with no lead byte: skip it rather
                         * than spinning on it. */
                        s++;
                        continue;
                    }
                    s++;
                    for (i = 0; i < extra; i++) {
                        if (*s == '\0') {
                            break;      /* truncated sequence: SDL2 promises not
                                         * to, but do not read past the buffer */
                        }
                        cp = (cp << 6) | (unsigned int)(*s & 0x3F);
                        s++;
                    }
                    if (cp != 0) {
                        g_keyEventHandler(cp, (int)cp, 1);
                    }
                }
            }
            break;
#endif
        case SDL_JOYAXISMOTION:
        case SDL_JOYHATMOTION:
            Pads_HandleEvent(&ev);
#ifdef MUGEN_VITA_GL
            VitaInput_TranslateEvent(&ev);
#endif
            break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
#ifdef MUGEN_VITA_GL
            /*
             * PS Vita port: in the character select, Cross/Square are "back" and
             * L/R do nothing, so their pad code is withheld -- otherwise the
             * A..Z bit they carry would confirm as well. See
             * VitaInput_PadOverridden.
             */
            if (!VitaInput_PadOverridden(ev.jbutton.button))
#endif
            {
                Pads_HandleEvent(&ev);
            }
#ifdef MUGEN_VITA_GL
            /* PS Vita port: the pads also drive the keyboard menus; see the
             * VitaInput_TranslateEvent comment at the top of this file. */
            VitaInput_TranslateEvent(&ev);
#endif
            break;
        case SDL_QUIT:
            g_quitRequested = 1;
            break;
#if !defined(ANDROID_PORT) && !defined(MUGEN_SDL2)
        /* Android has no resizable window: SDL_VIDEORESIZE does not exist and
         * the surface size is fixed by SetVideoMode. SDL2 has SDL_WINDOWEVENT
         * instead, handled at the top of this loop. */
        case SDL_VIDEORESIZE:
            Video_OnResize(ev.resize.w, ev.resize.h);
            break;
#endif
        }
    }
}

/* Queues the code of an input that went from released to held (or back). */
static void Input_Changed(int code, int old_state)
{
    if ((g_inputState[code] == 0) != (old_state == 0)) {
        if (g_inputState[code] == 0) {
            code |= INPUT_RELEASE;
        }
        Input_QueueCode(code);
    }
}

/* 0x0046B350: g_keyHandler. Keys remapped to 0 are ignored. */
void Input_OnKey(int key, int pressed)
{
    int code = g_keyMap != NULL ? g_keyMap[key] : key;
    int old;

    if (code == 0) {
        return;
    }
    old = (signed char)g_inputState[code];
    if (pressed) {
        g_inputState[code] |= INPUT_STATE_KEY;
    } else {
        g_inputState[code] &= ~INPUT_STATE_KEY;
    }
    Input_Changed(code, old);
}

/* 0x0046B3D0: g_padHandler. */
void Input_OnPadButton(int button, int pressed)
{
    int code = button + INPUT_PAD_BASE;
    int old;

    if (g_keyMap != NULL) {
        code = g_keyMap[code];
    }
    old = (signed char)g_inputState[code];
    if (pressed) {
        g_inputState[code] |= INPUT_STATE_PAD;
    } else {
        g_inputState[code] &= ~INPUT_STATE_PAD;
    }
    Input_Changed(code, old);
}

/*
 * 0x0046B450: creates the key queue and installs the input handlers. Fails
 * (0) when a handler is already installed, after installing the key handler
 * if only the pad handler was there.
 */
int Events_Init(void)
{
    if (g_keyQueue.data == NULL && !IntArray_Init(&g_keyQueue, INPUT_QUEUE_SIZE)) {
        return 0;
    }
    if (g_keyHandler != NULL) {
        return 0;
    }
    g_keyHandler = Input_OnKey;
    if (g_padHandler != NULL) {
        return 0;
    }
    g_padHandler = Input_OnPadButton;
    memset(g_inputState, 0, sizeof(g_inputState));
    g_keyQueue.write = 0;
    g_keyQueue.read = 0;
    Input_ResetState();
    return 1;
}

/* 0x0046B500: removes the handlers and frees the key map and the key queue. */
void Events_Deinit(void)
{
    int *map = g_keyMap;

    g_keyHandler = NULL;
    g_padHandler = NULL;
    if (map != NULL) {
        g_keyMap = NULL;
        free(map);
    }
    if (g_keyQueue.data != NULL) {
        free(g_keyQueue.data);
    }
    g_keyQueue.data = NULL;
    g_keyQueue.read = 0;
    g_keyQueue.write = 0;
    g_keyQueue.capacity = 0;
}

/* 0x0046B560: rebuilds g_inputState from the held keys and buttons (focus regained, remapping). */
void Input_ResetState(void)
{
    int i;

    memset(g_inputState, 0, sizeof(g_inputState));
    if (g_keyMap == NULL) {
        for (i = 1; i < INPUT_KEY_COUNT; i++) {
            g_inputState[i] |= g_keyState[i] != 0;
        }
        for (i = 0; i < INPUT_PAD_COUNT; i++) {
            g_inputState[INPUT_PAD_BASE + i] |= g_padButtonState[i] != 0 ? INPUT_STATE_PAD : 0;
        }
        return;
    }
    for (i = 1; i < INPUT_KEY_COUNT; i++) {
        g_inputState[g_keyMap[i]] |= g_keyState[i] != 0;
    }
    for (i = 0; i < INPUT_PAD_COUNT; i++) {
        g_inputState[g_keyMap[INPUT_PAD_BASE + i]] |= g_padButtonState[i] != 0 ? INPUT_STATE_PAD : 0;
    }
}

/*
 * 0x0046B670: appends an input code to g_keyQueue (only while the keyboard
 * or the pads are active); when full, the oldest code is dropped.
 */
void Input_QueueCode(int code)
{
    if (!g_padsActive && !g_keyboardActive) {
        return;
    }
    if (code == 0) {
        return;
    }
    g_keyQueue.data[g_keyQueue.write] = code;
    g_keyQueue.write++;
    if (g_keyQueue.write >= g_keyQueue.capacity) {
        g_keyQueue.write = 0;
    }
    if (g_keyQueue.write == g_keyQueue.read) {
        g_keyQueue.read++;
        if (g_keyQueue.read >= g_keyQueue.capacity) {
            g_keyQueue.read = 0;
        }
    }
}

/* 0x0046B6E0: 1 if g_keyQueue holds a code. */
int Input_CodePending(void)
{
    if (!g_padsActive && !g_keyboardActive) {
        return 0;
    }
    return g_keyQueue.read != g_keyQueue.write;
}

/*
 * 0x0046B710: keyboard code with release bit 0x200 to an input code:
 * INPUT_NONE for no key, INPUT_RELEASE for a release.
 */
int Input_TranslateKey(int key)
{
    int code = key & ~0x200;
    if (code == 0x200 || code == 0) {   /* the first test can never hold */
        return INPUT_NONE;
    }
    if (key & 0x200) {
        return code | INPUT_RELEASE;
    }
    return code;
}

/* 0x0046B740: joystick button with release bit 0x100 (JOY_NONE = none) to an input code. */
int Input_TranslateJoy(int button)
{
    int code = button & ~0x100;

    if (code == JOY_NONE) {
        return INPUT_NONE;
    }
    if (code != 0) {
        code += INPUT_PAD_BASE;
        if (button & 0x100) {
            code |= INPUT_RELEASE;
        }
    }
    return code;
}

/* 0x0046B770: an empty array with room for capacity ints; 0 when out of memory. */
int IntArray_Init(IntArray *a, int capacity)
{
    a->data = NULL;
    a->read = 0;
    a->write = 0;
    a->capacity = 0;
    a->data = calloc(1, capacity * sizeof(int));
    if (a->data == NULL) {
        return 0;
    }
    a->capacity = capacity;
    return 1;
}
