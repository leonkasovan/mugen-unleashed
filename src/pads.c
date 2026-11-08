/*
 * Joysticks (original module 0x4745F0-0x474CCF): up to four SDL joysticks
 * whose axes, hats and buttons all become buttons (see Pad), the state of
 * those buttons (g_padButtonState, by pad * PAD_BUTTONS + button) and the
 * joystick code queue that menus read (g_padQueue). Button changes reach
 * the input engine through g_padHandler (Input_OnPadButton).
 *
 * Also here: Input_FindKey, the menu lookup of an input code in the
 * players' key maps.
 */
/* <SDL.h> comes from mugen.h, which pulls it in after glad.h. */

#include "mugen.h"

#ifdef MUGEN_GAMEPAD_POLICY
/* Binding policy for one player plus publication to the input map (config.c),
 * applied when a pad attaches or detaches. */
void Config_RepublishJoystick(int player);
#endif

#ifdef MUGEN_SDL2_API
/* Port fix: own only the subsystem references acquired here. Options reloads
 * must not increment SDL's refcounts indefinitely or shut down another owner. */
static Uint32 g_padOwnedSubsystems;

static void Pads_LogDevice(SDL_Joystick *joy, int index)
{
    char guid[33];
    const char *name = SDL_JoystickName(joy);
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, sizeof(guid));
    Log_Printf("pads: device %d name=\"%s\" instance=%d guid=%s mapped=%d\n", index,
               name != NULL ? name : "(unnamed)", (int)SDL_JoystickInstanceID(joy),
               guid, (int)SDL_IsGameController(index));
}

static void Pads_LogSdl2(void)
{
    static int version_logged;
    static const char *const hints[] = {
        "SDL_XINPUT_ENABLED", "SDL_DIRECTINPUT_ENABLED", "SDL_JOYSTICK_HIDAPI",
        "SDL_JOYSTICK_RAWINPUT", "SDL_JOYSTICK_WGI", "SDL_GAMECONTROLLER_IGNORE_DEVICES",
        "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"
    };
    int i;
    if (!version_logged) {
        SDL_version linked;
        SDL_GetVersion(&linked);
        Log_Printf("pads: SDL runtime %u.%u.%u, headers %u.%u.%u, ABI %u-bit, revision %s\n",
                   (unsigned)linked.major, (unsigned)linked.minor, (unsigned)linked.patch,
                   (unsigned)SDL_MAJOR_VERSION, (unsigned)SDL_MINOR_VERSION, (unsigned)SDL_PATCHLEVEL,
                   (unsigned)(sizeof(void *) * 8), SDL_GetRevision());
        version_logged = 1;
    }
    for (i = 0; i < (int)(sizeof(hints) / sizeof(hints[0])); i++) {
        const char *value = SDL_GetHint(hints[i]);
        Log_Printf("pads: hint %s=%s\n", hints[i], value != NULL ? value : "(default)");
    }
}

static int Pads_InitSdl2(void)
{
    Pads_LogSdl2();
    /* Initialize raw enumeration independently. A failed controller service
     * must not roll back an otherwise usable joystick service. */
    if ((SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0) {
            Log_Printf("pads: joystick init failed: %s\n", SDL_GetError());
            return 0;
        }
        g_padOwnedSubsystems |= SDL_INIT_JOYSTICK;
    }
    if ((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
            Log_Printf("pads: controller init failed: %s; raw joystick input remains available\n", SDL_GetError());
        } else {
            g_padOwnedSubsystems |= SDL_INIT_GAMECONTROLLER;
        }
    }
    Log_Printf("pads: subsystems joystick=%d controller=%d\n",
               (SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) != 0,
               (SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) != 0);
    SDL_JoystickEventState(SDL_ENABLE);
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) {
        SDL_GameControllerEventState(SDL_ENABLE);
    }
    /* Pad initialization is on the SDL/video thread. Refresh discovery before
     * the first count without consuming queued hotplug or keyboard events. */
    SDL_PumpEvents();
    return 1;
}
#endif

/* Axis values beyond this press the axis' button. */
#define PAD_AXIS_THRESHOLD 16000

#ifdef MUGEN_GAMEPAD_POLICY
/* Defined below (the game-controller path); Pads_Init and Input_DeinitPads
 * call them, so the declarations come first. */
static int Pads_OpenControllers(void);
static void Pads_CloseControllers(void);
#endif

/*
 * 0x004745F0: opens the joysticks (at most PAD_MAX). Returns the button
 * count of the first opened joystick that has buttons, 0 when there is
 * none. With no joystick at all nothing else changes (g_padsActive keeps
 * its value).
 */
