/* Deterministic SDL2 driver boundary around the real pads.c. No OS input or
 * SDL library: failures and service refcounts are controlled independently. */
#include "mugen.h"
#include <assert.h>
#include <stdarg.h>

static int joy_refs, ctrl_refs, fail_joy, fail_ctrl, devices, pending_devices;
static int enum_error, open_error, enum_calls, init_calls, close_calls, pumps;
static int joy_object;
static const char *error_text;
static char messages[8192];
void Log_Printf(const char *fmt, ...)
{
    size_t used=strlen(messages);
    va_list args;
    va_start(args,fmt);vsnprintf(messages+used,sizeof(messages)-used,fmt,args);va_end(args);
}
static Uint32 test_was_init(Uint32 flags)
{
    Uint32 state=(joy_refs ? SDL_INIT_JOYSTICK : 0) | (ctrl_refs ? SDL_INIT_GAMECONTROLLER : 0);
    return flags ? state & flags : state;
}
static int test_init(Uint32 flags)
{
    init_calls++;
    if ((flags & SDL_INIT_JOYSTICK) && fail_joy) {error_text="mock joystick failure";return -1;}
    if ((flags & SDL_INIT_GAMECONTROLLER) && fail_ctrl) {error_text="mock controller failure";return -1;}
    if (flags & SDL_INIT_JOYSTICK) joy_refs++;
    if (flags & SDL_INIT_GAMECONTROLLER) {ctrl_refs++;joy_refs++;}
    return 0;
}
static void test_quit(Uint32 flags)
{
    if (flags & SDL_INIT_GAMECONTROLLER) {assert(ctrl_refs>0);ctrl_refs--;joy_refs--;}
    if (flags & SDL_INIT_JOYSTICK) {assert(joy_refs>0);joy_refs--;}
}
static const char *test_error(void) {return error_text ? error_text : "";}
static int test_num(void)
{
    enum_calls++;
    if(enum_error){error_text="mock enumeration failure";return -1;}
    return joy_refs ? devices : 0;
}
static void test_pump(void) {pumps++;if(pending_devices){devices=pending_devices;pending_devices=0;}}
static SDL_Joystick *test_open(int index)
{if(open_error){error_text="mock open failure";return NULL;}return (SDL_Joystick *)&joy_object;}
static void test_close(SDL_Joystick *joy) {assert(joy==(SDL_Joystick *)&joy_object);close_calls++;}
static const char *test_name(SDL_Joystick *joy) {return "mock raw joystick";}
static int test_axes(SDL_Joystick *joy) {return 2;}
static int test_hats(SDL_Joystick *joy) {return 1;}
static int test_buttons(SDL_Joystick *joy) {return 8;}
static int test_event_state(int state) {return SDL_ENABLE;}
static SDL_bool test_is_controller(int index) {return SDL_FALSE;}
static SDL_GameController *test_ctrl_open(int index) {return NULL;}
static void test_ctrl_close(SDL_GameController *ctrl) {abort();}
static const char *test_ctrl_name(SDL_GameController *ctrl) {return "mock controller";}
static void test_version(SDL_version *v) {SDL_VERSION(v);}
static const char *test_hint(const char *hint) {return NULL;}
static const char *test_revision(void) {return "mock revision";}
static SDL_JoystickID test_instance(SDL_Joystick *joy) {return 42;}
static SDL_JoystickGUID test_guid(SDL_Joystick *joy) {SDL_JoystickGUID g={{0}};return g;}
static void test_guid_string(SDL_JoystickGUID guid,char *text,int size) {snprintf(text,(size_t)size,"mock-guid");}

#define SDL_InitSubSystem test_init
#define SDL_WasInit test_was_init
#define SDL_QuitSubSystem test_quit
#define SDL_GetError test_error
#define SDL_NumJoysticks test_num
#define SDL_PumpEvents test_pump
#define SDL_JoystickOpen test_open
#define SDL_JoystickClose test_close
#define SDL_JoystickName test_name
#define SDL_JoystickNumAxes test_axes
#define SDL_JoystickNumHats test_hats
#define SDL_JoystickNumButtons test_buttons
#define SDL_JoystickEventState test_event_state
#define SDL_GameControllerEventState test_event_state
#define SDL_IsGameController test_is_controller
#define SDL_GameControllerOpen test_ctrl_open
#define SDL_GameControllerClose test_ctrl_close
#define SDL_GameControllerName test_ctrl_name
#define SDL_GetVersion test_version
#define SDL_GetHint test_hint
#define SDL_GetRevision test_revision
#define SDL_JoystickInstanceID test_instance
#define SDL_JoystickGetGUID test_guid
#define SDL_JoystickGetGUIDString test_guid_string
#include "../src/pads.c"

