/*
 * Runtime tests for the SDL2 input paths that a headless log comparison cannot
 * reach: pad-slot mapping, key translation through a real event queue, and
 * UTF-8 text decoding into the console's unicode value.
 *
 * Built and run by tools/test_sdl2input.sh. Links the engine's pads.c,
 * keyboard.c and events.c together with the shim, so these are the real
 * functions, not copies. It runs against a virtual joystick, so no hardware is
 * needed.
 */
#include "mugen.h"

#include <stdio.h>
#include <string.h>

/* The engine's globals live in globals.c, which pulls in the whole engine;
 * these tests only need a handful, so they are defined here instead. The
 * declarations below match symbols.h exactly. */
unsigned char g_padButtonState[INPUT_PAD_COUNT + 1];
unsigned char g_inputState[INPUT_CODE_COUNT];
unsigned char g_keyState[0x201];
int *g_keyMap;
Pad g_pads[PAD_MAX];
int g_padCount;
int g_padsActive;
int g_keyboardActive;
int g_keyboardKeepRepeat;
int g_keyboardNoReleases;
int g_keyboardQueueWrite;
int g_keyboardQueueRead;
int g_keyboardQueue[INPUT_RING_SIZE];
int g_padQueue[INPUT_RING_SIZE];
int g_padQueueWrite;
int g_padQueueRead;
int g_keyModState;
char g_pauseToggled;
KeyEventFn g_keyEventHandler;
IntArray g_keyQueue;
InputHandlerFn g_keyHandler;
InputHandlerFn g_padHandler;
int g_padsChanged;
Console *g_console;
GameVars *g_game;
int g_outOfMemory;
Uint32 g_videoSdlFlags;
ActivateFn g_onActivate;
int g_quitRequested;

/* Called by Events_Poll on a window resize; nothing in these tests resizes. */
void Video_OnResize(int width, int height)
{
    (void)width;
    (void)height;
}

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* pads.c and events.c call these; they are the engine's logging and code
 * queueing, which these tests do not exercise. */
void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