int Pads_Init(void)
{
    int found = 0;
    int i;

#ifdef MUGEN_SDL2_API
    if (!Pads_InitSdl2()) {
        g_padCount = 0;
        g_padsActive = 0;
        return 0;
    }
#endif

    g_padCount = SDL_NumJoysticks();
#ifdef MUGEN_SDL2_API
    if (g_padCount < 0) {
        Log_Printf("pads: joystick enumeration failed: %s\n", SDL_GetError());
        g_padCount = 0;
        g_padsActive = 0;
        return 0;
    }
#endif
    /*
     * Diagnostic (port addition): how many devices SDL reports, and what each
     * one looks like. Without this the only evidence was "Initializing pads...
     * OK", which says a pad opened but not which pad, what topology it has, or
     * which engine button codes it will therefore deliver -- the three things
     * needed to explain a pad that opens and then appears dead.
     */
    Log_Printf("pads: SDL_NumJoysticks() = %d\n", g_padCount);
    if (g_padCount == 0) {
#if defined(ANDROID_PORT) || defined(MUGEN_SDL2_API)
        /*
         * Fix (port change): keep discovery alive when nothing is attached.
         * Returning here left joystick events disabled (Input_DeinitPads sets
         * SDL_IGNORE), so a pad connected later produced no event and
         * Pads_DeviceEvent never ran. Adding is harmless: Pads_HandleEvent
         * ignores events while g_padsActive is 0, and the add event is what
         * sets it.
         */
        Log_Printf("pads: input initialized, no devices enumerated; check SDL backend/driver visibility\n");
        SDL_JoystickEventState(SDL_ENABLE);
        g_padsActive = 0;
        g_padCount = 0;
        return 0;
#else
        return 0;
#endif
    }
    if (g_padCount > PAD_MAX) {
        g_padCount = PAD_MAX;
    }
    memset(g_pads, 0, sizeof(g_pads));
    for (i = 0; i < g_padCount; i++) {
        SDL_Joystick *joy = SDL_JoystickOpen(i);
        Pad *pad = &g_pads[i];
        const char *name;
        int count;

        if (joy == NULL) {
#ifdef MUGEN_SDL2_API
            Log_Printf("pads: SDL_JoystickOpen(%d) failed: %s\n", i, SDL_GetError());
#endif
            continue;
        }
        /* Port fix: SDL1 names a device index; SDL2 names an open handle. */
#ifdef MUGEN_SDL2_API
        name = SDL_JoystickName(joy);
        Pads_LogDevice(joy, i);
#else
        name = SDL_JoystickName(i);
#endif
        pad->joystick = joy;
        pad->hat_base = SDL_JoystickNumAxes(joy) * 2;
        pad->button_base = pad->hat_base + SDL_JoystickNumHats(joy) * 4;
        count = SDL_JoystickNumButtons(joy) + pad->button_base;
        pad->button_count = count;
        if (count > PAD_BUTTONS) {
            pad->button_count = PAD_BUTTONS;
        }
        /*
         * The topology decides every engine button code:
         *   axis  n   -> pad*PAD_BUTTONS + n*2 + 1 (negative) / + 2 (positive)
         *   hat       -> pad*PAD_BUTTONS + axes*2 + 1..4 (up, down, left, right)
         *   button b  -> pad*PAD_BUTTONS + axes*2 + hats*4 + 1 + b
         * So the same physical pad produces different codes once its axis or
         * hat count changes, and mugen.cfg's [P<n> Joystick] group has to agree
         * with that. Printing the ranges is what makes a mismatch visible.
         */
        Log_Printf("pads: %d \"%s\" axes=%d hats=%d buttons=%d -> "
                   "axis codes %d..%d, hat codes %d..%d, button codes %d..%d\n",
                   i, name != NULL ? name : "(unnamed)",
                   SDL_JoystickNumAxes(joy), SDL_JoystickNumHats(joy), SDL_JoystickNumButtons(joy),
                   1, pad->hat_base, pad->hat_base + 1, pad->hat_base + 4,
                   pad->button_base + 1, pad->button_base + SDL_JoystickNumButtons(joy));
        if (count > PAD_BUTTONS) {
            Log_Printf("pads: %d has %d codes, only %d fit a slot; the rest are dropped\n", i, count,
                       PAD_BUTTONS);
        }
        if (found == 0) {
            found = pad->button_count;
        }
    }
    /* One byte more than the INPUT_PAD_COUNT buttons. */
    memset(g_padButtonState, 0, INPUT_PAD_COUNT + 1);
#ifdef MUGEN_GAMEPAD_POLICY
    if (Pads_OpenControllers() > 0) {
        /* Controller events, not raw joystick events, drive these pads. */
        SDL_GameControllerEventState(SDL_ENABLE);
    }
#endif
    g_padsActive = found != 0;
    if (g_padsActive) {
        SDL_JoystickEventState(SDL_ENABLE);
    }
    g_padsChanged = 0;
    return found;
}

/*
 * Compact engine codes of the semantic controls.
 *
 * These are numbers rather than Android state, and the direction bookkeeping
 * below needs them on every backend (Pads_DeviceEvent is compiled for desktop
 * SDL2 as well), so they are defined outside the MUGEN_GAMEPAD_POLICY block.
 */
#define PAD_CTRL_UP     1
#define PAD_CTRL_DOWN   2
#define PAD_CTRL_LEFT   3
#define PAD_CTRL_RIGHT  4
#define PAD_CTRL_A      5
#define PAD_CTRL_B      6
#define PAD_CTRL_C      7
#define PAD_CTRL_X      8
#define PAD_CTRL_Y      9
#define PAD_CTRL_Z      10
#define PAD_CTRL_START  11
/* SELECT is a system control: Back in menus, Pause in a match. The screen
 * loop translates it before Game_ReadInput runs the keyboard pause logic. */
#define PAD_CTRL_SELECT PAD_SELECT_CODE

/*
 * Direction sources (fix for the port's d-pad/analog overlap).
 *
 * The stick and the d-pad both drive the same four engine buttons, and the
 * first version wrote the button state directly from each source. Moving the
 * stick back inside its deadzone therefore released a direction the d-pad was
 * still holding, because the stick could not tell that the d-pad owned it.
 *
 * Each slot now records what the stick and the pad are each holding, and the
 * engine-visible button state is their union. The masks use the PAD_CTRL_*
 * direction codes as bit positions (1..4, so bits 1..4 of a byte).
 */
#define PAD_DIR_SOURCES 2 /* 0 = stick, 1 = d-pad */

/* Sidecars, not fields on Pad: the reconstructed struct has a fixed layout. */
static unsigned char g_padDirHeld[PAD_DIR_SOURCES][PAD_MAX];
#ifdef MUGEN_GAMEPAD_POLICY
/* System controls never enter g_padButtonState or the gameplay input map. */
static unsigned char g_padMenuHeld[PAD_MAX];
#endif

static void Pads_ComposeDirections(int slot)
{
    int code;

    if (slot < 0 || slot >= PAD_MAX) {
        return;
    }
    for (code = PAD_CTRL_UP; code <= PAD_CTRL_RIGHT; code++) {
        int bit = 1 << code;
        int held = ((g_padDirHeld[0][slot] | g_padDirHeld[1][slot]) & bit) != 0;
        int index = slot * PAD_BUTTONS + code;

        /* Send only real changes: the engine's queue must not fill with
         * release/press pairs for a direction that never moved. */
        if (g_padButtonState[index] != held) {
            Pads_SetButton(index, held);
        }
    }
}

/* Records what one source holds for one direction and re-derives the union. */
#ifdef MUGEN_GAMEPAD_POLICY
static void Pads_SetDirectionSource(int slot, int source, int code, int held)
{
    int bit;

    if (slot < 0 || slot >= PAD_MAX || source < 0 || source >= PAD_DIR_SOURCES) {
        return;
    }
    if (code < PAD_CTRL_UP || code > PAD_CTRL_RIGHT) {
        return;
    }
    bit = 1 << code;
    if (held) {
        g_padDirHeld[source][slot] |= (unsigned char)bit;
    } else {
        g_padDirHeld[source][slot] &= (unsigned char)~bit;
    }
    Pads_ComposeDirections(slot);
}
#endif /* only the controller path feeds per-source directions */

/*
 * Forgets every slot's direction sources.
 *
 * Suspend and deinit release the engine buttons so a held key does not stay
 * held across the pause, but the source masks behind them were left populated.
 * The next analog event then recomposed the union and brought the old direction
 * back: holding Up, backgrounding, and moving the stick Right produced Up=1 and
 * Right=1. Clearing the masks together with the buttons keeps the two in step.
 */
void Pads_ResetDirectionSources(void)
{
    int slot;

    for (slot = 0; slot < PAD_MAX; slot++) {
        g_padDirHeld[0][slot] = 0;
        g_padDirHeld[1][slot] = 0;
#ifdef MUGEN_GAMEPAD_POLICY
        g_padMenuHeld[slot] = 0;
#endif
    }
}