static void reset(void)
{
    Input_DeinitPads();
    joy_refs=ctrl_refs=fail_joy=fail_ctrl=devices=pending_devices=0;
    enum_error=open_error=enum_calls=init_calls=close_calls=pumps=0;
    error_text=NULL;messages[0]=0;
}
int main(void)
{
    int slot, context;
#ifdef TEST_EXPECT_BITS
    assert(sizeof(void*)*8==TEST_EXPECT_BITS);
#endif
    printf("abi: %u-bit build\n",(unsigned)sizeof(void*)*8);
    reset();devices=1;fail_ctrl=1;
    assert(Pads_Init()>0 && g_padsActive && g_pads[0].joystick!=NULL);
    assert(joy_refs==1 && ctrl_refs==0 && strstr(messages,"mock controller failure"));
    Input_DeinitPads();assert(joy_refs==0 && ctrl_refs==0 && close_calls==1);
    reset();devices=1;fail_joy=1;
    assert(!Pads_Init() && g_padCount==0 && !g_padsActive && enum_calls==0);
    assert(strstr(messages,"mock joystick failure"));
    reset();assert(!Pads_Init());assert(!Pads_Init());
    assert(joy_refs==2 && ctrl_refs==1); /* one joystick ref plus the controller dependency */
    Input_DeinitPads();assert(joy_refs==0 && ctrl_refs==0);
    reset();joy_refs=2;ctrl_refs=1;devices=1;
    assert(Pads_Init()>0 && init_calls==0);
    Input_DeinitPads();assert(joy_refs==2 && ctrl_refs==1); /* owned by another subsystem */
    reset();enum_error=1;
    assert(!Pads_Init() && g_padCount==0 && !g_padsActive);
    assert(strstr(messages,"mock enumeration failure"));
    reset();devices=1;open_error=1;
    assert(!Pads_Init() && !g_padsActive && strstr(messages,"mock open failure"));
    reset();pending_devices=1;
    assert(Pads_Init()>0 && pumps>0 && g_padsActive);
    reset();
    /* All four slot offsets, independent of host pointer width. No OS driver
     * is involved: only a non-NULL semantic handle enables system controls. */
    for (slot = 0; slot < PAD_MAX; slot++) {
        unsigned int select = (unsigned int)(slot * PAD_BUTTONS + PAD_SELECT_CODE);
        unsigned int back = (unsigned int)(slot * PAD_BUTTONS + PAD_BACK_CODE);
        assert(Pads_MenuAction(select, PAD_CONTEXT_MENU) == PAD_MENU_NONE);
        assert(Pads_MenuAction(back, PAD_CONTEXT_MATCH) == PAD_MENU_NONE);
        g_padController[slot] = (SDL_GameController *)&joy_object;
        assert(Pads_MenuAction(select, PAD_CONTEXT_MENU) == PAD_MENU_BACK);
        assert(Pads_MenuAction(select, PAD_CONTEXT_MATCH) == PAD_MENU_PAUSE);
        assert(Pads_MenuAction(select, PAD_CONTEXT_TITLE) == PAD_MENU_CONSUMED);
        for (context = PAD_CONTEXT_MENU; context <= PAD_CONTEXT_MATCH; context++) {
            assert(Pads_MenuAction(back, (PadMenuContext)context) == PAD_MENU_BACK);
            assert(Pads_MenuAction(back | PAD_RELEASE, (PadMenuContext)context) == PAD_MENU_CONSUMED);
            assert(Pads_MenuAction(select | PAD_RELEASE, (PadMenuContext)context) == PAD_MENU_CONSUMED);
        }
        assert(Pads_MenuAction((unsigned int)(slot * PAD_BUTTONS + 5), PAD_CONTEXT_MENU) == PAD_MENU_NONE);
        g_padMenuHeld[slot] = 3;
        Pads_ClearDirectionSources(slot);
        assert(!g_padMenuHeld[slot]);
        g_padMenuHeld[slot] = 3;
        g_padController[slot] = NULL;
    }
    Pads_ResetDirectionSources();
    for (slot = 0; slot < PAD_MAX; slot++) assert(!g_padMenuHeld[slot]);
    assert(Pads_MenuAction(~0u, PAD_CONTEXT_MENU) == PAD_MENU_NONE);
    puts("ok: failed controller init preserves raw input; errors, discovery refresh and service ownership pass");
    puts("ok: menu actions decode all slot offsets and consume releases; raw buttons remain ordinary input");
    return 0;
}
