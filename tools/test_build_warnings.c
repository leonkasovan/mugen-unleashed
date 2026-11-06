/* Regressions behind the w64devkit -Wall diagnostics. Real engine code runs;
 * file existence and the external resampler close call are test boundaries. */
#include "mugen.h"
#include "test_abi.h"
#include <assert.h>
/* A standalone console harness must not be redirected to SDL_main on Win32. */
#ifdef main
#undef main
#endif

#ifdef TEST_EXPECT_BITS
_Static_assert(sizeof(void *) * 8 == TEST_EXPECT_BITS, "wrong warnings harness ABI");
#endif

static int file_attempts, errors, rs_closes;
static const char *existing_file;
static char opened_path[512];
static GameVars game;
static Player player;
/* Exercise the exact quote selection helper used by Match_Unload; the other
 * match-management functions are discarded by the linker. */
#include "../src/matchload.c"

/* Storyboard parsing runs against real INI/keyed-array code; asset loaders
 * are boundaries because these fixtures have no graphics, fonts or audio. */
SpriteSet *Sff_Load(const char *path)
{
    assert(strcmp(path, "fixture.sff") == 0);
    return calloc(1, sizeof(SpriteSet));
}
int SpriteSet_Free(SpriteSet *sprites) { free(sprites); return 1; }
void Fonts_Load(Ini *ini, Font **fonts, const char *const *dirs)
{
    (void)ini; (void)fonts; (void)dirs;
}
Snd *Snd_Load(const char *path) { (void)path; abort(); }
Anim *Anim_Parse(Ini *ini) { (void)ini; abort(); }
Bg *Bg_Load(const char *name, Ini *ini, const char *dir, SpriteSet *sprites, Anim *anim)
{
    (void)name; (void)ini; (void)dir; (void)sprites; (void)anim; abort();
}
int Bgm_ReadInfo(Ini *ini, const char *key, BgmInfo *info)
{
    (void)ini; (void)key; (void)info; abort();
}
void Bgm_Stop(void) {}
void Bg_Free(Bg *bg) { (void)bg; abort(); }
void Anim_Free(Anim *anim) { (void)anim; abort(); }
void Snd_Free(Snd *snd) { (void)snd; abort(); }
int Font_Free(Font *font) { (void)font; abort(); }

FILE *Test_Fopen(const char *path, const char *mode)
{
    assert(strcmp(mode, "rb") == 0);
    assert(strlen(path) < sizeof(opened_path));
    strcpy(opened_path, path);
    file_attempts++;
    return existing_file != NULL && strcmp(path, existing_file) == 0 ? stdout : NULL;
}

int Test_Fclose(FILE *fp)
{
    assert(fp == stdout);
    return 0;
}

void Error_Printf(const char *fmt, ...)
{
    (void)fmt;
    errors++;
}

void resample_close(void *handle)
{
    assert(handle == &rs_closes);
    rs_closes++;
}

static void test_input(void)
{
    InputBuf *in = &player.input;
    InputBuf saved;
    int i;

    memset(in, 0, sizeof(*in));
    for (i = 1; i < KEY_COUNT; i++) in->keys[i] = 1;
    in->read = 5;
    Ai_ReleaseAll(&player);
    for (i = 1; i < KEY_COUNT; i++) {
        assert(in->keys[i] == 0);
        assert(in->ring[i - 1] == (unsigned int)(i | 0x10));
    }
    assert(in->ring[12] == (13 | 0x10));
    assert(in->read == 0 && in->write == 13);

    memset(in, 0, sizeof(*in));
    in->keys[KEY_LEFT] = in->keys[KEY_RIGHT] = 1;
    in->read = 4;
    Ai_ReleaseKeysExcept(&player, KEY_LEFT);
    assert(in->keys[KEY_LEFT] == 1 && in->keys[KEY_RIGHT] == 0);
    assert(in->read == 0 && in->write == 2);
    assert(in->ring[1] == (13 | 0x10));

    in->locked = 1;
    saved = *in;
    Ai_ReleaseAll(&player);
    assert(memcmp(in, &saved, sizeof(saved)) == 0);
    in->locked = 0;
    saved = *in;
    PlayerInput_Push(in, 14);
    PlayerInput_Push(in, 15);
    assert(memcmp(in, &saved, sizeof(saved)) == 0);
    puts("ok: AI releases retain key-13/read behavior without out-of-bounds access");
}

