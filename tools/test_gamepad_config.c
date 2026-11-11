/*
 * Regression test for the SDL2 key menu -> cfg -> reload/hotplug path.
 * Runs the production KeyConfig_Run, Options_Save/Load and SDL virtual pads.
 * Rendering, timing and the input-map rebuild are stubbed at their boundaries.
 */
#include "mugen.h"
#include <assert.h>

#ifdef main
#undef main
#endif

#ifdef MUGEN_VITA
void Keyboard_HandleKey(int key, int pressed) { (void)key; (void)pressed; }
#endif

static GameVars game;
static Font options_font;
static int map_rebuilds;

typedef struct MenuEvent {
    int key;
    int pad;
    int remove_device;
} MenuEvent;

static MenuEvent menu[64];
static int menu_count, menu_pos;
static int devices[2];
static const int slots[] = {
    KEY_JUMP, KEY_CROUCH, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B,
    KEY_C, KEY_X, KEY_Y, KEY_Z, KEY_START
};
static const int p1_keys[] = { 1, 2, 3, 4, 8, 6, 7, 5, 9, 10, 11 };
static const int p2_keys[] = { 1, 2, 3, 4, 6, 5, 7, 8, 9, 10, 0 };

void Log_Printf(const char *fmt, ...) { (void)fmt; }
void Error_Printf(const char *fmt, ...) { (void)fmt; }
DrawParams *DrawParams_Init(DrawParams *dp) { (void)dp; abort(); }
void Font_DrawText(const char *text, Font *font, int x, int y, int align, int bank, int r, int g, int b)
{ (void)text; (void)font; (void)x; (void)y; (void)align; (void)bank; (void)r; (void)g; (void)b; abort(); }
void Gfx_SoftFillRectSub(int x1, int y1, int x2, int y2, unsigned int color)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)color; abort(); }
void Gfx_SoftFillRectAdd(int x1, int y1, int x2, int y2, unsigned int color)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)color; abort(); }
void Error_Fatal(const char *fmt, ...)
{
    fprintf(stderr, "%s", fmt);
    abort();
}
void Bgm_SetVolume(double volume) { (void)volume; }
void Sound_PlaySystem(Snd *snd, int group, int channel, int number, double volume)
{ (void)snd; (void)group; (void)channel; (void)number; (void)volume; }
void Input_InitPads(void) {}
int Input_Remap(void) { map_rebuilds++; return 1; }
int Input_UpdateMap(void) { map_rebuilds++; return 1; }
void Input_ResetState(void) {}
int Input_CodePending(void) { return 0; }
int Gfx_SetRenderTarget(SpriteSet *target, int index) { (void)target; (void)index; return 1; }
unsigned int Video_MapRGB(int r, int g, int b)
{ (void)r; (void)g; (void)b; return 0; }
void Video_HandleFullscreenKey(int *key) { (void)key; }
void Timer_SetFps(int fps) { g_frameRate = fps; }
void Timer_ResetFrame(void) {}
void SysDraw_Clear(SysDrawList *list) { (void)list; }
void Screen_EndFrame(void) {}
void Bg_SetCamera(Bg *bg, float x, float y, float ox, float oy, float zoom, float cx, float cy)
{ (void)bg; (void)x; (void)y; (void)ox; (void)oy; (void)zoom; (void)cx; (void)cy; }
void Bg_Tick(Bg *bg) { (void)bg; }
void Bg_SetScale(Bg *bg, float x, float y) { (void)bg; (void)x; (void)y; }
void Bg_Draw(Bg *bg, int layer) { (void)bg; (void)layer; }
void Font_DrawTextEx(const char *text, Font *font, const DrawParams *dp, int align, int bank, int r, int g, int b)
{ (void)text; (void)font; (void)dp; (void)align; (void)bank; (void)r; (void)g; (void)b; }

static void remove_pad(int slot)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYDEVICEREMOVED;
    event.jdevice.which = SDL_JoystickInstanceID(g_pads[slot].joystick);
    Pads_DeviceEvent(&event);
    assert(SDL_JoystickDetachVirtual(devices[slot]) == 0);
    devices[slot] = -1;
}

