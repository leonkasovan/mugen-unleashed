/*
 * Host gate for Ini_CountGroups (src/ini.c).
 *
 * Stage_Load counts the [bg] groups while [StageInfo] is selected, to decide
 * what the "Background:" log line should say. If the count leaves the INI
 * cursor at the end of the file, the reads that follow (autoturn, resetbg,
 * xscale, yscale) silently miss and keep their defaults. autoturn then stays
 * 0, Round_Tick's auto-turn loop never runs, and players keep facing the
 * wrong way after crossing up -- a visual bug with no failing call.
 *
 * This harness reproduces that sequence on a synthetic stage file. No SDL
 * window, no game data, no GPU. ini.c needs the real mugen.h, so the few
 * util.c/stream.c pieces it calls are stubbed below.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mugen.h"

/* ---- the util.c pieces ini.c calls ---- */
int g_iniCaseSensitive;
char g_iniLang[3];
int g_outOfMemory;

void *__cdecl Mem_Calloc(size_t count, size_t size)
{
    void *p = calloc(count, size);

    if (p == NULL) {
        g_outOfMemory = 1;
    }
    return p;
}

int __cdecl Char_IsSpace(int c)
{
    return (unsigned int)(c - 1) < 0x7F && isspace(c);
}

int __cdecl Char_ToLower(int c)
{
    if ((unsigned int)(c - 1) < 0x7F) {
        c = tolower(c);
    }
    return c;
}

int __cdecl Str_StartsWithI(const char *s, const char *prefix)
{
    for (; *s != '\0'; s++, prefix++) {
        if (*prefix == '\0') {
            return 1;
        }
        if (Char_ToLower(*s) != Char_ToLower(*prefix)) {
            return 0;
        }
    }
    return 1;
}

int __cdecl Str_PrefixMatchI(const char *a, const char *b, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (Char_ToLower(a[i]) != Char_ToLower(b[i]) || a[i] == '\0' || b[i] == '\0') {
            return 0;
        }
    }
    return 1;
}

int __cdecl Str_PrefixMatch(const char *a, const char *b, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return 0;
        }
    }
    return 1;
}

/* ---- the stream.c pieces ini.c calls, over stdio ---- */
Stream *__cdecl Stream_OpenFile(const char *path, const char *mode)
{
    FILE *fp = fopen(path, mode);
    Stream *s;

    if (fp == NULL) {
        return NULL;
    }
    s = calloc(1, sizeof(Stream));
    if (s == NULL) {
        fclose(fp);
        return NULL;
    }
    s->type = STREAM_FILE;
    s->fp = fp;
    return s;
}

char *__cdecl Stream_Gets(Stream *s, char *buf, int size)
{
    return fgets(buf, size, s->fp);
}

long __cdecl Stream_GetSize(Stream *s)
{
    long cur = ftell(s->fp);
    long size;

    fseek(s->fp, 0, SEEK_END);
    size = ftell(s->fp);
    fseek(s->fp, cur, SEEK_SET);
    return size;
}

int __cdecl Stream_Seek(Stream *s, long offset, int origin)
{
    return fseek(s->fp, offset, origin);
}

long __cdecl Stream_Tell(Stream *s)
{
    return ftell(s->fp);
}

int __cdecl Stream_Close(Stream *s)
{
    if (s == NULL) {
        return 0;
    }
    if (s->fp != NULL) {
        fclose(s->fp);
    }
    free(s);
    return 0;
}

/* ---- the test ---- */
static int failures;

static void check_int(Ini *ini, const char *key, int want)
{
    int value = -1;
    int found = Ini_GetInt(ini, key, &value);

    if (found != 1 || value != want) {
        printf("FAIL: %s found=%d value=%d, wanted %d\n", key, found, value, want);
        failures++;
    }
}

static void check_float(Ini *ini, const char *key, float want)
{
    float value = -1.0f;
    int found = Ini_GetFloat(ini, key, &value);

    if (found != 1 || value != want) {
        printf("FAIL: %s found=%d value=%g, wanted %g\n", key, found, (double)value, (double)want);
        failures++;
    }
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "test_ini_groups.ini";
    FILE *fp;
    Ini *ini;
    int group;
    int group_line;
    int cursor;
    int at_end;
    int groups;

    printf("abi: %d-bit build\n", (int)(sizeof(void *) * 8));

    fp = fopen(path, "w");
    if (fp == NULL) {
        perror(path);
        return 1;
    }
    fputs("[Bound]\n"
          "screenleft = 15\n"
          "screenright = 15\n"
          "\n"
          "[StageInfo]\n"
          "zoffset = 200\n"
          "autoturn = 1\n"
          "resetBG = 1\n"
          "localcoord = 320, 240\n"
          "xscale = 1\n"
          "yscale = 1\n"
          "\n"
          "[Shadow]\n"
          "intensity = 64\n"
          "\n"
          "[bg 0]\n"
          "type = normal\n"
          "\n"
          "[bg 1]\n"
          "type = normal\n",
          fp);
    fclose(fp);

    ini = Ini_Open(path);
    if (ini == NULL) {
        printf("FAIL: Ini_Open(%s)\n", path);
        return 1;
    }
    if (Ini_SelectGroup(ini, "StageInfo") == -1) {
        printf("FAIL: [StageInfo] not found\n");
        return 1;
    }
    check_int(ini, "zoffset", 200);

    group = ini->group;
    group_line = ini->group_line;
    cursor = ini->cursor;
    at_end = ini->at_end;

    /* The Stage_Load diagnostic block, verbatim in shape. */
    groups = Ini_CountGroups(ini, "bg]") + Ini_CountGroups(ini, "bg ");
    if (groups != 2) {
        printf("FAIL: counted %d [bg N] groups, wanted 2\n", groups);
        failures++;
    }
    if (ini->group != group) {
        printf("FAIL: group index moved %d -> %d\n", group, ini->group);
        failures++;
    }
    if (ini->group_line != group_line) {
        printf("FAIL: group line moved %d -> %d\n", group_line, ini->group_line);
        failures++;
    }
    if (ini->cursor != cursor || ini->at_end != at_end) {
        printf("FAIL: cursor not preserved (cursor=%d->%d at_end=%d->%d, group line %d)\n",
               cursor, ini->cursor, at_end, ini->at_end, ini->group_line);
        failures++;
    }

    /* These are the reads that silently kept their defaults in the bug. */
    check_int(ini, "autoturn", 1);
    check_int(ini, "resetbg", 1);
    check_float(ini, "xscale", 1.0f);
    check_float(ini, "yscale", 1.0f);

    Ini_Free(ini);
    remove(path);

    if (failures != 0) {
        printf("FAIL: %d check(s)\n", failures);
        return 1;
    }
    printf("ok: counting groups keeps [StageInfo] selected and its keys readable\n");
    return 0;
}
