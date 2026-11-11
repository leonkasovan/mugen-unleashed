/*
 * Port addition: merges a platform's own configuration over the game's
 * data/mugen.cfg.
 *
 * This module is not part of the original binary; there is no address to name.
 * The engine reads exactly one configuration file (config.c, Config_Load), and
 * a port whose device needs different settings -- resolution, renderer,
 * precache -- would otherwise have to replace the game tree's file, losing
 * whatever the user set. So a second file is layered over it instead.
 *
 * The semantics, and why each of them is what it is:
 *
 *   - Per key, not per file and not per group. Replacing the whole file or
 *     whole groups drops the settings the platform file does not mention
 *     ([Video] BlitMode/Resizable/VRetrace, [Options] Difficulty/Life/Time,
 *     [Sound] volumes, ...), and those must come from the game's file.
 *
 *   - The platform file wins over the game's, because its values are what the
 *     device needs. Ini_GetValue returns the FIRST matching line of the
 *     selected group (ini.c), so within each group the platform's lines are
 *     emitted before the game's.
 *
 *   - Nothing the engine writes back is forced. Options_Save
 *     (config.c 0x0045F5B0), the FirstRun clear and
 *     Config_WriteMissingGamepadGroups all write data/mugen.cfg; a platform
 *     value that outranked them would be silently restored on the next boot.
 *     Those groups are copied from the game's file untouched.
 *
 *   - Matching is case-insensitive, like every Ini lookup: g_iniCaseSensitive
 *     is never set anywhere in the engine.
 *
 *   - The merged text is a fresh buffer, never a file. The game tree may be
 *     read-only (a PS Vita VPK is mounted read-only), and Ini_SetValuef-style
 *     edits touch only the source lines -- lookups keep using the parsed
 *     snapshot until a save and reopen (ini.c) -- so they cannot merge.
 */
#include "mugen.h"
#include "configmerge.h"

/*
 * Groups whose keys the engine writes back, or that a platform policy owns.
 * A value for any of these in the overlay would fight the options screen:
 *
 *   [Options]        Options_Save writes Difficulty, Life, Time, GameSpeed,
 *                    Team.1VS2Life, Team.LoseOnKO.
 *   [P<n> Keys]      Options_Save writes them (Config_WriteKeys).
 *   [P<n> Joystick]  Options_Save writes them; on the SDL2 targets the runtime
 *                    pad policy (Config_ApplyJoystickSlot) owns them anyway.
 *   [P<n> Gamepad]   Config_WriteMissingGamepadGroups writes them.
 *
 * [Config] FirstRun is deliberately NOT skipped: forcing it to 0 is what keeps
 * the engine from rewriting data/mugen.cfg on a read-only tree.
 */
static const char *const merge_skip_groups[] = {
    "Options",
    "P1 Keys", "P2 Keys",
    "P1 Joystick", "P2 Joystick",
    "P1 Gamepad", "P2 Gamepad",
};

/* Individual keys the engine writes back in groups it otherwise does not own. */
static const char *const merge_skip_keys[] = {
    "MasterVolume", "WavVolume", "BGMVolume",           /* [Sound] */
    "p1.joystick.type", "p2.joystick.type",             /* [Input] */
};

/* Growable text buffer holding the merged file. */
typedef struct {
    char *text;
    int len;
    int cap;
} MergeText;

/* The parser's notion of a blank character (ini.c, Ini_IsBlank). */
static int Merge_IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Appends one line plus '\n'; 0 on allocation failure. */
static int MergeText_Append(MergeText *t, const char *line)
{
    int len = (int)strlen(line);
    int need = t->len + len + 2;        /* line, '\n', terminator */
    char *grown;
    int cap;

    if (need > t->cap) {
        cap = t->cap > 0 ? t->cap : 0x800;
        while (cap < need) {
            cap *= 2;
        }
        grown = realloc(t->text, (size_t)cap);
        if (grown == NULL) {
            return 0;
        }
        t->text = grown;
        t->cap = cap;
    }
    if (len > 0) {
        memcpy(t->text + t->len, line, (size_t)len);
    }
    t->len += len;
    t->text[t->len++] = '\n';
    t->text[t->len] = '\0';
    return 1;
}

/* Length of a "[group]" header line, 0 when the line is not one. */
static int Merge_HeaderLength(const char *line)
{
    int len = (int)strlen(line);

    if (len < 3 || line[0] != '[' || line[len - 1] != ']') {
        return 0;
    }
    return len;
}

/* Index into ini->group_lines of the group spelled exactly like header, or -1. */
static int Merge_FindGroup(const Ini *ini, const char *header)
{
    int len = (int)strlen(header);
    int i;

    for (i = 0; i < ini->group_count; i++) {
        const char *h = ini->lines[ini->group_lines[i]];

        if ((int)strlen(h) == len && Str_PrefixMatchI(h, header, len)) {
            return i;
        }
    }
    return -1;
}

/* One past the last line of the group: the next header, or the end of the file. */
static int Merge_GroupEnd(const Ini *ini, int group)
{
    if (group + 1 < ini->group_count) {
        return ini->group_lines[group + 1];
    }
    return ini->line_count;
}

/*
 * Length of the key part of a "key = value" line: everything before '=' with
 * trailing blanks removed. 0 when the line carries no '=' at all.
 */
static int Merge_KeyLength(const char *line)
{
    int len;

    for (len = 0; line[len] != '\0' && line[len] != '='; len++) {
    }
    if (len == 0 || line[len] != '=') {
        return 0;
    }
    while (len > 0 && Merge_IsBlank(line[len - 1])) {
        len--;
    }
    return len;
}