static void add_pad(int slot, int mapped)
{
    SDL_Event event;
    int index = SDL_JoystickAttachVirtual(mapped ? SDL_JOYSTICK_TYPE_GAMECONTROLLER : SDL_JOYSTICK_TYPE_UNKNOWN,
                                        SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(index >= 0);
    devices[slot] = index;
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYDEVICEADDED;
    event.jdevice.which = index;
    Pads_DeviceEvent(&event);
    assert(Pads_SlotUsesController(slot) == mapped);
}

void Events_Poll(void)
{
    MenuEvent *event;
    game.skip_draw = 1;
    assert(menu_pos < menu_count);
    event = &menu[menu_pos++];
    if (event->key) {
        g_keyboardQueue[g_keyboardQueueWrite++] = event->key;
    }
    if (event->pad) {
        Pads_QueueCode(event->pad);
    }
    if (event->remove_device) {
        remove_pad(event->remove_device - 1);
    }
}

static void enqueue(int key, int pad, int remove_device)
{
    assert(menu_count < (int)(sizeof(menu) / sizeof(menu[0])));
    menu[menu_count].key = key;
    menu[menu_count].pad = pad;
    menu[menu_count].remove_device = remove_device;
    menu_count++;
}

static void reset_menu(void)
{
    menu_count = menu_pos = 0;
    enqueue(0, 0, 0); /* The entry-point poll precedes the menu's frame loop. */
}

static int gameplay_events;
static void record_gameplay(int code, int pressed)
{
    (void)code;
    (void)pressed;
    gameplay_events++;
}

static void controller_button(int slot, SDL_GameControllerButton button, int pressed)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = pressed ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
    event.cbutton.which = SDL_JoystickInstanceID(g_pads[slot].joystick);
    event.cbutton.button = (Uint8)button;
    Pads_HandleControllerEvent(&event);
}

static void read_frame(ScreenLoop *screen)
{
    menu_count = menu_pos = 0;
    enqueue(0, 0, 0);
    Screen_ReadInput(screen);
    assert(menu_pos == menu_count);
}

