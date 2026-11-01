/*
 * sdl-input-mapper -- a PS Vita tool for inspecting SDL 1.2 joystick input.
 *
 * Why this exists
 * ---------------
 * mugen-unleashed's Vita port needs to know what SDL's Vita joystick driver
 * actually reports: which button index is Cross, whether the d-pad arrives as
 * buttons or as a hat, what the analog stick's axis indices and ranges are.
 * Guessing those from a desktop SDL port produced a menu that could not be
 * navigated. This tool answers the question by observation instead: press
 * things, read the indices off the screen.
 *
 * It also reports hats and axes, because "gamepad input" is not only buttons --
 * the d-pad may be a hat, and the analog stick is two axes with a range and a
 * centre that are driver-defined.
 *
 * What it shows
 * -------------
 *   - the joystick's name, and how many axes / buttons / hats it reports;
 *   - a grid of button cells, one per button index, lit while held;
 *   - a bar per axis with its raw signed value;
 *   - the raw hat value (SDL_HAT_* bits);
 *   - the last few events in order, so a press can be pinned to an index;
 *   - the same events appended to ux0:data/sdl-input-mapper.log as text, so the
 *     whole session can be sent on rather than read off a photograph.
 *
 * Deliberate choices (learned the hard way on the mugen port)
 * ----------------------------------------------------------
 *   - SDL_SWSURFACE. src/video.c on the mugen port writes straight into
 *     surface->pixels without locking, which a hardware surface does not allow;
 *     this tool draws with SDL_FillRect, but it asks for a software surface for
 *     the same reason: it is the mode that behaves predictably on this driver.
 *   - SDL_Flip every frame. The Vita driver returns a double-buffered surface
 *     whether or not SDL_DOUBLEBUF is requested, and SDL 1.2 only presents a
 *     double-buffered surface on SDL_Flip. Without it the screen stays black.
 *   - Video is initialised before audio. SDL 1.2's SDL_Init walks its
 *     subsystems in a fixed order and returns at the first failure; audio comes
 *     before video, so a failed audio init silently leaves video uninitialised.
 *     This tool does not need audio, but it does not ask for it either.
 *   - _newlib_heap_size_user is defined (see vita_newlib_inputmapper.c): the
 *     VitaSDK default of 128 MiB is larger than the process budget allows.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <SDL.h>

#include "input_mapper_font.h"

#define SCREEN_W 960
#define SCREEN_H 544

/* How many recent events to keep on screen. */
#define EVENT_ROWS 6

/* Maximums we are willing to display; the real counts come from SDL. */
#define MAX_BUTTONS 32
#define MAX_AXES    8
#define MAX_HATS    4

typedef struct {
    int type;      /* SDL event type, or 0 when the slot is unused */
    int index;     /* button / axis / hat index */
    int value;     /* new state: 1/0, axis value, or hat bits */
    unsigned int serial;
} EventRow;

static SDL_Surface *g_screen;
static FILE *g_log;

/* Live input state. */
static unsigned char g_button[MAX_BUTTONS];
static int g_axis[MAX_AXES];
static int g_hat[MAX_HATS];

static EventRow g_events[EVENT_ROWS];
static unsigned int g_serial;

/* ---------------------------------------------------------------- drawing -- */

static Uint32 rgb(int r, int g, int b)
{
    return SDL_MapRGB(g_screen->format, (Uint8)r, (Uint8)g, (Uint8)b);
}

static void fill(int x, int y, int w, int h, Uint32 color)
{
    SDL_Rect r;

    if (w <= 0 || h <= 0) {
        return;
    }
    r.x = (Sint16)x;
    r.y = (Sint16)y;
    r.w = (Uint16)w;
    r.h = (Uint16)h;
    SDL_FillRect(g_screen, &r, color);
}

static void frame(int x, int y, int w, int h, Uint32 color)
{
    fill(x, y, w, 1, color);
    fill(x, y + h - 1, w, 1, color);
    fill(x, y, 1, h, color);
    fill(x + w - 1, y, 1, h, color);
}

/*
 * Draws one character. Set pixels are merged into horizontal runs so the
 * number of SDL_FillRect calls stays small enough to redraw every frame.
 */