/* Forgets both sources for a slot (device removed or slot reassigned). */
void Pads_ClearDirectionSources(int slot)
{
    if (slot < 0 || slot >= PAD_MAX) {
        return;
    }
    g_padDirHeld[0][slot] = 0;
    g_padDirHeld[1][slot] = 0;
#ifdef MUGEN_GAMEPAD_POLICY
    g_padMenuHeld[slot] = 0;
#endif
    Pads_ComposeDirections(slot);
}

#ifdef MUGEN_GAMEPAD_POLICY
/*
 * Port fix: the controller path uses SDL's game-controller semantics.
 *
 * The engine's raw model turns a device's axis/hat/button *indices* into input
 * codes (code = axes*2 + hats*4 + 1 + index). That only lines up with a given
 * mugen.cfg when the pad has the topology those numbers were written for, and
 * a real pad does not: the tested Xbox Wireless Controller reports
 * 8 axes and 0 hats, so its face buttons start at code 17 while mugen.cfg's
 * A/B/X/Y (13..16) are axis codes on it, and its d-pad arrives as buttons at
 * code 32+ -- past the 32 codes one pad slot holds, so those were dropped
 * outright. The pad opened, and nothing on it worked.
 *
 * So the semantic controls are bound directly. SDL_GameController
 * maps a device through its own database, so A/B/X/Y, the d-pad, the shoulders
 * and Start mean the same thing on every pad. The engine's 12 code slots per
 * pad are more than enough for those eleven controls.
 *
 * The compact layout, also written into g_game->joy_keys by
 * Pads_ControllerBindings():
 *
 *   1 up   2 down   3 left   4 right
 *   5 A    6 B      7 C      8 X      9 Y      10 Z      11 Start
 *
 * Directions keep 1..4 because that is where the raw axis path already put the
 * left stick -- the two agree instead of fighting.
 *
 * C and Z go on the shoulders (RB and LB): a M.U.G.E.N 6-button pad puts the
 * third punch and third kick on the top row, and a modern gamepad's only
 * remaining buttons are RB/LB. Logged at start-up so the choice is visible.
 */
/* (The PAD_CTRL_* codes are defined above the MUGEN_GAMEPAD_POLICY block: the direction
 * bookkeeping needs them on every backend.) */

/* Sidecar, not a field on Pad: the reconstructed struct has a fixed layout
 * (MUGEN_ASSERT_SIZE(Pad, 0x14)) and backend state must not extend it. */
static SDL_GameController *g_padController[PAD_MAX];

/* Semantic control -> compact engine code, in one table so the two lists
 * cannot drift apart. */
typedef struct PadCtrlMap {
    SDL_GameControllerButton button;
    int code;
    int key;
    const char *name;
} PadCtrlMap;

static const PadCtrlMap pad_ctrl_map[] = {
    { SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_CTRL_UP, KEY_JUMP, "Up" },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_CTRL_DOWN, KEY_CROUCH, "Down" },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_CTRL_LEFT, KEY_LEFT, "Left" },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_CTRL_RIGHT, KEY_RIGHT, "Right" },
    { SDL_CONTROLLER_BUTTON_A, PAD_CTRL_A, KEY_A, "A" },
    { SDL_CONTROLLER_BUTTON_B, PAD_CTRL_B, KEY_B, "B" },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_CTRL_C, KEY_C, "C" },
    { SDL_CONTROLLER_BUTTON_X, PAD_CTRL_X, KEY_X, "X" },
    { SDL_CONTROLLER_BUTTON_Y, PAD_CTRL_Y, KEY_Y, "Y" },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_CTRL_Z, KEY_Z, "Z" },
    { SDL_CONTROLLER_BUTTON_START, PAD_CTRL_START, KEY_START, "Start" },
    /* System controls have no KEY_* slot. The screen/key-config loops
     * interpret them; -1 keeps them out of the player binding table. */
    { SDL_CONTROLLER_BUTTON_BACK, PAD_CTRL_SELECT, -1, "Select" },
    { SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_BACK_CODE, -1, "Back (R3)" },
};
#define PAD_CTRL_MAP_COUNT ((int)(sizeof(pad_ctrl_map) / sizeof(pad_ctrl_map[0])))

/*
 * Port fix: pads whose SDL mapping leaves the system buttons out.
 *
 * SDL_GameController reports only the controls its mapping names. The database
 * entry for some handheld pads covers the face buttons, shoulders and d-pad and
 * nothing else -- for the GO-Super Gamepad (RG351MP and other ArkOS handhelds)
 * it names b0..b11 and stops, so SELECT, START and the stick clicks are simply
 * invisible to the controller API. Nothing in the engine can recover them from
 * there: the raw joystick still sees all 17 buttons, but a slot driven through
 * the controller deliberately ignores raw codes (see Pads_ControllerBindings).
 *
 * The fix is to complete the mapping before the controller is opened: query the
 * mapping SDL uses, append the missing bindings, register it again. SDL then
 * delivers BACK/START/... like on any other pad and the semantic layer below
 * needs no change at all.
 *
 * The numbers are raw joystick button indices, the same numbering the kernel and
 * the distribution's own input profiles use (ArkOS ships
 * ~/.config/retroarch/autoconfig/udev/GO-Super Gamepad.cfg: select=12,
 * start=13). Keyed by GUID, so no other pad can be affected, and skipped when
 * the mapping already carries back:/start: -- a newer SDL database, or a user's
 * own mapping, wins over this table.
 */
typedef struct PadMappingFixup {
    const char *guid;
    const char *bindings;
} PadMappingFixup;

static const PadMappingFixup pad_mapping_fixups[] = {
    /* GO-Super Gamepad (RG351MP and other ArkOS handhelds).
     *
     * The pad reports exactly seventeen BTN_* codes, in joydev order: b0..b7 are
     * A/B/X/Y and the four shoulders, b8..b11 the d-pad, and b12..b16 the five
     * buttons the vendor driver reports as 0x2c0..0x2c4 -- SELECT and START
     * first, then the stick clicks and the hotkey button. The ArkOS input
     * profile for this pad numbers them the same way (select=12, start=13), so
     * the two agree on the numbering even though the kernel calls 0x2c0/0x2c1
     * THUMBL/THUMBR.
     *
     *   b12 SELECT -> back      b13 START -> start
     *   b14 L3     -> leftstick b15 R3    -> rightstick
     */
    { "1900bb3e4b4800000011000000010000",
      "back:b12,start:b13,leftstick:b14,rightstick:b15" },
};
#define PAD_FIXUP_COUNT ((int)(sizeof pad_mapping_fixups / sizeof pad_mapping_fixups[0]))

/* Called before SDL_GameControllerOpen: the mapping has to be in place when
 * SDL builds its button table, otherwise the events still never arrive. */