static void check_menu_actions(void)
{
    ScreenLoop screen;
    TitleState title;
    int p, b, k;
    int before[2][KEY_COUNT];
    int keyboard_before[2][KEY_COUNT];
    static const SDL_GameControllerButton buttons[] = {
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_RIGHTSTICK
    };

    memcpy(before, game.joy_keys, sizeof(before));
    memcpy(keyboard_before, game.keys, sizeof(keyboard_before));
    g_padHandler = record_gameplay;
    for (p = 0; p < 2; p++) {
        memset(&screen, 0, sizeof(screen));
        screen.kind = 4;
        screen.ticks = 1;
        game.defocus_paused = 0;
        game.training_menu = p; /* P1 normal pause, P2 training pause */
        controller_button(p, SDL_CONTROLLER_BUTTON_BACK, 1);
        read_frame(&screen);
        assert(screen.key == ENGINE_KEY_PAUSE && !screen.done && !screen.input2);
        Game_ReadInput(&screen);
        assert(game.defocus_paused);
        controller_button(p, SDL_CONTROLLER_BUTTON_BACK, 1); /* no repeat */
        read_frame(&screen);
        assert(!screen.key);
        controller_button(p, SDL_CONTROLLER_BUTTON_BACK, 0);
        read_frame(&screen);
        Game_ReadInput(&screen);
        assert(game.defocus_paused && !screen.key && !screen.done);
        controller_button(p, SDL_CONTROLLER_BUTTON_BACK, 1);
        read_frame(&screen);
        Game_ReadInput(&screen);
        assert(!game.defocus_paused);
        controller_button(p, SDL_CONTROLLER_BUTTON_BACK, 0);
        read_frame(&screen);
        game.defocus_paused = p; /* Escape from running and paused matches */
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1);
        read_frame(&screen);
        assert(screen.done && screen.key == SDLK_ESCAPE);
        Game_ReadInput(&screen);
        assert(!game.defocus_paused);
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0);
        screen.done = 0;
        read_frame(&screen);
        assert(!screen.done && !screen.key);

        for (k = 2; k <= 3; k++) { /* character selection and options */
            for (b = 0; b < 2; b++) {
                memset(&screen, 0, sizeof(screen));
                screen.kind = k;
                controller_button(p, buttons[b], 1);
                read_frame(&screen);
                assert(screen.key == SDLK_ESCAPE && screen.done && !screen.input && !screen.input2);
                controller_button(p, buttons[b], 0);
                screen.done = 0;
                read_frame(&screen);
                assert(!screen.key && !screen.done);
            }
        }

        memset(&screen, 0, sizeof(screen));
        memset(&title, 0, sizeof(title));
        screen.kind = 1;
        title.items[TITLE_ITEM_EXIT] = "Exit";
        game.demo_wait = -1;
        controller_button(p, SDL_CONTROLLER_BUTTON_BACK, 1);
        read_frame(&screen);
        Title_Update(&screen, &title);
        assert(!title.chosen && title.item == 0 && !screen.done);
        controller_button(p, SDL_CONTROLLER_BUTTON_BACK, 0);
        read_frame(&screen);
        Title_Update(&screen, &title);
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1);
        read_frame(&screen);
        Title_Update(&screen, &title);
        assert(title.item == TITLE_ITEM_EXIT && !title.chosen && !screen.done && !screen.fade_out);
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0);
        read_frame(&screen);
        Title_Update(&screen, &title);
        assert(!title.chosen && !screen.fade_out);
        title.box_shown = 1;
        title.item = TITLE_ITEM_ARCADE;
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1);
        read_frame(&screen);
        Title_Update(&screen, &title);
        assert(!title.box_shown && title.box_moving && !title.chosen && title.item == TITLE_ITEM_ARCADE);
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0);
        read_frame(&screen);
        Title_Update(&screen, &title);
        title.box_moving = 0;
        title.items[TITLE_ITEM_EXIT] = NULL;
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1);
        read_frame(&screen);
        Title_Update(&screen, &title);
        assert(!title.chosen && title.item == TITLE_ITEM_ARCADE); /* hidden Exit */
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0);
        read_frame(&screen);
        Title_Update(&screen, &title);
        title.items[TITLE_ITEM_EXIT] = "Exit";
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1);
        read_frame(&screen);
        Title_Update(&screen, &title);
        controller_button(p, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0);
        read_frame(&screen);
        Title_Update(&screen, &title);
        assert(title.item == TITLE_ITEM_EXIT && !title.chosen);
        screen.input = 0x10; /* a separate ordinary attack confirms */
        Title_Update(&screen, &title);
        assert(title.chosen && screen.fade_out);
    }
    assert(gameplay_events == 0);
    for (p = 0; p < 2; p++) {
        assert(!g_padButtonState[p * PAD_BUTTONS + PAD_SELECT_CODE]);
        assert(!g_padButtonState[p * PAD_BUTTONS + PAD_BACK_CODE]);
        for (b = 0; b < 2; b++) {
            int code = p * PAD_BUTTONS + (b ? PAD_BACK_CODE : PAD_SELECT_CODE);
            reset_menu();
            enqueue(p ? ENGINE_KEY_F2 : ENGINE_KEY_F1, 0, 0);
            enqueue(0, p * PAD_BUTTONS + 5, 0); /* partially edit Config all */
            enqueue(0, code, 0);              /* cancel edits */
            enqueue(0, code | PAD_RELEASE, 0); /* release must not confirm */
            enqueue(0, code, 0);              /* leave the config screen */
            KeyConfig_Run(1, 0);
            assert(menu_pos == menu_count);
            assert(!memcmp(before, game.joy_keys, sizeof(before)));
            reset_menu();
            enqueue(p ? ENGINE_KEY_F2 : ENGINE_KEY_F1, 0, 0);
            enqueue(SDLK_q, 0, 0);
            enqueue(0, code, 0);
            enqueue(0, code | PAD_RELEASE, 0);
            enqueue(0, code, 0);
            KeyConfig_Run(0, 0);
            assert(menu_pos == menu_count);
            assert(!memcmp(keyboard_before, game.keys, sizeof(keyboard_before)));
        }
    }
    /* Ordinary mapped attacks still reach the gameplay callback. */
    controller_button(0, SDL_CONTROLLER_BUTTON_A, 1);
    controller_button(0, SDL_CONTROLLER_BUTTON_A, 0);
    assert(gameplay_events == 2);
    g_padQueueRead = g_padQueueWrite;

    /* Generic joystick buttons 12/13 are not stolen as system controls. */
    remove_pad(1);
    add_pad(1, 0);
    assert(Pads_MenuAction(PAD_BUTTONS + PAD_SELECT_CODE, PAD_CONTEXT_MENU) == PAD_MENU_NONE);
    assert(Pads_MenuAction(PAD_BUTTONS + PAD_BACK_CODE, PAD_CONTEXT_MATCH) == PAD_MENU_NONE);
    {
        SDL_Event event;
        memset(&event, 0, sizeof(event));
        event.type = SDL_JOYBUTTONDOWN;
        event.jbutton.which = SDL_JoystickInstanceID(g_pads[1].joystick);
        event.jbutton.button = 0; /* 6 axes => first button is raw code 13 */
        gameplay_events = 0;
        Pads_HandleEvent(&event);
        assert(gameplay_events == 1 && g_padButtonState[PAD_BUTTONS + 13]);
    }
    g_padHandler = NULL;
    puts("gamepad menu: P1/P2 pause, Back, safe title exit, remap cancellation and raw fallback passed");
}