static void test_resampler(void)
{
    ResamplerSession *session;
    Resampler_Close(NULL);
    session = calloc(1, sizeof(*session));
    assert(session != NULL);
    Resampler_Close(session); /* ASan/libc detects the old double-free. */
    session = calloc(1, sizeof(*session));
    assert(session != NULL);
    session->state = calloc(1, sizeof(*session->state));
    assert(session->state != NULL);
    session->state->rs = &rs_closes;
    session->state->in_float = malloc(16);
    session->state->out_float = malloc(16);
    session->state->out_buf = malloc(16);
    Resampler_Close(session);
    assert(rs_closes == 1);
    puts("ok: NULL, stateless and populated resampler sessions close safely");
}

static void test_counted_strings(void)
{
    char text[33];
    int i;
    assert(CountedStr_Make(NULL) == NULL);
    memset(text, 'x', sizeof(text));
    for (i = 0; i <= 32; i++) {
        CountedStr *key, *copy;
        text[i] = '\0';
        key = CountedStr_Make(text);
        assert(key != NULL && key->len == (unsigned int)i);
        copy = CountedStr_Copy(key, NULL, NULL);
        assert(copy != NULL && copy != key && copy->len == key->len);
        assert(memcmp(copy->data, text, i) == 0);
        assert(CountedStr_Differ(key, copy) == 0);
        free(key);
        free(copy);
        text[i] = 'x';
    }
    puts("ok: empty, short and trailing-storage counted-string keys copy safely");
}

static void test_paths(void)
{
    char path[1200], base[600], out[512], dir[512];
    g_game = &game;
    existing_file = "base/sub/file.def";
    assert(File_Find("sub/file.def", out, dir, "base/") == 1);
    assert(strcmp(out, existing_file) == 0 && strcmp(dir, "base/sub/") == 0);
    existing_file = "./file.def";
    assert(File_Find("file.def", out, dir, NULL) == 1);
    assert(strcmp(out, existing_file) == 0 && strcmp(dir, "./") == 0);

    memset(path, 'x', sizeof(path)); path[sizeof(path) - 1] = '\0';
    file_attempts = 0;
    assert(File_Find(path, out, dir, "") == 0 && file_attempts == 0);
    path[1000] = '/'; path[1001] = 'x'; path[1002] = '\0';
    assert(File_Find(path, out, dir, "") == 0 && file_attempts == 0);

    memset(base, 'b', sizeof(base)); base[sizeof(base) - 1] = '\0';
    existing_file = "./file.def";
    assert(File_Find("file.def", out, dir, base) == 1);
    assert(strcmp(out, existing_file) == 0); /* oversized candidate was skipped */
    assert(errors == 2);
    puts("ok: file lookup rejects overlong components and skips truncated candidates");
}

static void test_ini_groups(void)
{
    char text[] = "[Info]\0key=value\0[en.Info]\0key=localized\0[TPEnd]";
    char *lines[] = {text, text + 7, text + 17, text + 27, text + 41};
    int groups[] = {0, 2};
    char name[600];
    Ini ini;
    memset(&ini, 0, sizeof(ini));
    ini.text = text; ini.lines = lines; ini.line_count = 5;
    ini.group_lines = groups; ini.group_count = 2;
    assert(Ini_SelectGroup(&ini, "Info") == 7 && ini.group == 0);
    strcpy(ini.lang, "en");
    assert(Ini_SelectGroup(&ini, "Info") == 27 && ini.group == 1);
    assert(Ini_SelectGroup(&ini, "Missing") == -1);
    memset(name, 'x', sizeof(name)); name[sizeof(name) - 1] = '\0';
    assert(Ini_SelectGroup(&ini, name) == -1);
    assert(Ini_FindGroup(&ini, name) == -1);
    puts("ok: normal/localized INI groups and overlong names are handled safely");
}