static void draw_char(int x, int y, int scale, Uint32 color, char ch)
{
    int code = (unsigned char)ch;
    const unsigned char *glyph;
    int row, col;

    if (code < FONT_FIRST || code > FONT_LAST) {
        return;
    }
    glyph = g_font[code - FONT_FIRST];
    for (row = 0; row < FONT_H; row++) {
        unsigned int bits = glyph[row];
        col = 0;
        while (col < FONT_W) {
            int start;
            if ((bits & (1u << (FONT_W - 1 - col))) == 0) {
                col++;
                continue;
            }
            start = col;
            while (col < FONT_W && (bits & (1u << (FONT_W - 1 - col))) != 0) {
                col++;
            }
            fill(x + start * scale, y + row * scale, (col - start) * scale, scale, color);
        }
    }
}

static void draw_text(int x, int y, int scale, Uint32 color, const char *text)
{
    while (*text != '\0') {
        draw_char(x, y, scale, color, *text);
        x += (FONT_W + 1) * scale;
        text++;
    }
}

/* Text width in pixels, for centring. */
static int text_width(const char *text, int scale)
{
    return (int)strlen(text) * (FONT_W + 1) * scale;
}

static void draw_text_centered(int y, int scale, Uint32 color, const char *text)
{
    draw_text((SCREEN_W - text_width(text, scale)) / 2, y, scale, color, text);
}

/* ---------------------------------------------------------------- logging -- */

