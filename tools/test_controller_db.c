/* Production pads.c + SDL2 virtual joysticks. No physical hardware required. */
#include "mugen.h"
#include <assert.h>
#include <stdarg.h>
#ifdef main
#undef main
#endif
static int db_opens, republished;
static char messages[32768];
void Log_Printf(const char *fmt, ...)
{
    size_t used = strlen(messages);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(messages + used, sizeof(messages) - used, fmt, ap);
    va_end(ap);
}
void Config_RepublishJoystick(int player) { (void)player; republished++; }
static SDL_JoystickGUID device_guid(int index)
{
    if (index == -1) return SDL_JoystickGetGUIDFromString("1900bb3e4b4800000011000000010000");
    return SDL_JoystickGetDeviceGUID(index);
}
static SDL_RWops *open_db(const char *path, const char *mode)
{
    assert(!strcmp(path, "data/gamecontrollerdb.txt") && !strcmp(mode, "rb"));
    db_opens++;
    return SDL_RWFromFile(path, mode);
}
#define SDL_JoystickGetDeviceGUID device_guid
#define SDL_RWFromFile open_db
#include "../src/pads.c"

static int attach(void)
{
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 2, 16, 0);
    assert(index >= 0);
    return index;
}
static void write_db(const char *guid, const char *scenario, int button)
{
    FILE *f = fopen("data/gamecontrollerdb.txt", "wb");
    const char *platform = SDL_GetPlatform();
    assert(f);
    if (!strcmp(scenario, "invalid")) {
        fputs("not a controller mapping\n", f);
    } else if (!strcmp(scenario, "empty")) {
        fputs("# optional database with no mappings\n", f);
    } else {
        if (!strcmp(scenario, "platform")) platform = !strcmp(platform, "Linux") ? "Windows" : "Linux";
        fprintf(f, "# test file, CRLF lines\r\n%s,Local Test Pad,a:b%d,b:b0,back:b12,start:b13,leftstick:b14,rightstick:b15,leftx:a0,lefty:a1,platform:%s,\r\n",
                guid, button, platform);
        fprintf(f, "1900bb3e4b4800000011000000010000,Local GO Override,a:b0,back:b3,start:b4,leftstick:b5,rightstick:b6,platform:%s,\r\n", platform);
    }
    assert(fclose(f) == 0);
}
static void check_button(int button)
{
    SDL_GameControllerButtonBind bind;
    assert(Pads_SlotUsesController(0));
    bind = SDL_GameControllerGetBindForButton(g_padController[0], SDL_CONTROLLER_BUTTON_A);
    assert(bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && bind.value.button == button);
    bind = SDL_GameControllerGetBindForButton(g_padController[0], SDL_CONTROLLER_BUTTON_BACK);
    assert(bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && bind.value.button == 12);
}
/* Exact field values with either comma or end-of-string termination. */
static int has_field(const char *mapping, const char *field)
{
    const char *p = mapping;
    size_t n = strlen(field);
    while ((p = strstr(p, field)) != NULL) {
        if (p > mapping && p[-1] == ',' && (p[n] == ',' || p[n] == '\0')) return 1;
        p++;
    }
    return 0;
}
static void check_fixups(void)
{
    SDL_JoystickGUID guid = device_guid(-1);
    char *before = SDL_GameControllerMappingForGUID(guid);
    char *after;
    assert(before && has_field(before, "back:b3") && has_field(before, "rightstick:b6"));
    Pads_CompleteControllerMapping(-1);
    after = SDL_GameControllerMappingForGUID(guid);
    assert(after && !strcmp(before, after)); /* local complete mapping wins */
    SDL_free(before); SDL_free(after);
    assert(SDL_GameControllerAddMapping("1900bb3e4b4800000011000000010000,Partial GO Override,a:b0,back:b7,") >= 0);
    Pads_CompleteControllerMapping(-1);
    after = SDL_GameControllerMappingForGUID(guid);
    assert(after && has_field(after, "back:b7") && has_field(after, "start:b13") &&
           has_field(after, "leftstick:b14") && has_field(after, "rightstick:b15"));
    assert(!strstr(after, "back:b12")); /* preserve partial user mapping */
    before = after;
    Pads_CompleteControllerMapping(-1);
    after = SDL_GameControllerMappingForGUID(guid);
    assert(after && !strcmp(before, after)); /* completion is idempotent */
    SDL_free(before); SDL_free(after);
    assert(!Pads_MappingHasBinding("guid,back:b99,a:b0,", "back", 4));
    assert(!Pads_MappingHasBinding("guid,name,rightstick:b1,", "stick", 5));
    assert(Pads_MappingHasBinding("guid,name,back:,", "back", 4));
}
int main(int argc, char **argv)
{
    int index, fallback;
    char guid[33];
    const char *scenario;
    SDL_Event event;
    assert(argc == 2);
    scenario = argv[1];
    SDL_setenv("SDL_GAMECONTROLLERCONFIG", "", 1);
    SDL_setenv("SDL_GAMECONTROLLERCONFIG_FILE", "", 1);
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SKIP: SDL input unavailable: %s\n", SDL_GetError()); return 77;
    }
    if (SDL_NumJoysticks() != 0) {
        fprintf(stderr, "SKIP: isolate physical input devices before running this fixture\n"); SDL_Quit(); return 77;
    }
    index = attach();
    SDL_JoystickGetGUIDString(device_guid(index), guid, sizeof(guid));
    assert(!SDL_IsGameController(index));
    if (strcmp(scenario, "missing")) write_db(guid, scenario, 1);
    fallback = !strcmp(scenario, "missing") || !strcmp(scenario, "invalid") ||
               !strcmp(scenario, "empty") || !strcmp(scenario, "platform");
    if (!strcmp(scenario, "hotplug")) {
        assert(SDL_JoystickDetachVirtual(index) == 0);
        assert(!Pads_Init());
        assert(!Pads_Init() && db_opens == 1); /* load once, even without pads */
        index = attach();
    }
    if (!strcmp(scenario, "hotplug") || !strcmp(scenario, "hotplug-no-scan")) {
        memset(&event, 0, sizeof(event));
        event.type = SDL_JOYDEVICEADDED; event.jdevice.which = index;
        Pads_DeviceEvent(&event);
        assert(g_padsActive && republished == 1);
        check_button(1);
    } else {
        assert(Pads_Init() > 0 && g_padsActive);
        if (fallback) assert(!Pads_SlotUsesController(0));
        else check_button(1);
    }
    assert(db_opens == 1);
    Pads_LoadControllerMappings(); assert(db_opens == 1);
    if (!fallback) check_fixups();
    if (!strcmp(scenario, "reload")) {
        Input_DeinitPads();
        write_db(guid, scenario, 2);
        messages[0] = '\0';
        assert(Pads_Init() > 0 && db_opens == 2);
        check_button(2);
        assert(strstr(messages, "0 new mappings (existing entries may be updated)"));
    }
    if (!strcmp(scenario, "missing")) assert(strstr(messages, "optional data/gamecontrollerdb.txt unavailable"));
    Input_DeinitPads();
    assert(SDL_JoystickDetachVirtual(index) == 0);
    SDL_Quit();
    printf("ok: controller db %s (%u-bit host)\n", scenario, (unsigned)(sizeof(void *) * 8));
    return 0;
}
