/* Regression tests for CNS terminators, renderer resource lifetime and hotplug.
 * Production parser, font-cache, video preinit/deinit and input functions run;
 * file/font loaders and the actual video-mode creation are boundary stubs. */
#include "mugen.h"
#include <assert.h>
#include "test_abi.h"
#ifdef TEST_EXPECT_BITS
_Static_assert(sizeof(void *) * 8 == TEST_EXPECT_BITS, "wrong regression harness ABI");
#endif

static GameVars game;
static Console console;
static SpriteSet screen;
static Font fonts[64];
static int font_count, custom_fonts, epoch, preinit_calls;
static int live[64];

void Log_Printf(const char *fmt, ...) { (void)fmt; }
void Error_Printf(const char *fmt, ...) { (void)fmt; }
void Error_Exit(void) { abort(); }
void Error_Fatal(const char *fmt, ...) { (void)fmt; abort(); }
void *Mem_Calloc(size_t n, size_t bytes) { return calloc(n, bytes); }
int Char_IsSpace(int c) { return c > 0 && c < 128 && isspace(c); }
int Char_IsDigit(int c) { return c >= '0' && c <= '9'; }
int Trigger_Parse(const char *s, CharData *owner, ExprValue *v, char *t, int max, const char **end)
{ (void)s; (void)owner; (void)v; (void)t; (void)max; (void)end; abort(); }
void Trigger_Free(Trigger *t) { (void)t; abort(); }
int Trigger_ArgCount(int id) { (void)id; abort(); }
int Pads_SlotUsesController(int slot) { (void)slot; return 0; }

Font *Font_Load(const char *path, int height)
{
    Font *f;
    (void)height;
    if (!custom_fonts && (strstr(path, "options") || strstr(path, "mssansserif"))) return NULL;
    assert(font_count < 64);
    f = &fonts[font_count];
    memset(f, 0, sizeof(*f));
    live[font_count++] = 1;
    f->size_h = epoch;
    return f;
}
int Font_Free(Font *font)
{
    if (font != NULL) {
        int i = (int)(font - fonts);
        assert(i >= 0 && i < font_count && live[i]);
        live[i] = 0;
    }
    return 1;
}
static void check_font(Font *font)
{
    assert(font != NULL && live[font - fonts] && font->size_h == epoch);
}
void System_FreeMotif(void) {}
int System_LoadMotif(void) { return 1; }
void Android_VideoQuit(void) {}
int PalFx_InitGlobal(void) { preinit_calls++; return 1; }
static void clear(unsigned int color) { (void)color; }
void Gfx_SoftClear(unsigned int color) { clear(color); }
void Gfx_SelectRenderer(int mode) { g_renderMode = mode; g_gfxClear = clear; }
void Gfx_LogRenderer(int requested, int effective, int directx)
{ (void)requested; (void)effective; (void)directx; }
void Gfx_SetCoordSpace(int w, int h, int fit) { (void)w; (void)h; (void)fit; }
void Gfx_SetClipRect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
int Gfx_SetRenderTarget(SpriteSet *target, int index) { (void)target; (void)index; return 1; }
SpriteSet *Screen_Create(int depth, int w, int h) { (void)depth; (void)w; (void)h; return &screen; }
int SpriteSet_Free(SpriteSet *set) { assert(set == NULL || set == &screen); return 1; }
/* The link wraps only mode creation; real Video_PreInit/Deinit run. */
int __wrap_Video_SetMode(int gw, int gh, int w, int h, int depth, unsigned int flags)
{
    (void)gw; (void)gh; (void)depth; (void)flags;
    assert(g_sinTable != NULL && g_videoScratch != NULL);
    assert(g_sinTable[0] == 0 && g_sinTable[720] == 1024 && g_sinTable[2160] == -1024);
    g_videoWidth = w; g_videoHeight = h;
    return 1;
}