void MatchLog_Printf(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

/* ------------------------------------------------------------------ pads */

/*
 * SDL2 replaced SDL 1.2's device index in joystick events with an instance id
 * that has no relation to the pad slot. The engine indexes g_pads[] with the
 * slot, so an unmapped instance id writes far outside the table. This runs the
 * real Pads_HandleEvent against a virtual joystick whose instance id is
 * deliberately not 0, and checks the right slot received the button.
 */
static void test_pad_slot_mapping(void)
{
    SDL_Event ev;
    SDL_JoystickID id;
    int code0;
    int code1;

    SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);

    /*
     * Virtual pads for Pads_Init to open.
     *
     * Three, not two, and that matters: SDL numbers virtual instance ids from
     * 0, so on a clean process instance id == device index and an index-based
     * lookup looks correct. Attaching an extra pad at the start and closing it
     * forces the ids to shift, which is what makes this test able to tell the
     * mapping from indexing at all.
     */
    if (SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 4, 12, 1) < 0 ||
        SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 4, 12, 1) < 0 ||
        SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 4, 12, 1) < 0) {
        printf("SKIP: SDL_JoystickAttachVirtual unavailable (%s)\n", SDL_GetError());
        return;
    }
    /*
     * Drop the first device so the remaining ones keep their higher instance
     * ids while their device indices restart at 0. An engine that indexes by
     * `which` then picks the wrong pad.
     */
    if (SDL_JoystickDetachVirtual(0) != 0) {
        printf("SKIP: SDL_JoystickDetachVirtual unavailable (%s)\n", SDL_GetError());
        return;
    }
    memset(g_pads, 0, sizeof(g_pads));
    g_padCount = 0;
    g_padsActive = 0;
    if (Pads_Init() == 0) {
        printf("SKIP: Pads_Init found no joystick (%s)\n", SDL_GetError());
        return;
    }
    check(g_padCount == 2, "Pads_Init opened both remaining virtual joysticks");
    check(g_pads[0].joystick != NULL && g_pads[1].joystick != NULL, "both pad slots hold a handle");

    /*
     * The whole point: the second pad's instance id must arrive on slot 1.
     * Under SDL 1.2 `which` was the index, so this used to be free; under SDL2
     * it is an id that has nothing to do with the slot.
     */
    id = SDL_JoystickInstanceID(g_pads[1].joystick);
    printf("  pad 0 instance id %d, pad 1 instance id %d\n",
           (int)SDL_JoystickInstanceID(g_pads[0].joystick), (int)id);

    code0 = 0 * PAD_BUTTONS + g_pads[0].button_base + 1;
    code1 = 1 * PAD_BUTTONS + g_pads[1].button_base + 1;
    printf("  button codes: pad 0 -> %d, pad 1 -> %d\n", code0, code1);
    check(code0 != code1, "the two pads use different button codes");
    /*
     * The precondition that gives the test its power: the instance id of the
     * second pad must not be its index.
     */
    check(id != 1, "pad 1's instance id differs from its index (test precondition)");
    check(code1 < (int)sizeof(g_padButtonState), "the computed code is inside g_padButtonState");

    /* Pad 1 first, before anything else has touched the state. */
    memset(g_padButtonState, 0, sizeof(g_padButtonState));
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_JOYBUTTONDOWN;
    ev.jbutton.which = id;
    ev.jbutton.button = 0;
    ev.jbutton.state = SDL_PRESSED;
    Pads_HandleEvent(&ev);
    check(g_padButtonState[code1] == 1, "pad 1's button landed on pad 1's code");
    check(g_padButtonState[code0] == 0, "pad 1's button did NOT land on pad 0's code");

    /* Then pad 0, same button number: it must go to pad 0's code. */
    memset(g_padButtonState, 0, sizeof(g_padButtonState));
    ev.jbutton.which = SDL_JoystickInstanceID(g_pads[0].joystick);
    Pads_HandleEvent(&ev);
    check(g_padButtonState[code0] == 1, "pad 0's button landed on pad 0's code");
    check(g_padButtonState[code1] == 0, "pad 0's button did NOT land on pad 1's code");

    /* An event from a joystick the engine does not own must be dropped, not
     * indexed with. */
    ev.jbutton.which = 0x7FFFFFF0;
    Pads_HandleEvent(&ev);
    check(g_padButtonState[code0] == 1, "an unknown instance id did not disturb the state");

}

/* -------------------------------------------------------------- keyboard */

/*
 * Keyboard_HandleKey is what events.c calls with the translated key. The
 * translation itself is covered by tools/test_sdl2shim.sh; this checks that
 * the engine's storage takes an SDL 1.2-numbered special key without writing
 * outside g_keyState.
 */
static int calls;
static int last_key;

static void __cdecl KeySink(int key, int pressed)
{
    (void)pressed;
    calls++;
    last_key = key;
}

static int text_sink_calls;
static unsigned int text_sink_unicode;
static int text_sink_key;



static void __cdecl TextSink(unsigned int unicode, int key, int pressed)
{
    (void)pressed;
    text_sink_calls++;
    text_sink_unicode = unicode;
    text_sink_key = key;
}

static void test_key_delivery(void)
{
    int before = g_keyboardQueueWrite;

    memset(g_keyState, 0, sizeof(g_keyState));
    g_keyboardActive = 1;
    g_keyboardNoReleases = 0;
    g_keyHandler = NULL;

    /* SDL 1.2's numbers for Up (273) and F1 (282): inside the array. */
    Keyboard_HandleKey(273, 1);
    check(g_keyState[273] == 1, "Up set its state inside g_keyState");
    Keyboard_HandleKey(273, 0);
    check(g_keyState[273] == 0, "Up cleared its state");
    Keyboard_HandleKey(282, 1);
    check(g_keyState[282] == 1, "F1 set its state inside g_keyState");
    Keyboard_HandleKey(282, 0);
    check(g_keyboardQueueWrite != before, "key presses reached the key queue");

    /*
     * Keyboard_HandleKey feeds the input engine through g_keyHandler. The
     * console callback (g_keyEventHandler) is driven by Events_Poll instead,
     * because SDL2 splits control keys from text; that is covered by
     * test_text_input below.
     */
    calls = 0;
    g_keyHandler = KeySink;
    Keyboard_HandleKey(273, 1);
    Keyboard_HandleKey(273, 0);
    check(calls == 2, "the input engine saw both press and release");
    check(last_key == 273, "the input engine received the engine key, not an SDL2 code");
    g_keyHandler = NULL;
}

