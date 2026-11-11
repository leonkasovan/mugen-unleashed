/* Real parser/stream/merge test: localized write-back settings remain owned
 * by the game's file. No SDL runtime or rendering is used. */
#include "mugen.h"
#include "configmerge.h"
#include <assert.h>

static Ini *parse(char *text)
{
    Stream *stream = Stream_OpenMemory(text, (int)strlen(text));
    Ini *ini;
    assert(stream);
    ini = Ini_Parse(stream, INI_MODE_READ);
    Stream_Close(stream);
    assert(ini);
    return ini;
}

int main(void)
{
    static const char *const groups[] = {
        "Options", "P1 Keys", "P2 Keys", "P1 Joystick", "P2 Joystick",
        "P1 Gamepad", "P2 Gamepad"
    };
    static const char *const locales[] = { "en", "ja", "id", "pt", "EN", "zz" };
    int i, j, count = 0;
    for (i = 0; i < 7; ++i) for (j = 0; j < 6; ++j) {
        char base_text[1024], over_text[1024];
        Ini *base, *over, *merged;
        char *text = NULL;
        int len, keys, skipped;
        snprintf(base_text, sizeof base_text,
            "[%s]\nValue=10\n[%s.%s]\nValue=11\n"
            "[Video]\nWidth=640\nKeep=7\n[Sound]\nMasterVolume=60\n"
            "[Input]\np1.joystick.type=1\n",
            groups[i], locales[j], groups[i]);
        snprintf(over_text, sizeof over_text,
            "[%s]\nValue=20\n[%s.%s]\nValue=21\n"
            "[Video]\nWidth=960\n[Sound]\nMasterVolume=0\n"
            "[Input]\np1.joystick.type=0\n",
            groups[i], locales[j], groups[i]);
        Ini_SetLanguage(locales[j]);
        base = parse(base_text); over = parse(over_text);
        assert(ConfigMerge_Text(base, over, &text, &len, &keys, &skipped));
        assert(text && len == (int)strlen(text) && skipped >= 2);
        merged = parse(text);
        assert(Ini_SelectGroup(merged, groups[i]) != -1);
        assert(atoi(Ini_GetValue(merged, "Value")) == 11);
        assert(Ini_SelectGroup(merged, "Video") != -1);
        assert(atoi(Ini_GetValue(merged, "Width")) == 960);
        assert(atoi(Ini_GetValue(merged, "Keep")) == 7);
        assert(Ini_SelectGroup(merged, "Sound") != -1);
        assert(atoi(Ini_GetValue(merged, "MasterVolume")) == 60);
        assert(Ini_SelectGroup(merged, "Input") != -1);
        assert(atoi(Ini_GetValue(merged, "p1.joystick.type")) == 1);
        Ini_Free(merged);
        Ini_SetLanguage(NULL);
        merged = parse(text);
        assert(Ini_SelectGroup(merged, groups[i]) != -1);
        assert(atoi(Ini_GetValue(merged, "Value")) == 10);
        Ini_Free(merged); Ini_Free(over); Ini_Free(base); free(text);
        ++count;
    }
    printf("ok: %d localized write-back cases, per-key overrides and inheritance\n", count);
    return 0;
}