static void test_parser(void)
{
    static const struct { const char *input, *format; int count; } cases[] = {
        { "1,2)", "ii", 1 }, { "1,2]", "ii", 1 },
        { "1,2)", "ii:)", 2 }, { "1,2]", "ii:)", 1 },
        { "1,2]", "ii:]", 2 }, { "1,2", "ii", 2 },
        { "1)", "i", 0 }, { "1)", "i:)", 1 }
    };
    CharData owner;
    size_t i;
    memset(&owner, 0, sizeof(owner));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Expr a = {0}, b = {0};
        const char *end = NULL;
        assert(Cns_ParseValues(cases[i].input, cases[i].format, &owner, &end, &a, &b) == cases[i].count);
        Expr_Free(&a); Expr_Free(&b);
    }
    puts("ok: CNS terminators with and without a list");
}

static void test_hotplug(void)
{
    g_keyboardActive = 1;
    game.use_keyboard[0] = 1;
    game.keys[0][KEY_A] = 'a';
    assert(Input_Remap());
    g_keyState['a'] = 1;
    Input_OnKey('a', 1);
    assert(Input_CodePending());
    {
        int read = g_keyQueue.read, write = g_keyQueue.write;
        int code = g_keyQueue.data[read];
        InputHandlerFn key_handler = g_keyHandler, pad_handler = g_padHandler;
        Config_RepublishJoystick(1);
        assert(g_keyQueue.read == read && g_keyQueue.write == write);
        assert(g_keyQueue.data[read] == code && Input_CodePending());
        assert(g_keyHandler == key_handler && g_padHandler == pad_handler);
        assert(g_inputState[g_keyMap['a']] != 0);
    }
    /* An explicit user remap still resets the event engine, as before. */
    assert(Input_Remap() && !Input_CodePending());
    Events_Deinit();
    g_keyState['a'] = 0;
    puts("ok: hotplug preserves pending events, handlers and held keys");
}

static void test_fallback(int own_fonts)
{
    int cycle, i;
    DynArray history;
    memset(&game, 0, sizeof(game));
    memset(&console, 0, sizeof(console));
    memset(&history, 0, sizeof(history));
    custom_fonts = own_fonts;
    g_console = &console;
    console.view.lines = &history;
    console.input.max = 123; /* unrelated editing state must survive */
    game.game_coord[0] = 640; game.game_coord[1] = 480;
    game.video_width = 640; game.video_height = 480;
    game.motif_coord[0] = 640; game.motif_coord[1] = 480;
    epoch++;
    assert(System_LoadFonts());
    Options_GetFont();
    ConsoleView_LoadFont(&console.view);
    assert(Video_PreInit());
    for (cycle = 0; cycle < 2; cycle++) {
        int calls = preinit_calls;
        epoch++;
        System_ReinitVideo();
        assert(preinit_calls == calls + 1);
        assert(game.options_font == NULL);
        check_font(Options_GetFont());
        check_font(console.view.font);
        assert(console.view.font_owned == own_fonts);
        assert(console.view.lines == &history && console.input.max == 123);
        assert(console.view.w == 620.0f && game.render_mode == RENDER_SYSTEM);
        /* Sample exactly the rotation table locations the software drawers use. */
        assert(g_sinTable[720] == 1024 && g_sinTable[2160] == -1024);
    }
    ConsoleView_ReleaseFont(&console.view);
    if (game.options_font != game.font_medium) Font_Free(game.options_font);
    Font_Free(game.font_small); Font_Free(game.font_medium);
    game.options_font = game.font_small = game.font_medium = NULL;
    Video_Deinit();
    assert(g_sinTable == NULL && g_videoScratch == NULL);
    for (i = 0; i < font_count; i++) assert(live[i] == 0);
    g_console = NULL;
    puts(own_fonts ? "ok: fallback reloads private fonts and rotation storage twice" :
                     "ok: fallback reloads borrowed fonts and rotation storage twice");
}

int main(void)
{
    TEST_REPORT_ABI();
    g_game = &game;
    test_parser();
    test_hotplug();
    test_fallback(0);
    test_fallback(1);
    puts("ok: review regressions passed");
    return 0;
}
