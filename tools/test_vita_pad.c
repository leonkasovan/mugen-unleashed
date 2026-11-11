/* SDL2 raw-joystick fallback: unmapped device indices become stable engine
 * configuration codes. This is not the semantic Vita controller mapping;
 * tools/test_vita_sdl2input.c tests the real Vita SDL2 mapping separately. */
#include "mugen.h"

#include <stdio.h>
#include <string.h>

unsigned char g_padButtonState[INPUT_PAD_COUNT + 1];
unsigned char g_inputState[INPUT_CODE_COUNT];
unsigned char g_keyState[0x201];
int *g_keyMap;
Pad g_pads[PAD_MAX];
int g_padCount;
int g_logVerbose;
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
InputHandlerFn g_keyHandler;
InputHandlerFn g_padHandler;
KeyEventFn g_keyEventHandler;
int g_padsChanged;
int *g_inputQueue;
Uint32 g_videoSdlFlags;
ActivateFn g_onActivate;
int g_quitRequested;
IntArray g_keyQueue;
int g_outOfMemory;

static int failures;
static int last_code;
static int last_pressed;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

void Log_Printf(const char *fmt, ...)
{
    (void)fmt;
}

void MatchLog_Printf(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

/* Stubs the real input engine: records what the pad path delivers. */
static void CapturePad(int code, int pressed)
{
    printf("  pad event -> code %d pressed %d\n", code, pressed);
    last_code = code;
    last_pressed = pressed;
}

/* Fullscreen is covered by test_fullscreen; these input tests have no window. */
void Video_HandleFullscreenKey(int *key) { (void)key; }

void Video_OnResize(int width, int height)
{
    (void)width;
    (void)height;
}

/*
 * Simulates the Vita SDL1 driver's topology (4 axes, 0 hats, 12 buttons) by
 * opening virtual joysticks and asserting buttons by raw index, exactly the
 * way SDL_JOYBUTTONDOWN reaches Pads_HandleEvent.
 */
static int test_vita_topology(void)
{
    SDL_Event ev;

    SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
    if (SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 4, 12, 0) < 0) {
        printf("SKIP: no virtual joystick support (%s)\n", SDL_GetError());
        return 77;
    }
    memset(g_pads, 0, sizeof(g_pads));
    g_padCount = 0;
    g_padsActive = 0;
    if (Pads_Init() == 0) {
        printf("SKIP: Pads_Init found no joystick\n");
        return 77;
    }
    printf("  pad 0: axes=%d buttons=%d hats=%d -> hat_base=%d button_base=%d\n", SDL_JoystickNumAxes(g_pads[0].joystick),
           SDL_JoystickNumButtons(g_pads[0].joystick), SDL_JoystickNumHats(g_pads[0].joystick), g_pads[0].hat_base,
           g_pads[0].button_base);
    check(g_pads[0].hat_base == 8, "hat_base is axes*2 = 8 for the Vita topology");
    check(g_pads[0].button_base == 8, "button_base is hat_base + hats*4 = 8 for 0 hats");

    g_keyMap = NULL;
    g_padHandler = CapturePad;

    /*
     * Integration with the fixed config.c override. The values below are the
     * jk[] entries config.c now installs for this topology; the delivered code
     * must equal them exactly, or the button stays dead. Raw indices from the
     * 4-axis/0-hat driver (Triangle=0, Circle=1, Cross=2, Square=3, L=4, R=5,
     * Down=6, Left=7, Up=8, Right=9, Select=10, Start=11) become 9..20.
     */
    {
        struct {
            int raw;
            int config;
            const char *name;
        } pairs[] = {
            {8, 17, "Up/KEY_JUMP"}, {9, 18, "Right/KEY_RIGHT"},
            {6, 15, "Down/KEY_CROUCH"}, {7, 16, "Left/KEY_LEFT"},
            {3, 12, "Square/KEY_A"}, {0, 9, "Triangle/KEY_B"},
            {1, 10, "Circle/KEY_C"}, {2, 11, "Cross/KEY_X"},
            {4, 13, "L/KEY_Y"}, {5, 14, "R/KEY_Z"},
            {11, 20, "Start/KEY_START"},
        };
        size_t n;

        for (n = 0; n < sizeof(pairs) / sizeof(pairs[0]); n++) {
            memset(&ev, 0, sizeof(ev));
            ev.type = SDL_JOYBUTTONDOWN;
            ev.jbutton.button = (Uint8)pairs[n].raw;
            ev.jbutton.state = SDL_PRESSED;
            ev.jbutton.which = SDL_JoystickInstanceID(g_pads[0].joystick);
            last_code = -1;
            Pads_HandleEvent(&ev);
            if (last_code != pairs[n].config) {
                printf("FAIL: %s: delivered %d, config stores %d\n", pairs[n].name, last_code, pairs[n].config);
                failures++;
            }
        }
        printf("  all 11 config bindings match delivered codes\n");
    }
    return 0;
}

int main(void)
{
    int result;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SKIP: SDL_Init failed: %s\n", SDL_GetError());
        return 77;
    }
    printf("vita pad topology:\n");
    result = test_vita_topology();
    SDL_Quit();
    if (result != 0) return result;
    if (failures == 0) {
        printf("ok: vita pad tests passed\n");
    }
    return failures != 0;
}