/* Build an already-parsed INI fixture with the same sentinel as Ini_Parse. */
static void scene_ini(Ini *ini, char *text, char **lines, int *groups)
{
    char *p = text;
    memset(ini, 0, sizeof(*ini));
    ini->text = text; ini->lines = lines; ini->group_lines = groups;
    while (*p != '\0') {
        char *end = strchr(p, '\n');
        assert(end != NULL);
        *end = '\0';
        if (*p == '[' && strcmp(p, "[TPEnd]") != 0) {
            groups[ini->group_count++] = ini->line_count;
        }
        lines[ini->line_count++] = p;
        p = end + 1;
    }
}

static void test_storyboard(void)
{
    char text[] = "[scenedef]\nspr=fixture.sff\n[scene 0]\nend.time=1\n"
                  "clearcolor=11,22,33\nwindow=1,2,30,40\nlayerall.pos=5,6\n"
                  "[scene 1]\nend.time=2\n[TPEnd]\n";
    char long_text[1024], name[400];
    char *lines[32];
    int groups[8];
    Ini ini;
    Storyboard *sb;
    Scene *first, *second;
    scene_ini(&ini, text, lines, groups);
    sb = Storyboard_Parse("", &ini);
    assert(sb != NULL && sb->scene_count == 2);
    first = KeyedArray_Get(sb->scenes, 0, 0);
    second = KeyedArray_Get(sb->scenes, 0, 1);
    assert(first != NULL && second != NULL);
    assert(first->clear_color[0] == 11 && first->clear_color[2] == 33);
    assert(memcmp(first->clear_color, second->clear_color, sizeof(first->clear_color)) == 0);
    assert(memcmp(first->window, second->window, sizeof(first->window)) == 0);
    assert(second->window_on == 1 && second->window[3] == 40);
    assert(second->layerall_x == 5 && second->layerall_y == 6);
    Storyboard_Free(sb);

    memset(name, 'x', sizeof(name)); name[sizeof(name) - 1] = '\0';
    snprintf(long_text, sizeof(long_text), "[scenedef]\nspr=fixture.sff\n[scene %s]\nend.time=1\n[TPEnd]\n", name);
    scene_ini(&ini, long_text, lines, groups);
    assert(Storyboard_Parse("", &ini) == NULL);
    assert(strcmp(g_libraryErrorText, "Invalid or overlong scene group name") == 0);
    puts("ok: storyboard fields inherit correctly and overlong scene names are rejected");
}

static void test_victory_quotes(void)
{
    static CharData character;
    char *zero = malloc(32), *last = malloc(32);
    assert(zero != NULL && last != NULL);
    strcpy(zero, "quote zero"); strcpy(last, "quote 99");
    character.quotes[0] = zero;
    character.quotes[99] = last;
    character.quote_count = 2;
    game.match_rng = 1; game.match_rng_mul = 1; game.match_rng_mod = 97;
    assert(Ml_VictoryQuote(&character, 99) == last);
    assert(game.match_rng == 1); /* a preferred quote does not advance RNG */
    assert(Ml_VictoryQuote(&character, -1) == zero); /* second entry wraps */
    game.match_rng = 0;
    assert(Ml_VictoryQuote(&character, 500) == last);
    character.quote_count = 0;
    character.quotes[0] = character.quotes[99] = NULL;
    assert(Ml_VictoryQuote(&character, -1) == NULL);
    free(zero); free(last);
    puts("ok: victory quotes retain full pointer width and wrap at entry 99");
}

static void test_counter(void)
{
    LARGE_INTEGER frequency, first, second;
    assert(QueryPerformanceFrequency(&frequency));
    assert(frequency.QuadPart > 0);
    assert(QueryPerformanceCounter(&first));
    assert(QueryPerformanceCounter(&second));
    assert(second.QuadPart >= first.QuadPart);
#ifndef _WIN32
    assert(frequency.QuadPart == 10000000);
    assert(QueryPerformanceCounter(NULL) == FALSE);
    assert(QueryPerformanceFrequency(NULL) == FALSE);
#endif
    puts("ok: performance-counter declarations match their LARGE_INTEGER callers");
}

int main(void)
{
    TEST_REPORT_ABI();
    test_input();
    test_resampler();
    test_counted_strings();
    test_paths();
    test_ini_groups();
    test_storyboard();
    test_victory_quotes();
    test_counter();
    puts("ok: build-warning regressions passed");
    return 0;
}