/* ------------------------------------------------------------------ text */

/*
 * SDL2 delivers typed characters as SDL_TEXTINPUT with UTF-8 text; SDL 1.2 put
 * a single codepoint in keysym.unicode. events.c decodes the first sequence.
 * The decode lives in Events_Poll's switch, so it is exercised with the real
 * event queue.
 */
static void push_and_poll_text(const char *utf8)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_TEXTINPUT;
    SDL_strlcpy(ev.text.text, utf8, sizeof(ev.text.text));
    SDL_PushEvent(&ev);
    Events_Poll();
}

static void test_text_input(void)
{
    text_sink_calls = 0;
    text_sink_unicode = 0;
    text_sink_key = 0;
    Keyboard_SetEventHandler(TextSink);
    g_keyEventHandler = TextSink;

    push_and_poll_text("a");
    check(text_sink_calls == 1, "ASCII text input reached the console once");
    check(text_sink_unicode == 'a', "ASCII text decoded to its codepoint");

    text_sink_calls = 0;
    push_and_poll_text("\xC3\xA9");           /* U+00E9 e-acute, 2 bytes */
    check(text_sink_calls == 1, "2-byte UTF-8 reached the console once");
    check(text_sink_unicode == 0xE9, "2-byte UTF-8 decoded to U+00E9");

    text_sink_calls = 0;
    push_and_poll_text("\xE2\x82\xAC");       /* U+20AC euro, 3 bytes */
    check(text_sink_unicode == 0x20AC, "3-byte UTF-8 decoded to U+20AC");

    text_sink_calls = 0;
    push_and_poll_text("\xF0\x9F\x98\x80");   /* U+1F600, 4 bytes */
    check(text_sink_unicode == 0x1F600, "4-byte UTF-8 decoded to U+1F600");

    g_keyEventHandler = NULL;
    Keyboard_SetEventHandler(NULL);
}


/* --------------------------------------------------------------- console */

/*
 * The real console, driven the way the game drives it: an SDL event is queued,
 * Events_Poll() runs, and the console's own handler edits its line buffer.
 *
 * These are the integration cases the component tests cannot see. The console
 * compares `key` against SDL 1.2 keycodes (273 = Up) while SDL2 spells SDLK_UP
 * as scancode|0x40000000, and it reads Backspace/Enter from the unicode field,
 * which SDL2 no longer fills in for control keys.
 */
static void console_type(const char *utf8)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_TEXTINPUT;
    SDL_strlcpy(ev.text.text, utf8, sizeof(ev.text.text));
    SDL_PushEvent(&ev);
    Events_Poll();
}

static void console_key(int engine_key)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = (SDL_Keycode)Sdl2_SdlKeyForEngineKey(engine_key);
    ev.key.keysym.scancode = SDL_GetScancodeFromKey((SDL_Keycode)Sdl2_SdlKeyForEngineKey(engine_key));
    SDL_PushEvent(&ev);
    Events_Poll();
    ev.type = SDL_KEYUP;
    SDL_PushEvent(&ev);
    Events_Poll();
}

static const char *console_line(void)
{
    return g_console != NULL ? g_console->input.text->buf : "";
}