static void log_event(const char *fmt, ...)
{
    va_list ap;

    if (g_log == NULL) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

static void push_event(int type, int index, int value)
{
    int i;

    for (i = EVENT_ROWS - 1; i > 0; i--) {
        g_events[i] = g_events[i - 1];
    }
    g_events[0].type = type;
    g_events[0].index = index;
    g_events[0].value = value;
    g_events[0].serial = ++g_serial;
}

static const char *event_type_name(int type)
{
    switch (type) {
    case SDL_JOYBUTTONDOWN:
        return "BUTTON DOWN";
    case SDL_JOYBUTTONUP:
        return "BUTTON UP  ";
    case SDL_JOYAXISMOTION:
        return "AXIS       ";
    case SDL_JOYHATMOTION:
        return "HAT        ";
    default:
        return "?          ";
    }
}

/* ------------------------------------------------------------------ input -- */

static void handle_event(SDL_Event *ev, int *done)
{
    switch (ev->type) {
    case SDL_QUIT:
        *done = 1;
        break;
    case SDL_KEYDOWN:
        if (ev->key.keysym.sym == SDLK_ESCAPE) {
            *done = 1;
        }
        log_event("key    code %d (%s)\n", ev->key.keysym.sym,
                  (ev->key.keysym.sym == SDLK_ESCAPE) ? "Escape = quit" : "any other");
        break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int button = ev->jbutton.button;
        int pressed = ev->type == SDL_JOYBUTTONDOWN;

        if (button >= 0 && button < MAX_BUTTONS) {
            g_button[button] = (unsigned char)pressed;
        }
        push_event(ev->type, button, pressed);
        log_event("button index %2d %s   (SDL event %d)\n", button, pressed ? "DOWN" : "up  ",
                  ev->type);
        break;
    }
    case SDL_JOYAXISMOTION: {
        int axis = ev->jaxis.axis;

        if (axis >= 0 && axis < MAX_AXES) {
            g_axis[axis] = ev->jaxis.value;
        }
        push_event(ev->type, axis, ev->jaxis.value);
        /* Log sparsely: an analog axis emits a stream of values. */
        if (ev->jaxis.value <= -30000 || ev->jaxis.value >= 30000 || ev->jaxis.value == 0) {
            log_event("axis   index %2d value %6d (of -32768..32767)\n", axis, ev->jaxis.value);
        }
        break;
    }
    case SDL_JOYHATMOTION: {
        int hat = ev->jhat.hat;

        if (hat >= 0 && hat < MAX_HATS) {
            g_hat[hat] = ev->jhat.value;
        }
        push_event(ev->type, hat, ev->jhat.value);
        log_event("hat    index %2d value %d "
                  "(UP=%d RIGHT=%d DOWN=%d LEFT=%d when set)\n",
                  hat, ev->jhat.value, SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT);
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------- draw -- */

static void draw_buttons(int num_buttons, int y)
{
    int per_row = 8;
    int cell_w = 108;
    int cell_h = 30;
    int i;

    draw_text(8, y, 1, rgb(160, 200, 255), "BUTTONS");
    y += 12;

    for (i = 0; i < num_buttons && i < MAX_BUTTONS; i++) {
        int col = i % per_row;
        int row = i / per_row;
        int x = 8 + col * cell_w;
        int cy = y + row * (cell_h + 4);
        int held = g_button[i];

        fill(x, cy, cell_w - 6, cell_h, held ? rgb(40, 160, 80) : rgb(24, 28, 40));
        frame(x, cy, cell_w - 6, cell_h, held ? rgb(160, 255, 180) : rgb(70, 80, 100));
        {
            char label[16];
            sprintf(label, "%d %s", i, held ? "ON" : "off");
            draw_text(x + 6, cy + 4, 2, held ? rgb(255, 255, 255) : rgb(150, 160, 180), label);
        }
    }

    return;
}

static int buttons_height(int num_buttons)
{
    int rows = (num_buttons + 7) / 8;

    if (rows < 1) {
        rows = 1;
    }
    return 12 + rows * 34;
}

static void draw_axes(int num_axes, int y)
{
    int i;

    draw_text(8, y, 1, rgb(160, 200, 255), "AXES  (raw value, -32768 .. 32767)");
    y += 12;

    for (i = 0; i < num_axes && i < MAX_AXES; i++) {
        int value = g_axis[i];
        int bar_x = 120;
        int bar_w = SCREEN_W - bar_x - 130;
        int mid = bar_x + bar_w / 2;
        int pos;
        char label[32];

        fill(bar_x, y + 4, bar_w, 10, rgb(24, 28, 40));
        frame(bar_x, y + 4, bar_w, 10, rgb(70, 80, 100));
        /* Centre mark, so the resting position is visible. */
        fill(mid, y + 2, 1, 14, rgb(90, 100, 130));

        pos = mid + (int)((long)value * (bar_w / 2) / 32768);
        if (pos < bar_x) {
            pos = bar_x;
        }
        if (pos > bar_x + bar_w - 1) {
            pos = bar_x + bar_w - 1;
        }
        fill(pos - 2, y + 3, 5, 12, rgb(255, 200, 80));

        sprintf(label, "AXIS %d", i);
        draw_text(8, y + 1, 1, rgb(220, 220, 220), label);
        sprintf(label, "%6d", value);
        draw_text(bar_x + bar_w + 10, y + 1, 1, rgb(255, 220, 140), label);
        y += 18;
    }
}

static int axes_height(int num_axes)
{
    return 12 + (num_axes < 1 ? 1 : num_axes) * 18;
}

static void draw_hats(int num_hats, int y)
{
    int i;
    char label[64];

    if (num_hats <= 0) {
        draw_text(8, y, 1, rgb(120, 130, 150), "HATS  none reported");
        return;
    }
    draw_text(8, y, 1, rgb(160, 200, 255), "HATS  (raw SDL_HAT bits)");
    for (i = 0; i < num_hats && i < MAX_HATS; i++) {
        sprintf(label, "HAT %d = %d ", i, g_hat[i]);
        draw_text(8 + 150 + i * 120, y, 1, rgb(255, 220, 140), label);
    }
}

static void draw_events(int y)
{
    int i;

    draw_text(8, y, 1, rgb(160, 200, 255), "LAST EVENTS (most recent first)");
    y += 12;
    for (i = 0; i < EVENT_ROWS; i++) {
        char label[64];

        if (g_events[i].type == 0) {
            continue;
        }
        sprintf(label, "%-12s index %2d  value %6d", event_type_name(g_events[i].type),
                g_events[i].index, g_events[i].value);
        draw_text(8, y, 1, rgb(200, 210, 230), label);
        y += 10;
    }
}

static void draw(int num_axes, int num_buttons, int num_hats, const char *name)
{
    int y;

    fill(0, 0, SCREEN_W, SCREEN_H, rgb(10, 12, 20));

    draw_text_centered(6, 2, rgb(255, 255, 255), "SDL INPUT MAPPER");
    draw_text_centered(24, 1, rgb(140, 160, 190),
                       "PS Vita  --  SDL 1.2 joystick, hats and axes");

    y = 40;
    {
        char line[128];
        sprintf(line, "JOYSTICK: %s", name);
        draw_text(8, y, 1, rgb(255, 220, 140), line);
        y += 10;
        sprintf(line, "axes %d    buttons %d    hats %d", num_axes, num_buttons, num_hats);
        draw_text(8, y, 1, rgb(200, 210, 230), line);
        y += 14;
    }

    draw_buttons(num_buttons, y);
    y += buttons_height(num_buttons) + 4;

    draw_axes(num_axes, y);
    y += axes_height(num_axes) + 4;

    draw_hats(num_hats, y);
    y += 14;

    draw_events(y);

    /* Footer: Escape quits, and the log path, so a photograph of the screen
     * also says where the text version lives. */
    draw_text(8, SCREEN_H - 12, 1, rgb(120, 130, 150),
              "ESC or SDL_QUIT exits.  Log: ux0:data/sdl-input-mapper.log");
}

/* ------------------------------------------------------------------- main -- */

int main(int argc, char *argv[])
{
    SDL_Joystick *joy = NULL;
    const char *name = "(no joystick)";
    int num_axes = 0;
    int num_buttons = 0;
    int num_hats = 0;
    int done = 0;
    int have_joy = 0;

    (void)argc;
    (void)argv;

    /*
     * The log goes to ux0:data rather than the app directory: the launch
     * directory is not always writable, and a tool whose output silently does
     * not exist is worse than no tool.
     */
    g_log = fopen("ux0:data/sdl-input-mapper.log", "wt");
    if (g_log == NULL) {
        g_log = fopen("sdl-input-mapper.log", "wt");
    }
    log_event("sdl-input-mapper\n");

    /*
     * Video before audio, and audio not at all. SDL 1.2's SDL_Init returns at
     * the first subsystem failure, so a broken audio init would leave video
     * uninitialised; that is exactly how the mugen port came to report
     * "Can't change video mode" for an audio problem.
     */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK) < 0) {
        log_event("SDL_Init failed: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        if (g_log != NULL) {
            fclose(g_log);
        }
        return 1;
    }
    {
        const SDL_version *linked = SDL_Linked_Version();
        log_event("SDL headers %d.%d.%d, linked %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                  SDL_PATCHLEVEL, linked->major, linked->minor, linked->patch);
    }

    g_screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
    if (g_screen == NULL) {
        log_event("SDL_SetVideoMode(32) failed: %s; retrying 16\n", SDL_GetError());
        g_screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_SWSURFACE | SDL_FULLSCREEN);
    }
    if (g_screen == NULL) {
        log_event("SDL_SetVideoMode failed: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        if (g_log != NULL) {
            fclose(g_log);
        }
        return 1;
    }
    SDL_WM_SetCaption("SDL Input Mapper", "SDL Input Mapper");
    log_event("surface %dx%d %d bpp, flags 0x%08X%s\n", g_screen->w, g_screen->h,
              g_screen->format->BitsPerPixel, (unsigned)g_screen->flags,
              (g_screen->flags & SDL_DOUBLEBUF) ? " (double buffered -> SDL_Flip required)" : "");

    /* Joystick. Without ENABLE, SDL reports no joystick events at all. */
    SDL_JoystickEventState(SDL_ENABLE);
    log_event("SDL_NumJoysticks() = %d\n", SDL_NumJoysticks());
    if (SDL_NumJoysticks() > 0) {
        joy = SDL_JoystickOpen(0);
        if (joy != NULL) {
            have_joy = 1;
            num_axes = SDL_JoystickNumAxes(joy);
            num_buttons = SDL_JoystickNumButtons(joy);
            num_hats = SDL_JoystickNumHats(joy);
            name = SDL_JoystickName(0);
            if (name == NULL) {
                name = "(unnamed)";
            }
            log_event("joystick 0: \"%s\" axes=%d buttons=%d hats=%d\n", name, num_axes,
                      num_buttons, num_hats);
        } else {
            log_event("SDL_JoystickOpen(0) failed: %s\n", SDL_GetError());
        }
    }
    if (num_axes > MAX_AXES) {
        num_axes = MAX_AXES;
    }
    if (num_buttons > MAX_BUTTONS) {
        num_buttons = MAX_BUTTONS;
    }
    if (num_hats > MAX_HATS) {
        num_hats = MAX_HATS;
    }

    while (!done) {
        SDL_Event ev;

        while (SDL_PollEvent(&ev)) {
            handle_event(&ev, &done);
        }
        draw(num_axes, num_buttons, num_hats, name);
        /*
         * The Vita driver hands back a double-buffered surface whether or not
         * SDL_DOUBLEBUF was asked for, and SDL 1.2 presents such a surface only
         * on SDL_Flip. Skipping this leaves a black screen with everything
         * else working.
         */
        SDL_Flip(g_screen);
        SDL_Delay(16);
    }

    log_event("exiting; last %u events recorded\n", g_serial);
    if (have_joy && joy != NULL) {
        SDL_JoystickClose(joy);
    }
    SDL_Quit();
    if (g_log != NULL) {
        fclose(g_log);
    }
    return 0;
}