static void Pads_CompleteControllerMapping(int device_index)
{
    SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(device_index);
    char guidstr[33];
    const char *extra = NULL;
    char *current;
    char *extended;
    size_t need;
    int i;

    SDL_JoystickGetGUIDString(guid, guidstr, (int)sizeof guidstr);
    for (i = 0; i < PAD_FIXUP_COUNT; i++) {
        if (SDL_strcasecmp(guidstr, pad_mapping_fixups[i].guid) == 0) {
            extra = pad_mapping_fixups[i].bindings;
            break;
        }
    }
    if (extra == NULL) {
        return;
    }
    current = SDL_GameControllerMappingForGUID(guid);
    if (current == NULL) {
        Log_Printf("pads: %d has no controller mapping to complete\n", device_index);
        return;
    }
    /* Logged in full: which mapping is in effect decides everything below, and
     * a database file in the environment (PortMaster exports
     * SDL_GAMECONTROLLERCONFIG_FILE) can supply an entry for the same pad that
     * disagrees with the hardware -- it did here, binding back:/start: to
     * buttons this model never sends, so the presses produced nothing at all.
     * A mapping that already carries the keys is therefore overridden, not
     * trusted: these indices were read off the device itself. */
    Log_Printf("pads: %d mapping before: %s\n", device_index, current);
    need = SDL_strlen(current) + SDL_strlen(extra) + 2;
    extended = (char *)SDL_malloc(need);
    if (extended != NULL) {
        SDL_snprintf(extended, need, "%s,%s", current, extra);
        if (SDL_GameControllerAddMapping(extended) < 0) {
            Log_Printf("pads: %d could not complete the controller mapping: %s\n",
                       device_index, SDL_GetError());
        } else {
            Log_Printf("pads: %d controller mapping completed for a pad whose database"
                       " entry has no system buttons (%s)\n", device_index, extra);
        }
        SDL_free(extended);
    }
    SDL_free(current);
}

/* Port addition: shared by the screen loop and the standalone key-config
 * loop. Strip the release flag before decoding the slot. A raw joystick's
 * buttons 12/13 must retain their configured gameplay meaning. */
PadMenuAction Pads_MenuAction(unsigned int code, PadMenuContext context)
{
    unsigned int button = code & ~PAD_RELEASE;
    int slot = (int)(button / PAD_BUTTONS);
    int local = (int)(button % PAD_BUTTONS);

    if (button >= INPUT_PAD_COUNT || !Pads_SlotUsesController(slot) ||
        (local != PAD_SELECT_CODE && local != PAD_BACK_CODE)) {
        return PAD_MENU_NONE;
    }
    if ((code & PAD_RELEASE) != 0 ||
        (local == PAD_SELECT_CODE && context == PAD_CONTEXT_TITLE)) {
        return PAD_MENU_CONSUMED;
    }
    return local == PAD_SELECT_CODE && context == PAD_CONTEXT_MATCH ? PAD_MENU_PAUSE : PAD_MENU_BACK;
}

/* Opens the game controllers that back the joysticks already opened. Returns
 * how many opened. A pad SDL has no mapping for stays unopened and the caller
 * keeps the raw code path. */
static int Pads_OpenControllers(void)
{
    int opened = 0;
    int i;

    for (i = 0; i < g_padCount && i < PAD_MAX; i++) {
        if (g_pads[i].joystick == NULL) {
            continue;
        }
        Pads_CompleteControllerMapping(i);
        if (!SDL_IsGameController(i)) {
            Log_Printf("pads: %d has no SDL game-controller mapping; raw codes only\n", i);
            continue;
        }
        g_padController[i] = SDL_GameControllerOpen(i);
        if (g_padController[i] == NULL) {
            Log_Printf("pads: %d could not open as a game controller: %s\n", i, SDL_GetError());
            continue;
        }
        Log_Printf("pads: %d opened as a game controller (semantic buttons)\n", i);
        opened++;
    }
    return opened;
}

static void Pads_CloseControllers(void)
{
    int i;

    for (i = 0; i < PAD_MAX; i++) {
        if (g_padController[i] != NULL) {
            SDL_GameControllerClose(g_padController[i]);
            g_padController[i] = NULL;
        }
    }
}

/* 1 when the slot is driven by a game controller, so the raw event path must
 * keep out of the way: its axis codes 9..12 would collide with the compact
 * layout's X/Y/Z/Start. */
int Pads_SlotUsesController(int slot)
{
    if (slot < 0 || slot >= PAD_MAX) {
        return 0;
    }
    return g_padController[slot] != NULL;
}

/* How many pads are driven by a game controller (0 when none). */
int Pads_ControllerCount(void)
{
    int n = 0;
    int i;

    for (i = 0; i < PAD_MAX; i++) {
        if (g_padController[i] != NULL) {
            n++;
        }
    }
    return n;
}

/*
 * Fills out[KEY_COUNT] with the engine joy code of each action for player
 * `slot`, or JOY_NONE when the pad does not have that control.
 *
 * Fix (port change): the codes are local to a pad, and the engine's joy code
 * space is slot * PAD_BUTTONS + local. The first version always read the slot-0
 * controller and never added the offset, so player 2's default layout was
 * player 1's: A was 5 on both instead of 5 and 37. The offset is applied here,
 * once, and the controller check follows the slot.
 */
void Pads_ControllerBindings(int slot, int *out)
{
    int i;
    int base;

    if (slot < 0 || slot >= PAD_MAX) {
        slot = 0;
    }
    base = slot * PAD_BUTTONS;
    for (i = 0; i < KEY_COUNT; i++) {
        out[i] = JOY_NONE;
    }
    if (g_padController[slot] == NULL) {
        return;
    }
    for (i = 0; i < PAD_CTRL_MAP_COUNT; i++) {
        /* key < 0 marks a system control with no KEY_* slot (Select / R3).
         * Writing out[-1] would corrupt the caller's stack. */
        if (pad_ctrl_map[i].key < 0 || pad_ctrl_map[i].key >= KEY_COUNT) {
            continue;
        }
        out[pad_ctrl_map[i].key] = base + pad_ctrl_map[i].code;
    }
    Log_Printf("pads: controller layout (slot %d):", slot);
    for (i = 0; i < PAD_CTRL_MAP_COUNT; i++) {
        Log_Printf(" %s=%d", pad_ctrl_map[i].name, pad_ctrl_map[i].code);
    }
    Log_Printf("\n");
}

/*
 * Opens the semantic controller for a device that just appeared, in the slot
 * its raw joystick was given, and rebinds that player.
 *
 * Finding: the handle was opened but the bindings were not. A controller
 * plugged in after a pad-less start-up then pressed code 5 while the active
 * map still expected 13 -- it looked bound and pressed nothing. Only this
 * slot is rebound, so plugging in player 2's pad cannot disturb player 1.
 */
