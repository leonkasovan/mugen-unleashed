/* Real SDL2 mapping resolution; only engine callback boundaries are mocked. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#define MUGEN_H
#define PAD_MAX 4
static struct { SDL_Joystick *joystick; } g_pads[PAD_MAX];
static struct { int screen_kind; } game, *g_game = &game;
static int g_padsActive = 1, forwards, keys, last_key, last_pressed;
static SDL_Event forwarded;
static void Pads_HandleControllerEvent(const SDL_Event *event)
{ forwarded = *event; ++forwards; }
static void Keyboard_HandleKey(int key, int pressed)
{ last_key = key; last_pressed = pressed; ++keys; }
static void Log_Printf(const char *format, ...) { (void)format; }
#include "../port/psvita/src/vita_input.c"

static void button(SDL_JoystickID id, int b, int down)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
    event.cbutton.which = id;
    event.cbutton.button = (Uint8)b;
    event.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
    forwards = keys = 0;
    VitaInput_HandleControllerEvent(&event);
}

int main(void)
{
    SDL_VirtualJoystickDesc desc;
    SDL_GameController *controller;
    SDL_Joystick *joy;
    SDL_JoystickID id;
    int index, axes, b;
    static const int expected[] = {
        SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START
    };
    SDL_SetMainReady();
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    for (axes = 4; axes <= 6; axes += 2) {
        SDL_zero(desc);
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
        desc.naxes = (Uint16)axes;
        desc.nbuttons = axes == 4 ? 12 : 16;
        desc.name = "PSVita Controller";
        index = SDL_JoystickAttachVirtualEx(&desc);
        assert(index >= 0);
        VitaInput_EnsureMapping(index);
        assert(SDL_IsGameController(index));
        controller = SDL_GameControllerOpen(index);
        assert(controller);
        joy = SDL_GameControllerGetJoystick(controller);
        g_pads[0].joystick = joy;
        id = SDL_JoystickInstanceID(joy);
        for (b = 0; b < 12; ++b) {
            SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForButton(
                controller, (SDL_GameControllerButton)expected[b]);
            assert(bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON);
            assert(bind.value.button == b);
        }
        for (game.screen_kind = 1; game.screen_kind <= 4; ++game.screen_kind) {
            button(id, SDL_CONTROLLER_BUTTON_BACK, 1);
            assert(keys == 1 && last_key == 27 && last_pressed && !forwards);
            button(id, SDL_CONTROLLER_BUTTON_BACK, 0);
            assert(keys == 1 && last_key == 27 && !last_pressed && !forwards);
            button(id, SDL_CONTROLLER_BUTTON_START, 1);
            assert(!keys && forwards == 1 && forwarded.type == SDL_CONTROLLERBUTTONDOWN);
            for (b = 0; b < 6; ++b) {
                button(id, expected[b], 1);
                assert(forwards == 1);
                if (game.screen_kind == 2 && b >= 2) {
                    assert(forwarded.type == SDL_CONTROLLERBUTTONUP);
                    assert(keys == (b == 2 || b == 3));
                } else {
                    assert(forwarded.type == SDL_CONTROLLERBUTTONDOWN && !keys);
                }
                button(id, expected[b], 0);
                assert(forwards == 1 && forwarded.type == SDL_CONTROLLERBUTTONUP);
                assert(keys == (game.screen_kind == 2 && (b == 2 || b == 3)));
            }
            button(id, SDL_CONTROLLER_BUTTON_START, 0);
        }
        game.screen_kind = 2;
        button(id, SDL_CONTROLLER_BUTTON_A, 1);
        assert(keys == 1 && last_pressed);
        game.screen_kind = 1;
        button(id, SDL_CONTROLLER_BUTTON_A, 0);
        assert(keys == 1 && !last_pressed && forwarded.type == SDL_CONTROLLERBUTTONUP);
        button(id, SDL_CONTROLLER_BUTTON_BACK, 1);
        keys = 0;
        VitaInput_ClearCancel(0);
        assert(keys == 1 && !last_pressed);
        game.screen_kind = 2;
        button(id, SDL_CONTROLLER_BUTTON_BACK, 1);
        assert(keys == 1 && last_pressed);
        button(id, SDL_CONTROLLER_BUTTON_A, 1);
        assert(!keys);
        button(id, SDL_CONTROLLER_BUTTON_BACK, 0);
        assert(!keys);
        button(id, SDL_CONTROLLER_BUTTON_A, 0);
        assert(keys == 1 && !last_pressed);
        button(id + 1000, SDL_CONTROLLER_BUTTON_BACK, 1);
        assert(!keys && !forwards);
        g_padsActive = 0;
        button(id, SDL_CONTROLLER_BUTTON_BACK, 1);
        assert(!keys && !forwards);
        g_padsActive = 1;
        {
            char guid[33], mapping[256];
            char *saved = SDL_GameControllerMapping(controller);
            assert(saved);
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof guid);
            SDL_snprintf(mapping, sizeof mapping, "%s,Custom Vita,a:b0,b:b3,", guid);
            assert(SDL_GameControllerAddMapping(mapping) >= 0);
            VitaInput_EnsureMapping(index);
            assert(SDL_GameControllerGetBindForButton(controller,
                       SDL_CONTROLLER_BUTTON_A).value.button == 0);
            assert(SDL_GameControllerAddMapping(saved) >= 0);
            SDL_free(saved);
        }
        SDL_GameControllerClose(controller);
        g_pads[0].joystick = NULL;
        assert(SDL_JoystickDetachVirtual(index) == 0);
    }
    SDL_Quit();
    puts("ok: Vita SDL2 mappings, user precedence, Back/Pause and cancellation");
    return 0;
}