static void configure_pads(void)
{
    int i, p;
    reset_menu();
    for (p = 0; p < 2; p++) {
        const int *keys = p == 0 ? p1_keys : p2_keys;
        enqueue(p == 0 ? ENGINE_KEY_F1 : ENGINE_KEY_F2, 0, 0);
        /* A press from the other slot must not fill the selected player's row. */
        enqueue(0, (1 - p) * PAD_BUTTONS + 5, 0);
        for (i = 0; i < (int)(sizeof(slots) / sizeof(slots[0])); i++) {
            enqueue(keys[i] == 0 ? SDLK_SPACE : 0, keys[i] == 0 ? 0 : p * PAD_BUTTONS + keys[i], 0);
        }
    }
    enqueue(SDLK_ESCAPE, 0, 0);
    KeyConfig_Run(1, 0);
    assert(menu_pos == menu_count);
}

static Ini *open_config(void)
{
    Ini *ini = Ini_Open("data/mugen.cfg");
    assert(ini);
    return ini;
}

static void check_group(Ini *ini, const char *group, const int *keys)
{
    static const char *const names[] = {
        "Jump", "Crouch", "Left", "Right", "A", "B", "C", "X", "Y", "Z", "Start"
    };
    int i;
    assert(Ini_SelectGroup(ini, group) != -1);
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        const char *value = Ini_GetValue(ini, names[i]);
        assert(value && atoi(value) == keys[i]);
    }
}

static char *read_config(void)
{
    FILE *file = fopen("data/mugen.cfg", "rb");
    char *text;
    long size;
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0 && fseek(file, 0, SEEK_SET) == 0);
    text = malloc((size_t)size + 1);
    assert(text && fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = '\0';
    fclose(file);
    return text;
}

static void check_saved(int raw_p2)
{
    static const int raw1[] = { 9, 10, 11, 12, 13, 14, 20, 15, 16, 18, 24 };
    static const int raw2[] = { 41, 42, 43, 44, 46, 47, 52, 45, 48, 50, 54 };
    int raw2_edited[11];
    Ini *ini = open_config();
    char *text;
    int i;
    for (i = 0; i < 11; i++) raw2_edited[i] = p2_keys[i] ? PAD_BUTTONS + p2_keys[i] : 0;
    check_group(ini, "P1 Gamepad", p1_keys);
    if (!raw_p2) {
        check_group(ini, "P2 Gamepad", p2_keys);
    } else {
        assert(Ini_SelectGroup(ini, "P2 Gamepad") != -1);
        assert(atoi(Ini_GetValue(ini, "A")) == 5);
    }
    check_group(ini, "P1 Joystick", raw1);
    check_group(ini, "P2 Joystick", raw_p2 ? raw2_edited : raw2);
    assert(Ini_SelectGroup(ini, "Other") != -1);
    assert(!strcmp(Ini_GetValue(ini, "Keep"), "123"));
    Ini_Free(ini);
    text = read_config();
    assert(strstr(text, "; keep this comment"));
    if (strstr(text, "; keep this inline comment")) {
        assert(strstr(text, "A = 8 ; keep this inline comment"));
    }
    free(text);
}

static void check_active(void)
{
    int i;
    for (i = 0; i < 11; i++) {
        assert(game.joy_keys[0][slots[i]] == p1_keys[i]);
        assert(game.joy_keys[1][slots[i]] ==
               (p2_keys[i] ? PAD_BUTTONS + p2_keys[i] : JOY_NONE));
    }
}