/* Two keys are the same key: same length, case-insensitive. */
static int Merge_KeyEqual(const char *a, int a_len, const char *b, int b_len)
{
    return a_len == b_len && Str_PrefixMatchI(a, b, a_len);
}

/* The line's key is name. */
static int Merge_LineKeyIs(const char *line, const char *name)
{
    int len = Merge_KeyLength(line);
    int name_len = (int)strlen(name);

    return len == name_len && Str_PrefixMatchI(line, name, len);
}

/* The group header names a write-back group. */
static int Merge_SkipsGroup(const char *header)
{
    int len = Merge_HeaderLength(header) - 2;   /* inside the brackets */
    const char *group = header + 1;
    const char *dot;
    int i;

    if (len <= 0) {
        return 0;
    }
    /* Port fix: localized write-back groups obey the same ownership rule. */
    dot = memchr(group, '.', (size_t)len);
    if (dot != NULL) {
        len -= (int)(dot + 1 - group);
        group = dot + 1;
    }
    for (i = 0; i < (int)(sizeof(merge_skip_groups) / sizeof(merge_skip_groups[0])); i++) {
        const char *name = merge_skip_groups[i];

        if ((int)strlen(name) == len && Str_PrefixMatchI(group, name, len)) {
            return 1;
        }
    }
    return 0;
}

/* The line's key is one the engine writes back. */
static int Merge_SkipsKey(const char *line)
{
    int i;

    if (Merge_KeyLength(line) == 0) {
        return 0;       /* a header or a malformed line: nothing to skip */
    }
    for (i = 0; i < (int)(sizeof(merge_skip_keys) / sizeof(merge_skip_keys[0])); i++) {
        if (Merge_LineKeyIs(line, merge_skip_keys[i])) {
            return 1;
        }
    }
    return 0;
}

/*
 * The overlay's group defines the key of this line, and the overlay is allowed
 * to force it. Used to drop the game's line: emitting both would resolve to the
 * overlay's value anyway (first match wins), but leaving the duplicate in makes
 * a merged section read as if it had two different settings.
 */
static int Merge_OverlayHasKey(const Ini *over, int group, const char *line)
{
    int key_len = Merge_KeyLength(line);
    int i;
    int end;

    if (key_len == 0) {
        return 0;
    }
    end = Merge_GroupEnd(over, group);
    for (i = over->group_lines[group] + 1; i < end; i++) {
        const char *o = over->lines[i];
        int o_len = Merge_KeyLength(o);

        if (o_len == 0 || Merge_SkipsKey(o)) {
            continue;
        }
        if (Merge_KeyEqual(line, key_len, o, o_len)) {
            return 1;
        }
    }
    return 0;
}

/* Emits the overlay's own lines of one group; counts what it added. */
static int Merge_AppendGroupBody(MergeText *out, const Ini *over, int group, int *keys)
{
    int end = Merge_GroupEnd(over, group);
    int i;

    for (i = over->group_lines[group] + 1; i < end; i++) {
        const char *line = over->lines[i];

        if (Merge_HeaderLength(line) != 0) {
            break;      /* defensive: a group body never holds a header */
        }
        if (Merge_SkipsKey(line)) {
            continue;
        }
        if (!MergeText_Append(out, line)) {
            return 0;
        }
        (*keys)++;
    }
    return 1;
}

int ConfigMerge_Text(const Ini *base, const Ini *over,
                     char **text, int *text_len, int *keys, int *skipped)
{
    MergeText out;
    int bi;
    int oi;

    *text = NULL;
    *text_len = 0;
    *keys = 0;
    *skipped = 0;
    if (base == NULL || over == NULL || base->group_count == 0 || over->group_count == 0) {
        return 0;       /* nothing to layer: the caller keeps the base Ini */
    }
    memset(&out, 0, sizeof(out));

    /* The game's file in its own group order, with the overlay layered in. */
    for (bi = 0; bi < base->group_count; bi++) {
        const char *header = base->lines[base->group_lines[bi]];
        int end = Merge_GroupEnd(base, bi);
        int i;

        oi = Merge_FindGroup(over, header);
        if (!MergeText_Append(&out, header)) {
            goto fail;
        }
        if (oi < 0 || Merge_SkipsGroup(header)) {
            if (oi >= 0) {
                (*skipped)++;
            }
            for (i = base->group_lines[bi] + 1; i < end; i++) {
                if (!MergeText_Append(&out, base->lines[i])) {
                    goto fail;
                }
            }
            continue;
        }
        if (!Merge_AppendGroupBody(&out, over, oi, keys)) {
            goto fail;
        }
        for (i = base->group_lines[bi] + 1; i < end; i++) {
            if (Merge_OverlayHasKey(over, oi, base->lines[i])) {
                continue;
            }
            if (!MergeText_Append(&out, base->lines[i])) {
                goto fail;
            }
        }
    }

    /* Groups only the platform file has, in that file's order. */
    for (oi = 0; oi < over->group_count; oi++) {
        const char *header = over->lines[over->group_lines[oi]];

        if (Merge_FindGroup(base, header) >= 0) {
            continue;
        }
        if (Merge_SkipsGroup(header)) {
            (*skipped)++;
            continue;
        }
        if (!MergeText_Append(&out, header)) {
            goto fail;
        }
        if (!Merge_AppendGroupBody(&out, over, oi, keys)) {
            goto fail;
        }
    }

    if (out.text == NULL) {
        return 0;       /* no group was emitted: nothing to merge */
    }
    *text = out.text;
    *text_len = out.len;
    return 1;

fail:
    free(out.text);
    *keys = 0;
    *skipped = 0;
    return 0;
}