/*
 * Link stubs for tools/test_sdl2input.c.
 *
 * The console test drives the real Console_HandleKey(), which lives in
 * src/console.c alongside the debug command table, the console view and the
 * font/heap helpers those need. Stubbing them here keeps the test on the code
 * that matters -- key handling and the line buffer -- instead of linking half
 * the engine and a running game.
 *
 * Signatures are copied from src/symbols.h so the compiler checks them against
 * the real prototypes.
 */
#include "mugen.h"

#include <stdlib.h>

/* --- globals the console and its view read ---
 *
 * Declared in src/symbols.h with these exact types; the engine keeps them in
 * globals.c, which pulls in the whole engine. */

unsigned long long g_glStatPalStream;
unsigned long long g_glStatPalTexture;
unsigned long long g_glStatSpriteStream;
unsigned long long g_glStatSpriteTexture;
unsigned long long g_glStatTexCompress;
unsigned long long g_glTexMemory;
unsigned long long g_glTexMemoryPeak;
int g_palConvertCount;
int g_renderMode;
int g_videoDepth;
int g_videoWidth;
int g_videoHeight;
int g_targetWidth;
int g_targetHeight;
struct lua_State *g_luaState;
DecompBuffer *g_spriteDecompBuffer;

GfxRectFn g_gfxFillRect;
GfxBlendRectFn g_gfxBlendRect;

/* The console's view draws each glyph through the bound rect function; these
 * tests never render, so a no-op keeps the console's own code linkable. */
static void Stub_FillRect(int x, int y, int w, int h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

GfxRectFn g_gfxFillRectFns[GFX_RENDERER_COUNT];
GfxFillRectFn g_gfxFillRectColorFns[GFX_RENDERER_COUNT];
GfxFillRectFn g_gfxFillRectColor;

__attribute__((constructor)) static void Stub_InitRects(void)
{
    int i;

    for (i = 0; i < GFX_RENDERER_COUNT; i++) {
        g_gfxFillRectFns[i] = (GfxRectFn)Stub_FillRect;
        g_gfxFillRectColorFns[i] = (GfxFillRectFn)Stub_FillRect;
    }
    g_gfxFillRect = (GfxRectFn)Stub_FillRect;
    g_gfxFillRectColor = (GfxFillRectFn)Stub_FillRect;
}

/* The binding-name table the console prints; empty is fine here. */
const KeybindName g_keybindNames[105];

unsigned int Video_MapRGB(int r, int g, int b)
{
    (void)r;
    (void)g;
    (void)b;
    return 0;
}

/* --- allocation the engine routes through its own heap --- */

void *Mem_Calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void *Mem_Malloc(size_t size)
{
    return malloc(size);
}

void Mem_Free(void *ptr)
{
    free(ptr);
}

/* --- the debug commands Console_HandleKey can queue --- */

CmdTable *CmdTable_Create(void)
{
    return (CmdTable *)calloc(1, 64);
}

int CmdTable_Add(const char *name, CmdHandlerFn fn, void *arg, CmdTable *t)
{
    (void)name;
    (void)fn;
    (void)arg;
    (void)t;
    return 1;
}

int CmdTable_Exec(const char *line, CmdTable *t)
{
    (void)line;
    (void)t;
    return 1;
}

void Debug_ClearClipboards(void)
{
}

void Debug_ResetLifePowerTime(void)
{
}

void Debug_SetPlayerState(int n, int state)
{
    (void)n;
    (void)state;
}

void Debug_ToggleAi(int n)
{
    (void)n;
}

void Debug_TogglePlayer(int n)
{
    (void)n;
}

/* --- the console view and the drawing it does --- */

void Gfx_SetClipRect(int x1, int y1, int x2, int y2)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
}

Font *Font_Load(const char *path, int height)
{
    (void)path;
    (void)height;
    return NULL;
}

void Font_DrawTextEx(const char *text, Font *font, const DrawParams *dp, int align, int bank, int r, int g, int b)
{
    (void)text;
    (void)font;
    (void)dp;
    (void)align;
    (void)bank;
    (void)r;
    (void)g;
    (void)b;
}

int Font_TextWidth(const char *s, Font *font)
{
    (void)s;
    (void)font;
    return 0;
}

int SpriteSet_Free(SpriteSet *set)
{
    (void)set;
    return 1;
}

/* --- hashing and arrays the command table uses --- */

void HashTable_Free(HashTable *ht)
{
    (void)ht;
}

DynArray *DynArray_Create(int capacity, int elem_size)
{
    (void)capacity;
    (void)elem_size;
    return (DynArray *)calloc(1, 64);
}

/* --- players, touched only by the debug commands --- */

Player *Player_GetActive(int index)
{
    (void)index;
    return NULL;
}

void Player_SetLife(Player *p, int life)
{
    (void)p;
    (void)life;
}

void Player_SetPower(Player *p, int power)
{
    (void)p;
    (void)power;
}

int Round_GetStartTime(void)
{
    return 0;
}
