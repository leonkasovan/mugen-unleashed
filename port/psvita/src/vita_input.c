/* Port change: preserve Vita controls through native SDL2 controllers. */
#include "mugen.h"
#include "vita_input.h"

static unsigned char cancel_held[PAD_MAX];
static int VitaInput_AnyCancel(void)
{
    int i;
    for (i = 0; i < PAD_MAX; ++i) if (cancel_held[i]) return 1;
    return 0;
}

static void VitaInput_SetCancel(int slot, unsigned char mask)
{
    int before = VitaInput_AnyCancel(), after;
    cancel_held[slot] = mask;
    after = VitaInput_AnyCancel();
    if (before != after) Keyboard_HandleKey(27, after);
}

void VitaInput_ClearCancel(int slot)
{
    int i, before = VitaInput_AnyCancel();
    for (i = 0; i < PAD_MAX; ++i)
        if (slot < 0 || slot == i) cancel_held[i] = 0;
    if (before && !VitaInput_AnyCancel()) Keyboard_HandleKey(27, 0);
}

void VitaInput_EnsureMapping(int device_index)
{
    const char *name = SDL_JoystickNameForIndex(device_index);
    SDL_JoystickGUID guid;
    char guid_string[33], mapping[512];
    char *existing;

    /* The native driver's GetGamepadMapping returns false. Keep any SDL or
     * data/gamecontrollerdb.txt mapping; register a fallback only if missing. */
    if (name == NULL || SDL_strcmp(name, "PSVita Controller") != 0) return;
    guid = SDL_JoystickGetDeviceGUID(device_index);
    existing = SDL_GameControllerMappingForGUID(guid);
    if (existing != NULL) {
        SDL_free(existing);
        return;
    }
    SDL_JoystickGetGUIDString(guid, guid_string, sizeof guid_string);
    /* SDL2 Vita indices: Triangle/Circle/Cross/Square/L/R, then
     * Down/Left/Up/Right/Select/Start. No dependency on the axis count.
     * Optional trigger/stick-click controls are absent on the built-in pad. */
    SDL_snprintf(mapping, sizeof mapping,
        "%s,PSVita Controller,a:b2,b:b1,x:b3,y:b0,"
        "leftshoulder:b4,rightshoulder:b5,dpdown:b6,dpleft:b7,"
        "dpup:b8,dpright:b9,back:b10,start:b11,"
        "leftx:a0,lefty:a1,rightx:a2,righty:a3,", guid_string);
    if (SDL_GameControllerAddMapping(mapping) < 0)
        Log_Printf("pads: Vita SDL2 mapping failed: %s\n", SDL_GetError());
}

void VitaInput_HandleControllerEvent(const SDL_Event *event)
{
    SDL_JoystickID instance;
    int button_event = event->type == SDL_CONTROLLERBUTTONDOWN ||
                       event->type == SDL_CONTROLLERBUTTONUP;
    int pressed = event->type == SDL_CONTROLLERBUTTONDOWN;
    int button, slot;
    unsigned char cancel_bit;
    if (!g_padsActive) return;
    if (button_event) instance = event->cbutton.which;
    else if (event->type == SDL_CONTROLLERAXISMOTION) instance = event->caxis.which;
    else return;
    if (SDL_GameControllerFromInstanceID(instance) == NULL) return;
    for (slot = 0; slot < PAD_MAX; ++slot)
        if (g_pads[slot].joystick != NULL &&
            SDL_JoystickInstanceID(g_pads[slot].joystick) == instance) break;
    if (slot == PAD_MAX) return;
    if (!button_event) {
        Pads_HandleControllerEvent(event);
        return;
    }
    button = event->cbutton.button;
    cancel_bit = button == SDL_CONTROLLER_BUTTON_A ? 1 :
                 button == SDL_CONTROLLER_BUTTON_X ? 2 : 4;
    /* A character-select cancel can be released after changing screens. */
    if (!pressed && (button == SDL_CONTROLLER_BUTTON_A ||
                     button == SDL_CONTROLLER_BUTTON_X))
        VitaInput_SetCancel(slot, (unsigned char)(cancel_held[slot] & ~cancel_bit));
    /* Select is Escape (ASCII 27) on Vita; Start alone pauses a match. */
    if (button == SDL_CONTROLLER_BUTTON_BACK) {
        VitaInput_SetCancel(slot, pressed ? (unsigned char)(cancel_held[slot] | 4) :
                                           (unsigned char)(cancel_held[slot] & ~4));
        return;
    }
    if (g_game != NULL && g_game->screen_kind == 2 &&
        (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_X ||
         button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
         button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
        SDL_Event release = *event;
        /* Clear any gameplay press carried across the screen transition.
         * Cross/Square cancel; shoulders do nothing in character select. */
        release.type = SDL_CONTROLLERBUTTONUP;
        release.cbutton.state = SDL_RELEASED;
        Pads_HandleControllerEvent(&release);
        if (pressed && (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_X))
            VitaInput_SetCancel(slot, (unsigned char)(cancel_held[slot] | cancel_bit));
        return;
    }
    Pads_HandleControllerEvent(event);
}