int main(int argc, char **argv)
{
    int raw_p2;
    int invalid[KEY_COUNT];
    char *saved, *again, *original;
    assert(argc == 2);
    raw_p2 = !strcmp(argv[1], "mixed");
    assert(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0);
    memset(&game, 0, sizeof(game));
    g_game = &game;
    game.options_font = &options_font;
    game.safe_mode = 1;
    game.use_keyboard[0] = game.use_keyboard[1] = 1;
    g_keyboardActive = 1;
    g_frameRate = 60;
    devices[0] = devices[1] = -1;

    original = read_config();
    Options_Load();
    if (!strcmp(argv[1], "late-raw")) {
        /* The initial scan disabled runtime input because no device existed.
         * A later generic joystick must re-enable its saved raw bindings. */
        assert(!Pads_Init());
        /* Input_InitPads is stubbed for the menu harness: reproduce its
         * documented no-device result at this boundary. */
        game.use_joystick[0] = game.use_joystick[1] = 0;
        memset(game.joy_keys_active, 0, sizeof(game.joy_keys_active));
        add_pad(0, 0);
        assert(game.use_joystick[0] && game.joystick_type[0]);
        assert(game.joy_keys[0][KEY_A] == 13 && game.joy_keys_active[0][KEY_A] == 13);
        remove_pad(0);
        Input_DeinitPads();
        SDL_Quit();
        free(original);
        assert(map_rebuilds > 0);
        puts("gamepad cfg: late raw joystick is detected and its bindings become active");
        return 0;
    }
    add_pad(0, 1);
    add_pad(1, !raw_p2);
    if (!strcmp(argv[1], "menu-actions")) {
        check_menu_actions();
    } else if (!strcmp(argv[1], "reload")) {
        check_active();
        check_saved(0);
    } else {
        configure_pads();
        check_active();
        memcpy(invalid, game.joy_keys[0], sizeof(invalid));
        invalid[KEY_A] = PAD_BUTTONS + 5;
        assert(!Config_SetJoystickKeys(0, 1, invalid));
        check_active();
        if (!strncmp(argv[1], "missing", 7)) {
            FILE *file = fopen("data/mugen.cfg", "wb");
            assert(file && fwrite(original, 1, strlen(original), file) == strlen(original));
            fclose(file);
        }
        Options_Save();
        check_saved(raw_p2);
        saved = read_config();
        Options_Save();
        again = read_config();
        assert(!strcmp(saved, again));
        free(saved);
        free(again);
        Options_Load();
        check_active();

        /* Disabling a player's input must not erase its persisted mapping. */
        game.use_joystick[0] = 0;
        Options_Save();
        check_saved(raw_p2);
        game.use_joystick[0] = 1;

        /* Default uses the same per-player normalization as Config all. */
        reset_menu();
        enqueue(ENGINE_KEY_F3, 0, 0);
        enqueue(ENGINE_KEY_F4, 0, 0);
        enqueue(SDLK_ESCAPE, 0, 0);
        KeyConfig_Run(1, 0);
        Options_Save();
        {
#ifdef MUGEN_VITA
            static const int defaults[] = { 1, 2, 3, 4, 8, 9, 6, 5, 10, 7, 11 };
#else
            static const int defaults[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
#endif
            Ini *ini = open_config();
            check_group(ini, "P1 Gamepad", defaults);
            if (!raw_p2) check_group(ini, "P2 Gamepad", defaults);
            Ini_Free(ini);
        }
        configure_pads();
        Options_Save();
        check_saved(raw_p2);

        /* A detach must not make Save overwrite the stored gamepad mappings. */
        remove_pad(1);
        remove_pad(0);
        Options_Save();
        check_saved(raw_p2);
        add_pad(0, 1);
        add_pad(1, !raw_p2);
        check_active();

        /* Hotplug while assigning cancels the transaction. */
        reset_menu();
        enqueue(ENGINE_KEY_F1, 0, 0);
        enqueue(0, 5, 0);
        enqueue(0, 0, 2);
        KeyConfig_Run(1, 0);
        Options_Save();
        check_saved(raw_p2);
    }
    if (devices[1] != -1) remove_pad(1);
    if (devices[0] != -1) remove_pad(0);
    SDL_Quit();
    free(original);
    assert(map_rebuilds > 0);
    printf("gamepad cfg: %s passed (%u-bit host)\n", argv[1], (unsigned)(sizeof(void *) * 8));
    return 0;
}

/* Renderer policy boundary is outside the gamepad/config fixture. */
void Video_SetSwapInterval(int interval) { (void)interval; }