void Pads_AttachController(int slot, int device_index)
{
    if (slot < 0 || slot >= PAD_MAX) {
        return;
    }
    if (g_padController[slot] != NULL) {
        SDL_GameControllerClose(g_padController[slot]);
        g_padController[slot] = NULL;
    }
    Pads_CompleteControllerMapping(device_index);
    if (!SDL_IsGameController(device_index)) {
        /* No mapping: the raw joystick codes are the only ones that can work,
         * so leave the existing bindings alone. */
        Log_Printf("Pads: slot %d attached with no game-controller mapping; bindings unchanged\n", slot);
        return;
    }
    g_padController[slot] = SDL_GameControllerOpen(device_index);
    if (g_padController[slot] == NULL) {
        Log_Printf("Pads: controller slot %d attached but could not be opened: %s\n", slot, SDL_GetError());
        return;
    }
    Log_Printf("Pads: controller slot %d opened on attach (%s)\n", slot,
               SDL_GameControllerName(g_padController[slot]));
    /* Controller events now drive this pad, so the raw path must stand aside
     * and the semantic layout must become the active binding. */
    SDL_GameControllerEventState(SDL_ENABLE);
    Config_RepublishJoystick(slot);
}

/*
 * Handles an SDL controller event by mapping the semantic control onto the
 * compact layout above.
 */
void Pads_HandleControllerEvent(const SDL_Event *ev)
{
    int slot;
    int i;

    if (!g_padsActive) {
        return;
    }
    for (slot = 0; slot < PAD_MAX; slot++) {
        if (g_padController[slot] != NULL) {
            SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_padController[slot]));

            if (ev->type == SDL_CONTROLLERBUTTONDOWN || ev->type == SDL_CONTROLLERBUTTONUP) {
                if (id != ev->cbutton.which) {
                    continue;
                }
            } else if (ev->type == SDL_CONTROLLERAXISMOTION) {
                if (id != ev->caxis.which) {
                    continue;
                }
            } else {
                return;
            }
            break;
        }
    }
    if (slot >= PAD_MAX) {
        return;
    }

    if (ev->type == SDL_CONTROLLERBUTTONDOWN || ev->type == SDL_CONTROLLERBUTTONUP) {
        int pressed = ev->type == SDL_CONTROLLERBUTTONDOWN;
        static int logged[PAD_CTRL_MAP_COUNT];

        for (i = 0; i < PAD_CTRL_MAP_COUNT; i++) {
            if (pad_ctrl_map[i].button != ev->cbutton.button) {
                continue;
            }
            if (logged[i] < 2) {
                logged[i]++;
                Log_Printf("pads: controller %s %s -> code %d (slot %d, SDL button %d)\n",
                           pad_ctrl_map[i].name, pressed ? "down" : "up",
                           slot * PAD_BUTTONS + pad_ctrl_map[i].code, slot,
                           (int)pad_ctrl_map[i].button);
            }
            if (pad_ctrl_map[i].code >= PAD_CTRL_UP && pad_ctrl_map[i].code <= PAD_CTRL_RIGHT) {
                /* Directions are shared with the stick, so they go through the
                 * source bookkeeping instead of writing the button directly:
                 * a d-pad release must not clear a direction the stick holds,
                 * and vice versa. */
                Pads_SetDirectionSource(slot, 1, pad_ctrl_map[i].code, pressed);
            } else if (pad_ctrl_map[i].key < 0) {
                int bit = 1 << (pad_ctrl_map[i].code - PAD_SELECT_CODE);
                /* Port addition: edge-triggered menu input bypasses the
                 * gameplay callback and state, even with a stale key map. */
                if (((g_padMenuHeld[slot] & bit) != 0) != pressed) {
                    int code = slot * PAD_BUTTONS + pad_ctrl_map[i].code;
                    if (pressed) g_padMenuHeld[slot] |= (unsigned char)bit;
                    else g_padMenuHeld[slot] &= (unsigned char)~bit;
                    Pads_QueueCode(pressed ? code : code | PAD_RELEASE);
                }
            } else {
                Pads_SetButton(slot * PAD_BUTTONS + pad_ctrl_map[i].code, pressed ? 1 : 0);
            }
            return;
        }
        /* Port addition: a control SDL reports but the engine does not use.
         * Logged (once per button) because it is the difference between "the
         * pad never sent it" and "it arrived under a name we do not bind" --
         * the two look identical from the outside, and on a pad whose mapping
         * numbers the buttons differently it is the whole diagnosis. */
        {
            static int loggedUnmapped[SDL_CONTROLLER_BUTTON_MAX];

            if (ev->cbutton.button < SDL_CONTROLLER_BUTTON_MAX &&
                loggedUnmapped[ev->cbutton.button] < 2) {
                loggedUnmapped[ev->cbutton.button]++;
                Log_Printf("pads: controller button %d (%s) %s has no engine binding\n",
                           (int)ev->cbutton.button,
                           SDL_GameControllerGetStringForButton(ev->cbutton.button),
                           pressed ? "down" : "up");
            }
        }
        return;
    }

    /* Axes: the left stick steers with the same codes the d-pad uses, so
     * either input works without a second binding. */
    if (ev->type == SDL_CONTROLLERAXISMOTION) {
        int axis = ev->caxis.axis;
        int value = ev->caxis.value;
        int first;
        int dir;

        if (axis != SDL_CONTROLLER_AXIS_LEFTX && axis != SDL_CONTROLLER_AXIS_LEFTY) {
            return;
        }
        {
            int neg;
            int pos;

            first = (axis == SDL_CONTROLLER_AXIS_LEFTX) ? PAD_CTRL_LEFT : PAD_CTRL_UP;
            neg = first;
            pos = first + 1;
            dir = (value < -PAD_AXIS_THRESHOLD) ? -1 : ((value > PAD_AXIS_THRESHOLD) ? 1 : 0);
            /* Same inversion fix as before, now recorded per source: only the
             * stick's own view of these two directions changes, so a d-pad
             * press survives a stick that returns to centre. */
            Pads_SetDirectionSource(slot, 0, neg, dir < 0);
            Pads_SetDirectionSource(slot, 0, pos, dir > 0);
        }
    }
}
#endif /* MUGEN_GAMEPAD_POLICY */