static void test_console_integration(void)
{
    /*
     * Console_Create() also registers the debug commands, which needs the whole
     * game. What the key handler actually touches is the input line buffer, the
     * view and the GameVars flags, so the console is built from those.
     *
     * GameVars first: ConsoleView_Init() reads g_game->font_medium.
     */
    g_game = (GameVars *)calloc(1, sizeof(GameVars));
    g_console = (Console *)calloc(1, sizeof(Console));
    if (g_game == NULL || g_console == NULL || !ConsoleInput_Init(&g_console->input) ||
        !ConsoleView_Init(&g_console->view)) {
        printf("SKIP: could not build a console\n");
        return;
    }
    /* Console_Create() allocates these; the key handler uses scratch to build
     * each inserted character and the view to record the echoed line. */
    g_console->scratch = ConText_Create(0x202);
    g_console->bindings = KeybindList_Create();
    /* Enter submission appends the submitted line to the pending-command list,
     * which Console_Create() otherwise allocates. */
    g_console->pending = (DynArray *)calloc(1, sizeof(DynArray));
    if (g_console->pending != NULL) {
        g_console->pending->data = calloc(0x40, 4);
        g_console->pending->capacity = 0x40;
        g_console->pending->elem_size = 4;
    }
    if (g_console->scratch == NULL || g_console->bindings == NULL || g_console->pending == NULL) {
        printf("SKIP: could not finish the console\n");
        return;
    }
    if (g_console->bindings == NULL) {
        printf("SKIP: could not create the binding list\n");
        return;
    }
    /* Collect the console's key events the way the game does. */
    Keyboard_SetEventHandler(Console_HandleKey);
    g_keyEventHandler = Console_HandleKey;

    /* Open the console. */
    if (g_console->open == 0) {
        Console_Toggle();
    }
    check(g_console->open != 0, "the console opened");
    /* ConsoleInput_Init() already points the cursor at the input text; just
     * clear the line and put the cursor back at the start. */
    ConText_Set(g_console->input.text, "");
    g_console->input.cursor.chars = 0;
    g_console->input.cursor.bytes = 0;

    /* Typing through SDL_TEXTINPUT reaches the line buffer. */
    console_type("ab");
    check(strcmp(console_line(), "ab") == 0, "typing \"ab\" in one event put both characters on the line");

    /* Backspace removes the last character. SDL2 sends it as a key with
     * unicode 0; the console used to look for unicode 8. */
    console_key(8 /* SDL 1.2 SDLK_BACKSPACE */);
    check(strcmp(console_line(), "a") == 0, "Backspace deleted the last character");

    /* Home/End and the arrows are compared against SDL 1.2 keycodes. */
    console_type("bc");
    check(strcmp(console_line(), "abc") == 0, "the line reads abc");
    console_key(278 /* Home */);
    check(g_console->input.cursor.chars == 0, "Home moved the cursor to the start");
    console_key(279 /* End */);
    check(g_console->input.cursor.chars == 3, "End moved the cursor to the end");
    console_key(276 /* Left */);
    check(g_console->input.cursor.chars == 2, "Left moved the cursor left");
    console_key(275 /* Right */);
    check(g_console->input.cursor.chars == 3, "Right moved the cursor right");

    /* Enter submits: the console echoes the line and clears its input. */
    {
        unsigned int lines_before = g_console->view.lines->count;

        console_key(13 /* SDLK_RETURN */);
        check(strcmp(console_line(), "") == 0, "Enter cleared the input line");
        check(g_console->view.lines->count != lines_before, "Enter echoed the line into the output");
    }

    g_keyEventHandler = NULL;
    Keyboard_SetEventHandler(NULL);
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SKIP: SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    printf("pad slot mapping:\n");
    test_pad_slot_mapping();
    printf("key delivery:\n");
    test_key_delivery();
    printf("text input:\n");
    test_text_input();
    printf("console integration:\n");
    test_console_integration();
    SDL_Quit();
    if (failures == 0) {
        printf("ok: sdl2 input tests passed\n");
    }
    return failures != 0;
}