/* 0x004746D0: closes the joysticks and removes the joystick handler. */
void Input_DeinitPads(void)
{
    int i;

#ifndef MUGEN_SDL2_API
    if (g_padsActive == 0) {
        return;
    }
#endif
    g_padHandler = NULL;
#ifdef MUGEN_GAMEPAD_POLICY
    Pads_CloseControllers();
#endif
    memset(g_padButtonState, 0, INPUT_PAD_COUNT + 1);
    /* The slots go with the pads; a stale mask would resurface on the next
     * analog event of a later session. */
    Pads_ResetDirectionSources();
    g_padQueueWrite = 0;
    g_padQueueRead = 0;
#if defined(ANDROID_PORT) || defined(MUGEN_SDL2_API)
    /*
     * Fix (port change): SDL_IGNORE here disabled the joystick events that
     * Pads_DeviceEvent depends on, and Pads_Init returns early when no pad is
     * present, so the next device to be connected was never seen at all --
     * hotplug only worked if a pad happened to be attached at start-up.
     * Events stay enabled and Pads_HandleEvent already ignores them while no
     * pad is open (g_padsActive == 0).
     */
#else
    SDL_JoystickEventState(SDL_IGNORE);
#endif
    for (i = 0; i < g_padCount; i++) {
#ifdef MUGEN_SDL2_API
        /*
         * SDL2 removed SDL_JoystickOpened. The engine keeps its own handle per
         * pad slot, which is the same test -- a non-NULL handle means the slot
         * is open.
         */
        if (g_pads[i].joystick != NULL) {
#else
        if (SDL_JoystickOpened(i) && g_pads[i].joystick != NULL) {
#endif
            SDL_JoystickClose(g_pads[i].joystick);
#if defined(ANDROID_PORT) || defined(MUGEN_SDL2_API)
            /* Fix (port change): clear the handle on every SDL2 target, not
             * just Android. A closed handle left in the slot makes
             * Pads_DeviceEvent treat it as an open pad and skip the slot when
             * the device comes back. */
            g_pads[i].joystick = NULL;
#endif
        }
    }
    g_padsActive = 0;
    g_padCount = 0;
#ifdef MUGEN_SDL2_API
    if (g_padOwnedSubsystems & SDL_INIT_GAMECONTROLLER) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    if (g_padOwnedSubsystems & SDL_INIT_JOYSTICK) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    g_padOwnedSubsystems = 0;
#endif
}

/* 0x00474750: reports a button change (code = pad * PAD_BUTTONS + button). */
void Pads_SetButton(int code, int pressed)
{
    /* Fix (port change): callers can compute codes from device-reported axis,
     * hat and button numbers. Every index into g_padButtonState goes through
     * here, so this is the one place that has to reject an out-of-range code. */
    if (code < 0 || code > INPUT_PAD_COUNT) {
        return;
    }
    if (g_padHandler != NULL) {
        g_padHandler(code, pressed);
    }
    Pads_QueueCode(pressed ? code : code | PAD_RELEASE);
    g_padButtonState[code] = (unsigned char)pressed;
    g_padsChanged = 1;
}

/* A button whose state is a flag value (hat directions) or 0. */
static void pad_set_flag(int code, int value)
{
    if ((signed char)g_padButtonState[code] == value) {
        return;
    }
    Pads_SetButton(code, value);
}

/*
 * 0x00474790: handles an SDL joystick event.
 *
 * Quirks kept: every hat of a joystick drives the same four buttons, whose
 * state stores the SDL_HAT_* bit itself (2, 4 or 8, not 1) and which is
 * what the handler gets as "pressed"; axis and hat buttons are not checked
 * against PAD_BUTTONS, so high axis numbers write past g_padButtonState
 * (axis buttons of one pad may overlap the next pad's).
 */
/*
 * The pad slot an event belongs to.
 *
 * SDL 1.2's `which` was the device index, which is the slot. SDL2 replaced it
 * with an instance id that is unrelated to the slot, so the engine's handle is
 * matched back to its slot here. The table is not fixed at start-up: SDL2 sends
 * SDL_JOYDEVICEADDED/REMOVED and Pads_DeviceEvent (below) opens or releases the
 * slot on those events. This function only answers "which of the currently open
 * pads sent this", which is why an event from a device that is not open yet
 * returns -1.
 *
 * -1 means "not one of ours" and the caller drops the event. The original's
 * unchecked indexing below is preserved for the events that do match.
 */
static int Pads_EventSlot(const SDL_Event *ev)
{
#ifdef MUGEN_SDL2_API
    SDL_JoystickID id;
    int i;

    if (ev->type == SDL_JOYHATMOTION) {
        id = ev->jhat.which;
    } else if (ev->type == SDL_JOYAXISMOTION) {
        id = ev->jaxis.which;
    } else {
        id = ev->jbutton.which;
    }
    for (i = 0; i < g_padCount; i++) {
        if (g_pads[i].joystick != NULL && SDL_JoystickInstanceID(g_pads[i].joystick) == id) {
            return i;
        }
    }
    return -1;
#else
    if (ev->type == SDL_JOYHATMOTION) {
        return ev->jhat.which;
    }
    if (ev->type == SDL_JOYAXISMOTION) {
        return ev->jaxis.which;
    }
    return ev->jbutton.which;
#endif
}

void Pads_HandleEvent(const SDL_Event *ev)
{
    int slot;

    if (g_padsActive == 0) {
        /*
         * Diagnostic (port addition): say why a real event is being dropped.
         * A pad that opens but never activates lands here for every event, and
         * silently returning left nothing to distinguish that from a device
         * that sends nothing at all.
         */
        static int logged;
        if (!logged) {
            logged = 1;
            Log_Printf("pads: ignoring joystick event %d: no pad is active (g_padsActive=0)\n",
                       (int)ev->type);
        }
        return;
    }
    slot = Pads_EventSlot(ev);
    if (slot < 0) {
        static int logged_slot;
        if (!logged_slot) {
            logged_slot = 1;
            Log_Printf("pads: ignoring joystick event %d: device is not an open pad slot\n",
                       (int)ev->type);
        }
        return;
    }
#ifdef MUGEN_GAMEPAD_POLICY
    /*
     * The same device delivers both raw joystick events and controller events.
     * When a slot is backed by a game controller the semantic path owns it:
     * letting the raw path through as well pressed two codes per physical
     * button (observed: d-pad Down arrived as controller code 2 AND as raw
     * code 29), and the raw codes 9..12 would collide with the compact
     * layout's X/Y/Z/Start.
     */
    if (Pads_SlotUsesController(slot)) {
        return;
    }
#endif
    /*
     * Fix (port change): the original indexes the pad's button codes without a
     * bound, and the check that guarded it was Android-only. A pad reporting
     * axis 16 maps to local codes 33..34, past PAD_BUTTONS (32), so the write
     * landed in the next pad's slot -- pad 0's axis 16 changed pad 1's state --
     * and on the last slot it ran past g_padButtonState (INPUT_PAD_COUNT + 1
     * bytes) entirely. Devices with more than 15 axes, or more hats than one
     * slot has room for, are ordinary. Bound the local code on every platform;
     * the global code is checked again in Pads_SetButton.
     */
    if (ev->type == SDL_JOYAXISMOTION) {
        unsigned int axis = (unsigned int)ev->jaxis.axis;
        unsigned int last;

        /*
         * Axis 0 and 1 are swapped below, so both land among the first two.
         * The per-axis pair is (axis*2+1, axis*2+2) and every consumer treats
         * PAD_BUTTONS as an exclusive bound: Pads_DeviceEvent clears codes
         * below it, Pads_SetButton refuses anything above the global array.
         * The old test used >, which let axis 15 through with the pair 31/32;
         * 32 is outside the slot, so it stayed held after the device was
         * removed. Accept only when the whole pair is inside the slot.
         */
        if (axis > 1) {
            last = axis * 2u + 2u;
            if (last >= PAD_BUTTONS) {
                return;
            }
        }
    }
    if (ev->type == SDL_JOYHATMOTION && g_pads[slot].hat_base + 4 >= PAD_BUTTONS) {
        return;
    }
    if (ev->type == SDL_JOYHATMOTION) {
        int base = slot * PAD_BUTTONS + g_pads[slot].hat_base + 1;
        static int logged_hat;

        if (logged_hat < 8) {
            logged_hat++;
            Log_Printf("pads: hat %u value 0x%X -> codes %d..%d (slot %d)\n",
                       (unsigned)ev->jhat.hat, (unsigned)ev->jhat.value, base, base + 3, slot);
        }
        pad_set_flag(base, ev->jhat.value & SDL_HAT_UP);
        pad_set_flag(base + 1, ev->jhat.value & SDL_HAT_DOWN);
        pad_set_flag(base + 2, ev->jhat.value & SDL_HAT_LEFT);
        pad_set_flag(base + 3, ev->jhat.value & SDL_HAT_RIGHT);
    }
    if (ev->type == SDL_JOYAXISMOTION) {
        int axis = ev->jaxis.axis;
        int pad = slot;
        int dir, first;

        /* The first two axes are swapped: vertical, then horizontal. */
        if (axis == 0) {
            axis = 1;
        } else if (axis == 1) {
            axis = 0;
        }
        if (ev->jaxis.value < -PAD_AXIS_THRESHOLD || ev->jaxis.value > PAD_AXIS_THRESHOLD) {
            dir = ev->jaxis.value > 0 ? 1 : -1;
        } else {
            dir = 0;
        }
        first = axis * 2 + 1;
        /* Release the button of the other direction (both when centred). */
        if (dir >= 0 && g_padButtonState[pad * PAD_BUTTONS + first] != 0) {
            Pads_SetButton(pad * PAD_BUTTONS + first, 0);
        }
        if (dir <= 0 && g_padButtonState[pad * PAD_BUTTONS + first + 1] != 0) {
            Pads_SetButton(pad * PAD_BUTTONS + first + 1, 0);
        }
        if (dir != 0) {
            int code = pad * PAD_BUTTONS + first + (dir > 0);
            static int logged_axis;

            if (logged_axis < 16 && g_padButtonState[code] != 1) {
                logged_axis++;
                Log_Printf("pads: axis %d value %d -> code %d (slot %d)\n", ev->jaxis.axis,
                           (int)ev->jaxis.value, code, pad);
            }
            if (g_padButtonState[code] != 1) {
                Pads_SetButton(code, 1);
            }
        }
        return;
    }
    if (ev->type == SDL_JOYBUTTONDOWN || ev->type == SDL_JOYBUTTONUP) {
        int button = g_pads[slot].button_base + 1 + ev->jbutton.button;
        int pressed = ev->type == SDL_JOYBUTTONDOWN;
        int code;

        if (button >= PAD_BUTTONS) {
            static int logged_over;
            if (logged_over < 4) {
                logged_over++;
                Log_Printf("pads: button %u maps to code %d, outside a slot (%d); dropped\n",
                           (unsigned)ev->jbutton.button, button, PAD_BUTTONS);
            }
            return;
        }
        code = slot * PAD_BUTTONS + button;
        {
            static int logged_btn;
            if (logged_btn < 16) {
                logged_btn++;
                Log_Printf("pads: button %u %s -> code %d (slot %d)\n",
                           (unsigned)ev->jbutton.button, pressed ? "down" : "up", code, slot);
            }
        }
        if ((signed char)g_padButtonState[code] != pressed) {
            Pads_SetButton(code, pressed);
        }
    }
}

/* 0x00474A60: appends a code to g_padQueue (only while the pads are active), dropping the oldest when full. */
void Pads_QueueCode(int code)
{
    if (g_padsActive == 0) {
        return;
    }
    g_padQueue[g_padQueueWrite] = code;
    g_padQueueWrite++;
    if (g_padQueueWrite >= INPUT_RING_SIZE) {
        g_padQueueWrite = 0;
    }
    if (g_padQueueWrite == g_padQueueRead) {
        g_padQueueRead++;
        if (g_padQueueRead >= INPUT_RING_SIZE) {
            g_padQueueRead = 0;
        }
    }
}

/*
 * 0x00474AC0: takes the oldest code of g_padQueue (0 when the pads are not
 * active). Quirk: with an empty queue it loops forever; callers check
 * Pads_CodePending first.
 */
int Pads_PopCode(void)
{
    int code;

    if (g_padsActive == 0) {
        return 0;
    }
    while (g_padQueueRead == g_padQueueWrite) {
        /* The original spins here (jmp $). */
    }
    code = g_padQueue[g_padQueueRead];
    g_padQueueRead++;
    if (g_padQueueRead >= INPUT_RING_SIZE) {
        g_padQueueRead = 0;
    }
    return code;
}

/* 0x00474B10: 1 if g_padQueue holds a code. */
int Pads_CodePending(void)
{
    return g_padsActive != 0 && g_padQueueRead != g_padQueueWrite;
}

/*
 * 0x00474B30: looks code (an input code, see Input_TranslateKey) up in
 * count key maps of KEY_COUNT keyboard codes. Returns the bit of the first
 * matching key (1 << (key - 1)) in the low half and the map index in the
 * high half, or 0.
 */
long long Input_FindKey(int *const *maps, int count, int code)
{
    int m;

    for (m = 0; m < count; m++) {
        int keys[KEY_COUNT];
        int i;

        memcpy(keys, maps[m], sizeof(keys));
        for (i = 0; i < KEY_COUNT; i++) {
            /* Input_TranslateKey, inlined. */
            int k = keys[i] & ~KEYBOARD_RELEASE;
            if (k == KEY_NONE || k == 0) {
                k = INPUT_NONE;
            } else if (keys[i] & KEYBOARD_RELEASE) {
                k |= INPUT_RELEASE;
            }
            keys[i] = k;
        }
        for (i = 1; i < KEY_COUNT; i++) {
            if (code == keys[i]) {
                return ((long long)m << 32) | (unsigned int)(1 << (i - 1));
            }
        }
    }
    return 0;
}

#if defined(ANDROID_PORT) || defined(MUGEN_SDL2_API)
/*
 * SDL2 hotplug: attach a device that appeared, release one that left, keeping
 * existing pad slots stable. Written for Android but not Android-specific --
 * the desktop backend simply never called it, so a controller plugged in or
 * reconnected after start-up was ignored until the pads were reinitialised.
 * Matching is by SDL_JoystickInstanceID, which is what both platforms use.
 */
void Pads_DeviceEvent(const SDL_Event *event)
{
    int i, slot = -1;
    SDL_Joystick *joystick;
    SDL_JoystickID id;
    Pad *pad;

    if (g_game != NULL && g_game->nojoy) {
        return;
    }
    if (event->type == SDL_JOYDEVICEREMOVED) {
        for (i = 0; i < g_padCount; i++) {
            int button;
            if (g_pads[i].joystick == NULL ||
                SDL_JoystickInstanceID(g_pads[i].joystick) != event->jdevice.which) {
                continue;
            }
            for (button = 1; button < PAD_BUTTONS; button++) {
                int code = i * PAD_BUTTONS + button;
                if (g_padButtonState[code]) {
                    Pads_SetButton(code, 0);
                }
            }
            SDL_JoystickClose(g_pads[i].joystick);
#ifdef MUGEN_GAMEPAD_POLICY
            /*
             * The semantic handle for this slot has to go with the raw one.
             * Leaving it open left g_padController[i] pointing at the old
             * instance: after a reconnect the raw joystick was the new instance
             * while the controller was still the old one, raw events stayed
             * blocked because the slot "uses a controller", and the new
             * semantic events never matched, so the pad stopped working.
             *
             * Only when the controller path is compiled in: the semantic
             * handle array is part of it (see the sidecar declaration).
             * The desktop backend keeps its raw-joystick-only lifecycle here.
             */
            if (g_padController[i] != NULL) {
                SDL_GameControllerClose(g_padController[i]);
                g_padController[i] = NULL;
            }
#endif
            Pads_ClearDirectionSources(i);
            memset(&g_pads[i], 0, sizeof(Pad));
#ifdef MUGEN_GAMEPAD_POLICY
            /* The slot has no controller any more: put the raw [P<n> Joystick]
             * bindings back and publish them, so the player is not left bound
             * to semantic codes no device will send. This runs on every SDL2
             * target (MUGEN_GAMEPAD_POLICY); only the SDL 1.2 reference build
             * keeps the original raw-joystick lifecycle. */
            Config_RepublishJoystick(i);
#endif
        }
        return;
    }
    id = SDL_JoystickGetDeviceInstanceID(event->jdevice.which);
    if (id < 0) {
        Log_Printf("pads: hotplug device %d identity failed: %s\n", event->jdevice.which, SDL_GetError());
        return;
    }
    for (i = 0; i < PAD_MAX; i++) {
        if (g_pads[i].joystick != NULL && SDL_JoystickInstanceID(g_pads[i].joystick) == id) {
            return; /* already opened at startup */
        }
        if (slot < 0 && g_pads[i].joystick == NULL) {
            slot = i;
        }
    }
    if (slot < 0) {
        return;
    }
    joystick = SDL_JoystickOpen(event->jdevice.which);
    if (joystick == NULL) {
        Log_Printf("pads: SDL_JoystickOpen(%d) failed during hotplug: %s\n", event->jdevice.which, SDL_GetError());
        return;
    }
    Pads_LogDevice(joystick, event->jdevice.which);
    pad = &g_pads[slot];
    pad->joystick = joystick;
#ifdef MUGEN_GAMEPAD_POLICY
    Pads_AttachController(slot, event->jdevice.which);
#endif
    Pads_ClearDirectionSources(slot);
    pad->hat_base = SDL_JoystickNumAxes(joystick) * 2;
    pad->button_base = pad->hat_base + SDL_JoystickNumHats(joystick) * 4;
    pad->button_count = pad->button_base + SDL_JoystickNumButtons(joystick);
    if (pad->button_count > PAD_BUTTONS) {
        pad->button_count = PAD_BUTTONS;
    }
    if (g_padCount <= slot) {
        g_padCount = slot + 1;
    }
    g_padsActive = 1;
    SDL_JoystickEventState(SDL_ENABLE);
#ifdef MUGEN_GAMEPAD_POLICY
    /* A generic joystick may have no controller mapping. It still needs its
     * configured raw bindings published after a pad-less startup. */
    if (!Pads_SlotUsesController(slot)) Config_RepublishJoystick(slot);
#endif
    Log_Printf("pads: attached device %d to slot %d, instance=%d\n", event->jdevice.which, slot, (int)id);
}
#endif

/*
 * Diagnostic (port addition): the joystick bindings the config asked for.
 *
 * Printed after the options reload, because Options_Load rereads
 * [P1 Joystick]/[P2 Joystick] and a platform policy may have replaced them
 * since. Comparing these codes with the "pads: N ... -> codes" line from
 * Pads_Init is what shows whether a pad that opens will actually press
 * anything: the same physical button produces different engine codes on a
 * controller with a different axis or hat count.
 */
void Pads_LogBindings(void)
{
    static const struct {
        int key;
        const char *name;
    } names[] = {
        { KEY_JUMP, "Jump" },   { KEY_RIGHT, "Right" }, { KEY_CROUCH, "Crouch" }, { KEY_LEFT, "Left" },
        { KEY_A, "A" },         { KEY_B, "B" },         { KEY_C, "C" },           { KEY_X, "X" },
        { KEY_Y, "Y" },         { KEY_Z, "Z" },         { KEY_START, "Start" },
    };
    int player;

    if (g_game == NULL) {
        return;
    }
    for (player = 0; player < 2; player++) {
        const int *jk = g_game->joy_keys[player];
        int unbound = 0;
        int n;

        /* Built piecewise with an explicit cursor: an accumulating snprintf
         * offset can exceed the buffer when a value is long, and the previous
         * version then underflowed the remaining-size argument. */
        {
            char line[192];
            int pos = 0;

            line[0] = '\0';
            for (n = 0; n < (int)(sizeof(names) / sizeof(names[0])); n++) {
                int v = jk[names[n].key];
                int room = (int)sizeof(line) - pos;

                if (v == JOY_NONE) {
                    unbound++;
                }
                if (room <= 1) {
                    break;
                }
                pos += snprintf(line + pos, (size_t)room, "%s%s=%d", pos != 0 ? " " : "", names[n].name, v);
                if (pos >= (int)sizeof(line)) {
                    pos = (int)sizeof(line) - 1;
                }
            }
            Log_Printf("pads: P%d joystick bindings from mugen.cfg: %s\n", player + 1, line);
        }
        if (unbound == (int)(sizeof(names) / sizeof(names[0]))) {
            Log_Printf("pads: P%d has no joystick bound (every entry is JOY_NONE)\n", player + 1);
        }
    }
}
