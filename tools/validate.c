/*
 * validate -- check that a game pack's asset paths resolve on this filesystem.
 *
 * Why: Windows compares filenames case-insensitively, Linux does not. A pack
 * built on Windows routinely writes "chars/KFM/KFM.def" for a file that is
 * really "chars/kfm/kfm.def", or separates paths with backslashes
 * ("states\constants.cns"). Both work on Windows and both fail here, usually as
 * an unhelpful "Error reading character file" deep inside the select screen.
 *
 * Derived from Ikemen-GO's external/script/validate.lua, rewritten in C because
 * this engine cannot run a script: Lua_RunInit loads data/init.lua and nothing
 * else, the console's "lexec" takes a single line, and the vendored Lua is built
 * without io.popen. As a plain C tool it needs no Lua, no engine and no display,
 * so it runs on a build box and on the handheld alike.
 *
 * The lookup order it models is the engine's own:
 *   - File_Find (src/file.c): base, motif dir, "./", "data/";
 *   - character [Files] values are joined onto the character's directory
 *     (src/char.c Char_OpenFile -> Path_Join(name, dir, path)), so the correct
 *     spelling for a character is "files/c18.sff", not "C18Z2/files/c18.sff";
 *   - src/char.c reads cmd, cns, anim, stcommon, sprite, sound and st0..st9;
 *   - src/select.c resolves a bare "kfm" cell as chars/kfm/kfm.def.
 *
 * Modes:
 *   (default)      report only; nothing on disk is modified
 *   --fix          rewrite the offending values in place, keeping a .bak
 *   --quiet        summary only, no per-entry lines
 *   --out FILE     write the report to FILE instead of validation<TIME>.txt
 *
 * Exit status: 0 everything resolves, 1 problems found, 2 usage error.
 *
 * Build: make validate   (or cc -O2 -o validate tools/validate.c)
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define VERSION "1.0"

/* ---------------------------------------------------------------- utilities */

/* MinGW has no localtime_r; localtime() is fine here because the tool is
 * single-threaded and only stamps the report name. */
static void timestamp(time_t now, struct tm *out, const char *fmt, char *buf, size_t size)
{
#if defined(_WIN32)
    struct tm *tmp = localtime(&now);
    if (tmp != NULL) *out = *tmp;
#else
    (void)localtime_r(&now, out);
#endif
    strftime(buf, size, fmt, out);
}

static int g_quiet;
static int g_fix;

/* Counters, one per outcome. Kept together so the summary cannot drift. */
static struct {
    unsigned ok;
    unsigned case_mismatch;
    unsigned backslash;
    unsigned missing;
    unsigned fixed;
    unsigned renamed;
} g_count;

static FILE *g_report;

/* Case folding is ASCII-only on purpose: asset names in these packs are ASCII,
 * and tolower() in a non-C locale would fold bytes outside it inconsistently. */
static char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (lower_ascii(*a) != lower_ascii(*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Drop a trailing CR so a CRLF file parses the same as an LF one. */
static void strip_cr(char *s)
{
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\r') s[n - 1] = '\0';
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = '\0';
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, n - i + 1);
}

/* ------------------------------------------------------------------- paths */

/* Convert backslashes to forward slashes in place; returns 1 if any changed. */
static int to_posix(char *path)
{
    int changed = 0;
    char *p;

    for (p = path; *p != '\0'; p++) {
        if (*p == '\\') {
            *p = '/';
            changed = 1;
        }
    }
    return changed;
}

static int has_backslash(const char *s)
{
    return strchr(s, '\\') != NULL;
}

/* A path is a regular file (not a directory) we can open. */
static int is_file(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0') return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) != 0;
}

static int is_dir(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0') return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) != 0;
}

/*
 * Find `name` in `dir`, ignoring case. Writes the on-disk spelling to `out`.
 * Returns 1 on a hit. An exact-case hit is always preferred so a case-sensitive
 * filesystem with both "KFM" and "kfm" resolves deterministically.
 */
static int match_in_dir(const char *dir, const char *name, char *out, size_t out_size)
{
    DIR *d;
    struct dirent *ent;
    int found = 0;
    char exact[PATH_MAX];

    d = opendir(dir[0] == '\0' ? "." : dir);
    if (d == NULL) return 0;
    if (snprintf(exact, sizeof(exact), "%s/%s", dir[0] == '\0' ? "." : dir, name) >= (int)sizeof(exact)) {
        closedir(d);
        return 0;
    }
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
            continue;
        }
        if (ieq(ent->d_name, name)) {
            /* Prefer the exact spelling if it appears later in the listing. */
            if (strcmp(ent->d_name, name) == 0) {
                snprintf(out, out_size, "%s", ent->d_name);
                found = 1;
                break;
            }
            if (!found) {
                snprintf(out, out_size, "%s", ent->d_name);
                found = 1;
            }
        }
    }
    closedir(d);
    return found;
}

/* ------------------------------------------------------------- path resolver */

typedef enum {
    RES_OK = 0,      /* exact spelling exists under some base */
    RES_CASE,        /* exists, but the spelling differs */
    RES_SEP,         /* exists after converting backslashes */
    RES_MISSING      /* nothing matched */
} ResStatus;

#define MAX_BASES 6
#define MAX_PARTS 32

typedef struct {
    char matched[PATH_MAX];  /* the path as it exists on disk */
    char base[PATH_MAX];     /* the base it was found under */
    ResStatus status;
} Resolved;

/*
 * Resolve `value` against each of `bases` in turn, walking every component
 * case-insensitively. On a hit, fills *out and returns 1.
 *
 * The walk matters as much as the base order: "chars\C18Z2\C18Z2.def" needs
 * every component corrected independently, and only the components present on
 * disk can be trusted.
 */
static int resolve(const char *value, const char *const *bases, int base_count, Resolved *out)
{
    char raw[PATH_MAX];
    char norm[PATH_MAX];
    int had_backslash;
    int b;

    memset(out, 0, sizeof(*out));
    if (value == NULL || value[0] == '\0') {
        out->status = RES_MISSING;
        return 0;
    }
    if (strlen(value) >= sizeof(raw)) {
        out->status = RES_MISSING;
        return 0;
    }
    strcpy(raw, value);
    /* A drive letter is meaningless here; src/file.c strips it too. */
    if (isalpha((unsigned char)raw[0]) && raw[1] == ':') memmove(raw, raw + 2, strlen(raw + 2) + 1);
    had_backslash = has_backslash(raw);
    strcpy(norm, raw);
    to_posix(norm);

    /* 1. Fast path: the engine opens exactly this. */
    for (b = 0; b < base_count; b++) {
        char candidate[PATH_MAX];

        if (snprintf(candidate, sizeof(candidate), "%s%s", bases[b], norm) >= (int)sizeof(candidate)) continue;
        if (is_file(candidate)) {
            snprintf(out->matched, sizeof(out->matched), "%s", candidate);
            snprintf(out->base, sizeof(out->base), "%s", bases[b]);
            out->status = had_backslash ? RES_SEP : RES_OK;
            return 1;
        }
    }

    /* 2. Walk the components case-insensitively under each base. */
    for (b = 0; b < base_count; b++) {
        char current[PATH_MAX];
        char parts[MAX_PARTS][PATH_MAX];
        int part_count = 0;
        char *p;
        int i, all_found = 1;

        snprintf(current, sizeof(current), "%s", bases[b]);
        /* Split on both separators. */
        for (p = norm; *p != '\0';) {
            char *start;
            size_t len;

            while (*p == '/') p++;
            if (*p == '\0') break;
            start = p;
            while (*p != '\0' && *p != '/') p++;
            len = (size_t)(p - start);
            if (part_count >= MAX_PARTS || len > NAME_MAX) {
                all_found = 0;
                break;
            }
            memcpy(parts[part_count], start, len);
            parts[part_count][len] = '\0';
            part_count++;
        }
        if (!all_found || part_count == 0) continue;

        for (i = 0; i < part_count; i++) {
            char actual[PATH_MAX];
            char next[PATH_MAX];

            if (!match_in_dir(current, parts[i], actual, sizeof(actual))) {
                all_found = 0;
                break;
            }
            if (snprintf(next, sizeof(next), "%s%s%s", current,
                         (current[0] != '\0' && current[strlen(current) - 1] != '/') ? "/" : "",
                         actual) >= (int)sizeof(next)) {
                all_found = 0;
                break;
            }
            strcpy(current, next);
            if (i < part_count - 1 && !is_dir(current)) {
                all_found = 0;
                break;
            }
        }
        if (all_found && is_file(current)) {
            snprintf(out->matched, sizeof(out->matched), "%s", current);
            snprintf(out->base, sizeof(out->base), "%s", bases[b]);
            out->status = had_backslash ? RES_SEP : RES_CASE;
            return 1;
        }
    }

    out->status = RES_MISSING;
    return 0;
}

/*
 * The spelling a def file should carry: the resolved path minus the base it was
 * found under. Writing the absolute form instead would pin the pack to this
 * machine's layout, and for a character the engine joins the value onto the
 * character directory, so a base-qualified path would be doubled.
 */
static void relative_to(const Resolved *r, char *out, size_t out_size)
{
    size_t base_len = strlen(r->base);

    if (base_len > 0 && strncmp(r->matched, r->base, base_len) == 0) {
        snprintf(out, out_size, "%s", r->matched + base_len);
        return;
    }
    snprintf(out, out_size, "%s", r->matched);
    if (out[0] == '.' && out[1] == '/') memmove(out, out + 2, strlen(out + 2) + 1);
}

/* ------------------------------------------------------------------- INI */

/* Cut a trailing ";" comment, but only outside double quotes. In place. */
static void strip_comment(char *line)
{
    int quoted = 0;
    char *p;

    for (p = line; *p != '\0'; p++) {
        if (*p == '"') quoted = !quoted;
        else if (*p == ';' && !quoted) {
            *p = '\0';
            return;
        }
    }
}

/* Parse "key = value". Returns 1 and fills key (lower-cased) and value
 * (unquoted, trimmed). Empty values are treated as absent, matching the engine. */
static int parse_assignment(const char *code, char *key, size_t key_size,
                            char *value, size_t value_size)
{
    const char *eq = strchr(code, '=');
    size_t n, i;

    if (eq == NULL) return 0;
    n = (size_t)(eq - code);
    while (n > 0 && (code[n - 1] == ' ' || code[n - 1] == '\t')) n--;
    if (n == 0 || n >= key_size) return 0;
    for (i = 0; i < n; i++) key[i] = lower_ascii(code[i]);
    key[n] = '\0';

    n = 0;
    eq++;
    while (*eq == ' ' || *eq == '\t') eq++;
    while (*eq != '\0' && n + 1 < value_size) {
        if (*eq != '"') value[n++] = *eq;   /* unquote, as the engine does */
        eq++;
    }
    value[n] = '\0';
    trim(value);
    return value[0] != '\0';
}

/* Read a whole file into a malloc'd NUL-terminated buffer. NULL on failure. */
static char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long len;

    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    buf = malloc((size_t)len + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    if (size != NULL) *size = (size_t)len;
    return buf;
}

/* ------------------------------------------------------------- file checking */

/* The keys the engine reads out of a given def group. */
typedef struct {
    const char *const *keys;
    int count;
} KeySet;

static const char *const k_system_keys[] = {
    "spr", "snd", "anim", "fight", "font", "font0", "font1", "font2", "font3",
    "font4", "font5", "font6", "font7", "font8", "font9"
};
static const char *const k_fight_keys[] = {
    "spr", "snd", "anim", "font", "font0", "font1", "font2", "font3",
    "font4", "font5", "font6", "font7", "font8", "font9"
};
/* src/char.c: cmd, cns, anim, stcommon, sprite, sound, and the st0..st9 loop. */
static const char *const k_char_keys[] = {
    "cmd", "cns", "anim", "stcommon", "sprite", "sound",
    "st", "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7", "st8", "st9"
};

static int key_wanted(const KeySet *set, const char *key)
{
    int i;

    for (i = 0; i < set->count; i++) {
        if (strcmp(set->keys[i], key) == 0) return 1;
    }
    return 0;
}

/*
 * Validate one def file, optionally rewriting it.
 *
 * Rewriting is line-preserving on purpose: the file is copied line by line and
 * only the value after '=' changes, so comments, spacing, blank lines and key
 * order survive. A parse-and-reprint would silently restructure a file the
 * author owns.
 */
static void validate_file(const char *path, const char *const *bases, int base_count,
                          const KeySet *keys, const char *label)
{
    char *content;
    char *line, *next;
    size_t size;
    int changed = 0;

    content = read_file(path, &size);
    if (content == NULL) {
        printf("  [%s] can not read %s\n", label, path);
        g_count.missing++;
        return;
    }

    for (line = content; line != NULL && *line != '\0'; line = next) {
        char code[PATH_MAX * 2];
        char key[128], value[PATH_MAX];
        Resolved r;

        next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            next++;
        }
        snprintf(code, sizeof(code), "%s", line);
        strip_cr(code);
        strip_comment(code);
        if (!parse_assignment(code, key, sizeof(key), value, sizeof(value))) continue;
        if (!key_wanted(keys, key)) continue;

        resolve(value, bases, base_count, &r);
        if (r.status == RES_OK) {
            g_count.ok++;
            if (!g_quiet) fprintf(g_report, "  [%s] %s = %s [OK]\n", label, key, value);
        } else if (r.status == RES_MISSING) {
            g_count.missing++;
            fprintf(g_report, "  [%s] %s = %s [NOT FOUND]\n", label, key, value);
        } else {
            char rewrite[PATH_MAX];

            relative_to(&r, rewrite, sizeof(rewrite));
            if (r.status == RES_SEP) g_count.backslash++;
            else g_count.case_mismatch++;
            fprintf(g_report, "  [%s] %s = %s [%s] -> %s\n", label, key, value,
                    r.status == RES_SEP ? "BACKSLASH" : "CASE", rewrite);
            changed = 1;
        }
    }
    free(content);

    if (!changed) return;
    if (!g_fix) {
        fprintf(g_report, "[FIX|DRY-RUN] %s would be rewritten; pass --fix\n", path);
        return;
    }
    /* Rewrite pass: same walk, substituting only values that resolve. */
    content = read_file(path, &size);
    if (content == NULL) return;
    {
        char backup[PATH_MAX];
        FILE *in, *out_file;
        char *l, *nx;

        snprintf(backup, sizeof(backup), "%s.bak", path);
        remove(backup);
        if (rename(path, backup) != 0) {
            fprintf(g_report, "[ERROR] could not back up %s: %s\n", path, strerror(errno));
            free(content);
            return;
        }
        in = fopen(backup, "rb");
        out_file = fopen(path, "wb");
        if (in == NULL || out_file == NULL) {
            if (in != NULL) fclose(in);
            if (out_file != NULL) fclose(out_file);
            rename(backup, path);
            fprintf(g_report, "[ERROR] could not rewrite %s\n", path);
            free(content);
            return;
        }
        for (l = content; l != NULL; l = nx) {
            char code[PATH_MAX * 2];
            char key[128], value[PATH_MAX];
            int crlf, had_nl = 0;

            nx = strchr(l, '\n');
            /* had_nl: the file really ended this line with '\n'. A final line
             * without one must not gain it, or every rewrite grows the file. */
            had_nl = (nx != NULL);
            if (nx != NULL) *nx++ = '\0';
            /* Packs built on Windows are usually CRLF; keep the file's own
             * line ending instead of silently rewriting it to LF. */
            crlf = (l[0] != '\0' && l[strlen(l) - 1] == '\r');
            snprintf(code, sizeof(code), "%s", l);
            strip_cr(code);
            strip_comment(code);   /* or ";comment" becomes part of the value */
            if (parse_assignment(code, key, sizeof(key), value, sizeof(value)) &&
                key_wanted(keys, key)) {
                Resolved r;
                resolve(value, bases, base_count, &r);
                if (r.status == RES_CASE || r.status == RES_SEP) {
                    char rewrite[PATH_MAX];
                    char *eq = strchr(l, '=');
                    const char *after_eq;
                    const char *last;
                    size_t lead, trail;

                    relative_to(&r, rewrite, sizeof(rewrite));
                    /*
                     * Splice the new value over the old one in the original
                     * line. Rebuilding from "key = value" would drop the space
                     * before a trailing ";" comment and any alignment the author
                     * used; the only bytes that may change are the value itself.
                     *
                     * The value sits between the first '=' and the point where
                     * strip_comment() cut the line, so `code` holds the extent.
                     */
                    after_eq = eq + 1;
                    /* The value ends where the comment was cut; `code` is the
                     * CR-stripped, comment-stripped copy, so its length is the
                     * right end offset into `l`. */
                    last = l + strlen(code);
                    lead = 0;
                    while (after_eq + lead < last &&
                           (after_eq[lead] == ' ' || after_eq[lead] == '\t')) {
                        lead++;
                    }
                    trail = 0;
                    while ((size_t)(last - after_eq) > lead + trail &&
                           (last[-1 - trail] == ' ' || last[-1 - trail] == '\t')) {
                        trail++;
                    }
                    fprintf(out_file, "%.*s", (int)(after_eq - l), l);
                    fprintf(out_file, "%.*s", (int)lead, after_eq);
                    fputs(rewrite, out_file);
                    {
                        const char *tail = last - trail;
                        size_t tail_len = strlen(tail);

                        /* The tail is the comment (or nothing). A file that used
                         * CRLF must keep exactly one CR before the LF. */
                        if (tail_len > 0 && tail[tail_len - 1] == '\r') {
                            fprintf(out_file, "%.*s", (int)(tail_len - 1), tail);
                            fputc('\r', out_file);
                        } else {
                            fputs(tail, out_file);
                            if (crlf) fputc('\r', out_file);
                        }
                    }
                    if (had_nl) fputc('\n', out_file);
                    g_count.fixed++;
                    continue;
                }
            }
            {
                char plain[PATH_MAX * 2];
                snprintf(plain, sizeof(plain), "%s", l);
                strip_cr(plain);
                fprintf(out_file, "%s", plain);
            }
            if (crlf) fputc('\r', out_file);
            if (had_nl) fputc('\n', out_file);
        }
        fclose(in);
        fclose(out_file);
        fprintf(g_report, "[FIXED] %s (backup %s)\n", path, backup);
    }
    free(content);
}

/* --------------------------------------------------------------- select.def */

/*
 * Resolve a select.def cell to the def file the engine would load: an explicit
 * ".def" path is used as-is, a bare name "kfm" means "kfm/kfm.def" (src/select.c).
 * Returns 0 when the result would not fit.
 */
static int cell_to_def(const char *cell, char *out, size_t out_size)
{
    size_t n = strlen(cell);
    int wrote;

    if (n > 4 && ieq(cell + n - 4, ".def")) {
        wrote = snprintf(out, out_size, "%s", cell);
    } else {
        wrote = snprintf(out, out_size, "%s/%s.def", cell, cell);
    }
    if (wrote < 0 || (size_t)wrote >= out_size) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

static int is_random(const char *cell)
{
    return ieq(cell, "randomselect") || ieq(cell, "random") || ieq(cell, "randomselect2");
}

/*
 * Walk [Characters] and [ExtraStages]. Each [Characters] line is
 * "char [, stage [, ...]]", so the first column is the character and the second
 * a default stage.
 */
static void check_select(const char *path)
{
    char *content = read_file(path, NULL);
    char *line, *next;
    char section[64] = "";
    static const char *const char_bases[] = { "", "chars/" };
    static const char *const stage_bases[] = { "", "stages/" };
    unsigned chars = 0, stages = 0;

    if (content == NULL) {
        printf("  select.def [NOT FOUND]\n");
        g_count.missing++;
        return;
    }

    for (line = content; line != NULL && *line != '\0'; line = next) {
        char code[PATH_MAX * 2];
        char *p, *comma;

        next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            next++;
        }
        snprintf(code, sizeof(code), "%s", line);
        strip_comment(code);
        trim(code);
        if (code[0] == '\0') continue;

        if (code[0] == '[') {
            char *close = strchr(code, ']');
            if (close != NULL) {
                size_t n = (size_t)(close - code - 1);
                if (n >= sizeof(section)) n = sizeof(section) - 1;
                memcpy(section, code + 1, n);
                section[n] = '\0';
                trim(section);
                {
                    size_t i;
                    for (i = 0; section[i] != '\0'; i++) section[i] = lower_ascii(section[i]);
                }
            }
            continue;
        }

        if (strcmp(section, "characters") == 0) {
            char ch[PATH_MAX * 2], stage[PATH_MAX * 2], def[PATH_MAX * 2];

            comma = strchr(code, ',');
            if (comma != NULL) {
                *comma = '\0';
            }
            p = comma != NULL ? comma + 1 : NULL;
            snprintf(ch, sizeof(ch), "%s", code);
            trim(ch);
            if (ch[0] == '\0') continue;
            chars++;
            stage[0] = '\0';
            if (p != NULL) {
                char *c2 = strchr(p, ',');
                if (c2 != NULL) *c2 = '\0';
                snprintf(stage, sizeof(stage), "%s", p);
                trim(stage);
            } else {
                stage[0] = '\0';
            }

            if (is_random(ch)) {
                g_count.ok++;
                if (!g_quiet) fprintf(g_report, "  %s [OK]\n", ch);
            } else if (!cell_to_def(ch, def, sizeof(def))) {
                g_count.missing++;
                fprintf(g_report, "  %s [PATH TOO LONG]\n", ch);
            } else {
                Resolved r;

                resolve(def, char_bases, 2, &r);
                if (r.status == RES_OK) {
                    g_count.ok++;
                    if (!g_quiet) fprintf(g_report, "  %s = %s [OK]\n", ch, r.matched);
                } else if (r.status == RES_MISSING) {
                    g_count.missing++;
                    fprintf(g_report, "  %s [NOT FOUND]\n", ch);
                } else {
                    char rewrite[PATH_MAX];

                    relative_to(&r, rewrite, sizeof(rewrite));
                    if (r.status == RES_SEP) g_count.backslash++;
                    else g_count.case_mismatch++;
                    fprintf(g_report, "  %s [%s] -> %s\n", ch,
                            r.status == RES_SEP ? "BACKSLASH" : "CASE", rewrite);
                }
            }
            if (stage[0] != '\0' && !is_random(stage)) {
                Resolved r;

                stages++;
                resolve(stage, stage_bases, 2, &r);
                if (r.status == RES_OK) {
                    g_count.ok++;
                    if (!g_quiet) fprintf(g_report, "    stage %s [OK]\n", stage);
                } else if (r.status == RES_MISSING) {
                    g_count.missing++;
                    fprintf(g_report, "    stage %s [NOT FOUND]\n", stage);
                } else {
                    if (r.status == RES_SEP) g_count.backslash++;
                    else g_count.case_mismatch++;
                    fprintf(g_report, "    stage %s [%s]\n", stage,
                            r.status == RES_SEP ? "BACKSLASH" : "CASE");
                }
            }
        } else if (strcmp(section, "extrastages") == 0) {
            char stage[PATH_MAX * 2];
            Resolved r;

            comma = strchr(code, ',');
            if (comma != NULL) *comma = '\0';
            snprintf(stage, sizeof(stage), "%s", code);
            trim(stage);
            /* "stages/foo.def, order = 1" -- drop parameters after a comma was
             * already handled; strip a trailing "unlock=..." fragment too. */
            p = strstr(stage, "unlock");
            if (p != NULL) {
                while (p > stage && (p[-1] == ' ' || p[-1] == ',')) p--;
                *p = '\0';
                trim(stage);
            }
            if (stage[0] == '\0') continue;
            stages++;
            resolve(stage, stage_bases, 2, &r);
            if (r.status == RES_OK) {
                g_count.ok++;
                if (!g_quiet) fprintf(g_report, "  stage %s [OK]\n", stage);
            } else if (r.status == RES_MISSING) {
                g_count.missing++;
                fprintf(g_report, "  stage %s [NOT FOUND]\n", stage);
            } else {
                if (r.status == RES_SEP) g_count.backslash++;
                else g_count.case_mismatch++;
                fprintf(g_report, "  stage %s [%s]\n", stage,
                        r.status == RES_SEP ? "BACKSLASH" : "CASE");
            }
        }
    }
    fprintf(g_report, "  entries: %u characters, %u stage references\n", chars, stages);
    free(content);
}
/*
 * Rewrite select.def in place: convert backslash (or wrongly-cased) cell and
 * stage paths to the spelling that exists on disk.
 *
 * Only two columns are paths: the character (first column of a [Characters]
 * line) and its default stage (second column). Everything from the third comma
 * on is parameters -- "includestage=0", "order=3" -- and is copied verbatim.
 * [ExtraStages] lines likewise have the stage in their first column.
 *
 * Kept separate from check_select() on purpose: reporting must not depend on
 * --fix, and the rewrite must not re-derive what was already reported.
 */
static void fix_select(const char *path)
{
    static const char *const char_bases[] = { "", "chars/" };
    static const char *const stage_bases[] = { "", "stages/" };
    char *content;
    char *line, *next;
    char section[64] = "";
    int changed = 0;
    FILE *out_file;

    content = read_file(path, NULL);
    if (content == NULL) return;

    /* First pass: is there anything to change? Avoids a pointless .bak. */
    {
        char *l, *nx;
        char sec[64] = "";

        for (l = content; l != NULL; l = nx) {
            char code[PATH_MAX * 2];

            nx = strchr(l, '\n');
            if (nx != NULL) *nx++ = '\0';
            snprintf(code, sizeof(code), "%s", l);
            strip_cr(code);
            strip_comment(code);
            trim(code);
            if (code[0] == '[') {
                char *close = strchr(code, ']');
                size_t n, i;

                if (close == NULL) continue;
                n = (size_t)(close - code - 1);
                if (n >= sizeof(sec)) n = sizeof(sec) - 1;
                memcpy(sec, code + 1, n);
                sec[n] = '\0';
                trim(sec);
                for (i = 0; sec[i] != '\0'; i++) sec[i] = lower_ascii(sec[i]);
                continue;
            }
            if (strcmp(sec, "characters") == 0) {
                char first[PATH_MAX * 2], def[PATH_MAX * 2];
                char *comma = strchr(code, ',');
                Resolved r;

                if (comma != NULL) *comma = '\0';
                snprintf(first, sizeof(first), "%s", code);
                trim(first);
                if (first[0] == '\0' || is_random(first)) continue;
                if (!cell_to_def(first, def, sizeof(def))) continue;
                resolve(def, char_bases, 2, &r);
                if (r.status == RES_CASE || r.status == RES_SEP) changed = 1;
            } else if (strcmp(sec, "extrastages") == 0) {
                char stage[PATH_MAX * 2];
                char *comma = strchr(code, ',');
                Resolved r;

                if (comma != NULL) *comma = '\0';
                snprintf(stage, sizeof(stage), "%s", code);
                trim(stage);
                if (stage[0] == '\0') continue;
                resolve(stage, stage_bases, 2, &r);
                if (r.status == RES_CASE || r.status == RES_SEP) changed = 1;
            }
        }
    }
    if (!changed) {
        free(content);
        return;
    }

    /*
     * The scan above split `content` in place (it writes NUL over every '\n'),
     * so it cannot be walked a second time. Re-read the file for the rewrite.
     */
    free(content);
    content = read_file(path, NULL);
    if (content == NULL) return;

    {
        char backup[PATH_MAX + 8];

        snprintf(backup, sizeof(backup), "%s.bak", path);
        remove(backup);
        if (rename(path, backup) != 0) {
            fprintf(g_report, "[ERROR] could not back up %s: %s\n", path, strerror(errno));
            free(content);
            return;
        }
        out_file = fopen(path, "wb");
        if (out_file == NULL) {
            rename(backup, path);
            free(content);
            return;
        }
    }

    for (line = content; line != NULL; line = next) {
        char code[PATH_MAX * 2];
        int crlf, had_nl;

        next = strchr(line, '\n');
        had_nl = (next != NULL);
        if (next != NULL) *next++ = '\0';
        crlf = (line[0] != '\0' && line[strlen(line) - 1] == '\r');
        snprintf(code, sizeof(code), "%s", line);
        strip_cr(code);
        strip_comment(code);
        trim(code);

        if (code[0] == '[') {
            char *close = strchr(code, ']');

            if (close != NULL) {
                size_t n = (size_t)(close - code - 1), i;

                if (n >= sizeof(section)) n = sizeof(section) - 1;
                memcpy(section, code + 1, n);
                section[n] = '\0';
                trim(section);
                for (i = 0; section[i] != '\0'; i++) section[i] = lower_ascii(section[i]);
            }
        } else if (strcmp(section, "characters") == 0) {
            /*
             * Split into columns. Column 0 is the character, column 1 its
             * default stage, the rest are parameters. Each path column is
             * rebuilt from its own leading/trailing whitespace so the line's
             * shape survives.
             */
            char work[PATH_MAX * 2];
            char out_line[PATH_MAX * 4];
            char *cols[16];
            int ncols = 0, k, rewritten = 0;
            char *c;
            size_t used = 0;

            snprintf(work, sizeof(work), "%s", line);
            strip_cr(work);
            cols[ncols++] = work;
            for (c = work; *c != '\0' && ncols < 16; c++) {
                if (*c == ',') {
                    *c = '\0';
                    cols[ncols++] = c + 1;
                }
            }
            out_line[0] = '\0';

            for (k = 0; k < ncols && k < 2; k++) {
                char cell[PATH_MAX * 2], cell_out[PATH_MAX * 2];
                const char *raw = cols[k];
                size_t raw_len = strlen(raw), lead = 0, trail = 0;
                Resolved r;
                int is_char = (k == 0);

                while (lead < raw_len && (raw[lead] == ' ' || raw[lead] == '\t')) lead++;
                while (trail + lead < raw_len &&
                       (raw[raw_len - 1 - trail] == ' ' || raw[raw_len - 1 - trail] == '\t')) {
                    trail++;
                }
                snprintf(cell, sizeof(cell), "%.*s", (int)(raw_len - lead - trail), raw + lead);
                snprintf(cell_out, sizeof(cell_out), "%s", cell);

                if (cell[0] != '\0' && !is_random(cell)) {
                    if (is_char) {
                        char def[PATH_MAX * 2];

                        if (cell_to_def(cell, def, sizeof(def))) {
                            resolve(def, char_bases, 2, &r);
                            if (r.status == RES_CASE || r.status == RES_SEP) {
                                char full[PATH_MAX * 2];

                                relative_to(&r, full, sizeof(full));
                                /*
                                 * A cell is a character reference, not a file:
                                 * the engine appends "/<name>.def" itself, so
                                 * reduce the path back to the cell's shape and
                                 * only when the cell was a bare name.
                                 */
                                if (!(strlen(cell) > 4 && ieq(cell + strlen(cell) - 4, ".def"))) {
                                    size_t n = strlen(full);
                                    char *slash;

                                    if (n > 4 && ieq(full + n - 4, ".def")) full[n - 4] = '\0';
                                    slash = strrchr(full, '/');
                                    if (slash != NULL) {
                                        *slash = '\0';
                                        slash = strrchr(full, '/');
                                        if (slash != NULL) memmove(full, slash + 1, strlen(slash + 1) + 1);
                                    }
                                }
                                snprintf(cell_out, sizeof(cell_out), "%s", full);
                                rewritten = 1;
                            }
                        }
                    } else {
                        resolve(cell, stage_bases, 2, &r);
                        if (r.status == RES_CASE || r.status == RES_SEP) {
                            relative_to(&r, cell_out, sizeof(cell_out));
                            rewritten = 1;
                        }
                    }
                }
                used += (size_t)snprintf(out_line + used, sizeof(out_line) - used, "%s%.*s%s%.*s",
                                         k == 0 ? "" : ",", (int)lead, raw, cell_out,
                                         (int)trail, raw + raw_len - trail);
            }
            for (k = 2; k < ncols; k++) {
                used += (size_t)snprintf(out_line + used, sizeof(out_line) - used, ",%s", cols[k]);
            }

            if (rewritten) {
                fputs(out_line, out_file);
                if (crlf) fputc('\r', out_file);
                if (had_nl) fputc('\n', out_file);
                g_count.fixed++;
                continue;
            }
        } else if (strcmp(section, "extrastages") == 0) {
            char work[PATH_MAX * 2];
            char stage_out[PATH_MAX * 2];
            char *comma = strchr(line, ',');
            const char *raw;
            size_t raw_len, lead = 0, trail = 0;
            Resolved r;

            snprintf(work, sizeof(work), "%s", line);
            strip_cr(work);
            /* Keep everything from the first comma on (order=.../unlock=...). */
            {
                char *c = strchr(work, ',');
                if (c != NULL) *c = '\0';
            }
            raw = work;
            raw_len = strlen(raw);
            while (lead < raw_len && (raw[lead] == ' ' || raw[lead] == '\t')) lead++;
            while (trail + lead < raw_len &&
                   (raw[raw_len - 1 - trail] == ' ' || raw[raw_len - 1 - trail] == '\t')) {
                trail++;
            }
            {
                char cell[PATH_MAX * 2];
                int changed_here = 0;

                snprintf(cell, sizeof(cell), "%.*s", (int)(raw_len - lead - trail), raw + lead);
                snprintf(stage_out, sizeof(stage_out), "%s", cell);
                if (cell[0] != '\0') {
                    resolve(cell, stage_bases, 2, &r);
                    if (r.status == RES_CASE || r.status == RES_SEP) {
                        relative_to(&r, stage_out, sizeof(stage_out));
                        changed_here = 1;
                    }
                }
                if (changed_here) {
                    fprintf(out_file, "%.*s%s%.*s", (int)lead, raw, stage_out,
                            (int)trail, raw + raw_len - trail);
                    if (comma != NULL) fputs(comma, out_file);
                    if (crlf) fputc('\r', out_file);
                    if (had_nl) fputc('\n', out_file);
                    g_count.fixed++;
                    continue;
                }
            }
        }
        /* Unchanged line: emit the original text with its ending intact. */
        {
            char plain[PATH_MAX * 2];

            snprintf(plain, sizeof(plain), "%s", line);
            strip_cr(plain);
            fputs(plain, out_file);
        }
        if (crlf) fputc('\r', out_file);
        if (had_nl) fputc('\n', out_file);
    }
    fclose(out_file);
    fprintf(g_report, "[FIXED] %s (backup %s.bak)\n", path, path);
    free(content);
}

/* ------------------------------------------------------------- char def pass */

static void check_character_defs(const char *select_path)
{
    char *content = read_file(select_path, NULL);
    char *line, *next;
    char section[64] = "";
    static const char *const char_bases[] = { "", "chars/" };
    KeySet keys;
    char seen[256][PATH_MAX];
    int seen_count = 0;
    int i;

    keys.keys = k_char_keys;
    keys.count = (int)(sizeof(k_char_keys) / sizeof(k_char_keys[0]));

    if (content == NULL) return;
    for (line = content; line != NULL && *line != '\0'; line = next) {
        char code[PATH_MAX * 2];
        char *comma;
        char ch[PATH_MAX * 2], def[PATH_MAX * 2], cdir[PATH_MAX];
        Resolved r;
        char *slash;
        int dup = 0;

        next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            next++;
        }
        snprintf(code, sizeof(code), "%s", line);
        strip_comment(code);
        trim(code);
        if (code[0] == '\0') continue;
        if (code[0] == '[') {
            char *close = strchr(code, ']');
            if (close != NULL) {
                size_t n = (size_t)(close - code - 1);
                if (n >= sizeof(section)) n = sizeof(section) - 1;
                memcpy(section, code + 1, n);
                section[n] = '\0';
                trim(section);
                for (n = 0; section[n] != '\0'; n++) {
                    section[n] = lower_ascii(section[n]);
                }
            }
            continue;
        }
        if (strcmp(section, "characters") != 0) continue;

        comma = strchr(code, ',');
        if (comma != NULL) *comma = '\0';
        snprintf(ch, sizeof(ch), "%s", code);
        trim(ch);
        if (ch[0] == '\0' || is_random(ch)) continue;

        if (!cell_to_def(ch, def, sizeof(def))) continue;  /* reported by check_select */
        resolve(def, char_bases, 2, &r);
        /*
         * Inspect every character whose def actually exists, not just the ones
         * spelled exactly right: the interesting files are the ones that
         * resolved through a backslash or a case fix, and skipping them would
         * report a character as safe while its [Files] are still broken.
         * RES_MISSING was already reported by check_select().
         */
        if (r.status == RES_MISSING) continue;
        for (i = 0; i < seen_count; i++) {
            if (strcmp(seen[i], r.matched) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;
        if (seen_count < (int)(sizeof(seen) / sizeof(seen[0]))) {
            snprintf(seen[seen_count++], PATH_MAX, "%s", r.matched);
        }

        /* Character files are joined onto the character's own directory. */
        snprintf(cdir, sizeof(cdir), "%s", r.matched);
        slash = strrchr(cdir, '/');
        if (slash != NULL) slash[1] = '\0';
        {
            const char *bases[5];

            bases[0] = cdir;
            bases[1] = "";
            bases[2] = "chars/";
            bases[3] = "data/";
            fprintf(g_report, "character: %s\n", r.matched);
            validate_file(r.matched, bases, 4, &keys, "Files");
        }
    }
    fprintf(g_report, "  %d character def(s) inspected\n", seen_count);
    free(content);
}

/* ------------------------------------------------------------------- config */

static void check_config(char *motif, size_t motif_size)
{
    static const char *const names[] = { "data/mugen.cfg", "data/mugen.arm64.cfg" };
    const char *cfg = NULL;
    char *content;
    char *line, *next;
    int in_options = 0;
    int i;

    printf("=== [Config] ===\n");
    for (i = 0; i < 2; i++) {
        if (is_file(names[i])) {
            cfg = names[i];
            break;
        }
    }
    if (cfg == NULL) {
        g_count.missing++;
        printf("  config: data/mugen.cfg [NOT FOUND]\n");
        snprintf(motif, motif_size, "data/system.def");
        return;
    }
    g_count.ok++;
    printf("  config: using %s\n", cfg);

    content = read_file(cfg, NULL);
    if (content == NULL) return;
    for (line = content; line != NULL && *line != '\0'; line = next) {
        char code[PATH_MAX * 2];
        char key[128], value[PATH_MAX];
        char *close;

        next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            next++;
        }
        snprintf(code, sizeof(code), "%s", line);
        strip_comment(code);
        trim(code);
        if (code[0] == '\0') continue;
        if (code[0] == '[' && (close = strchr(code, ']')) != NULL) {
            *close = '\0';
            in_options = ieq(code + 1, "options");
            continue;
        }
        if (in_options && parse_assignment(code, key, sizeof(key), value, sizeof(value)) &&
            strcmp(key, "motif") == 0) {
            snprintf(motif, motif_size, "%s", value);
        }
    }
    free(content);
}

/* ------------------------------------------------------------------- usage */

static void usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "usage: %s [--fix] [--quiet] [--out FILE]\n"
            "\n"
            "Checks that a game pack's asset paths resolve on this filesystem.\n"
            "Run it from the game directory (the one holding chars/, data/).\n"
            "\n"
            "  --fix          rewrite offending values in place (keeps a .bak)\n"
            "  --quiet        summary only\n"
            "  --out FILE     write the report to FILE, not validation<TIME>.txt\n"
            "  -h, --help     this text\n"
            "  -V, --version  version\n"
            "\n"
            "Exit status: 0 all paths resolve, 1 problems found, 2 usage error.\n",
            argv0);
}

int main(int argc, char **argv)
{
    char report_path[PATH_MAX];
    char motif[PATH_MAX] = "data/system.def";
    char motif_dir[PATH_MAX] = "";
    char select_path[PATH_MAX] = "";
    const char *out_arg = NULL;
    int i;
    int bad;
    time_t now;
    struct tm tmv;
    char stamp[32];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fix") == 0) g_fix = 1;
        else if (strcmp(argv[i], "--quiet") == 0) g_quiet = 1;
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_arg = argv[++i];
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("validate %s\n", VERSION);
            return 0;
        } else {
            fprintf(stderr, "validate: unknown argument '%s'\n", argv[i]);
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (out_arg != NULL) {
        snprintf(report_path, sizeof(report_path), "%s", out_arg);
    } else {
        now = time(NULL);
        timestamp(now, &tmv, "%Y%m%d_%H%M%S", stamp, sizeof(stamp));
        snprintf(report_path, sizeof(report_path), "validation%s.txt", stamp);
    }
    g_report = fopen(report_path, "w");
    if (g_report == NULL) {
        fprintf(stderr, "validate: cannot create %s: %s\n", report_path, strerror(errno));
        return 2;
    }

    fprintf(g_report, "validate %s -- ", VERSION);
    now = time(NULL);
    timestamp(now, &tmv, "%Y-%m-%d %H:%M:%S", stamp, sizeof(stamp));
    fprintf(g_report, "%s\nmode: report%s\n\n", stamp, g_fix ? " + fix" : "");

    check_config(motif, sizeof(motif));

    /* Resolve the motif so the system.def checks use its real spelling. */
    {
        static const char *const sys_bases[] = { "" };
        Resolved r;

        resolve(motif, sys_bases, 1, &r);
        if (r.status == RES_OK) {
            g_count.ok++;
            fprintf(g_report, "  motif = %s [OK]\n", motif);
        } else if (r.status == RES_MISSING) {
            g_count.missing++;
            fprintf(g_report, "  motif = %s [NOT FOUND]\n", motif);
        } else {
            char rewrite[PATH_MAX];

            relative_to(&r, rewrite, sizeof(rewrite));
            if (r.status == RES_SEP) g_count.backslash++;
            else g_count.case_mismatch++;
            fprintf(g_report, "  motif = %s [%s] -> %s\n", motif,
                    r.status == RES_SEP ? "BACKSLASH" : "CASE", rewrite);
            snprintf(motif, sizeof(motif), "%s", rewrite);
        }
    }
    {
        char *slash = strrchr(motif, '/');
        if (slash != NULL) {
            size_t n = (size_t)(slash - motif + 1);
            memcpy(motif_dir, motif, n);
            motif_dir[n] = '\0';
        }
    }
    fprintf(g_report, "  motif directory = %s\n\n", motif_dir);

    printf("=== [system.def] ===\n");
    if (is_file(motif)) {
        const char *bases[5];

        /*
         * The font search order system.def itself documents: the system
         * directory, the program directory, data/, then font/.
         */
        bases[0] = "";
        bases[1] = motif_dir;
        bases[2] = "data/";
        bases[3] = "font/";
        bases[4] = NULL;
        {
            KeySet keys;
            keys.keys = k_system_keys;
            keys.count = (int)(sizeof(k_system_keys) / sizeof(k_system_keys[0]));
            validate_file(motif, bases, 4, &keys, "system.def");
        }
        /* The fight definition named by system.def has its own [Files]. */
        {
            char *content = read_file(motif, NULL);
            char fight[PATH_MAX] = "";

            if (content != NULL) {
                char *line, *next;
                for (line = content; line != NULL && *line != '\0'; line = next) {
                    char code[PATH_MAX * 2];
                    char key[128], value[PATH_MAX];

                    next = strchr(line, '\n');
                    if (next != NULL) {
                        *next = '\0';
                        next++;
                    }
                    snprintf(code, sizeof(code), "%s", line);
                    strip_comment(code);
                    if (parse_assignment(code, key, sizeof(key), value, sizeof(value)) &&
                        strcmp(key, "fight") == 0) {
                        snprintf(fight, sizeof(fight), "%s", value);
                    }
                }
                free(content);
            }
            if (fight[0] != '\0') {
                Resolved r;

                resolve(fight, bases, 3, &r);
                if (r.status == RES_MISSING) {
                    g_count.missing++;
                    fprintf(g_report, "  fight = %s [NOT FOUND]\n", fight);
                } else {
                    char fdir[PATH_MAX] = "";
                    char *slash = strrchr(r.matched, '/');

                    if (slash != NULL) {
                        size_t n = (size_t)(slash - r.matched + 1);
                        memcpy(fdir, r.matched, n);
                        fdir[n] = '\0';
                    }
                    if (r.status == RES_OK) {
                        g_count.ok++;
                        fprintf(g_report, "  fight = %s [OK]\n", fight);
                    } else {
                        if (r.status == RES_SEP) g_count.backslash++;
                        else g_count.case_mismatch++;
                        fprintf(g_report, "  fight = %s [%s] -> %s\n", fight,
                                r.status == RES_SEP ? "BACKSLASH" : "CASE", r.matched);
                    }
                    {
                        const char *fbases[5];
                        KeySet keys;

                        fbases[0] = "";
                        fbases[1] = fdir;
                        fbases[2] = "data/";
                        fbases[3] = "font/";
                        fbases[4] = NULL;
                        keys.keys = k_fight_keys;
                        keys.count = (int)(sizeof(k_fight_keys) / sizeof(k_fight_keys[0]));
                        validate_file(r.matched, fbases, 4, &keys, "fight.def");
                    }
                }
            }
        }
    } else {
        g_count.missing++;
        fprintf(g_report, "system.def %s [NOT FOUND]\n", motif);
    }

    /* select.def lives next to system.def (or in data/). */
    {
        char candidate[PATH_MAX + 16];

        snprintf(candidate, sizeof(candidate), "%sselect.def", motif_dir);
        if (is_file(candidate)) snprintf(select_path, sizeof(select_path), "%.*s",
                                          (int)(sizeof(select_path) - 1), candidate);
        else if (is_file("data/select.def")) snprintf(select_path, sizeof(select_path), "data/select.def");
    }

    printf("=== [select.def] ===\n");
    if (select_path[0] == '\0') {
        g_count.missing++;
        printf("  select.def [NOT FOUND]\n");
    } else {
        fprintf(g_report, "select.def: %s\n", select_path);
        check_select(select_path);
        if (g_fix) fix_select(select_path);
    }

    printf("=== [character defs] ===\n");
    if (select_path[0] != '\0') check_character_defs(select_path);

    bad = (int)(g_count.case_mismatch + g_count.backslash + g_count.missing);
    fprintf(g_report, "\n=== summary ===\n");
    fprintf(g_report, "  ok         %u\n", g_count.ok);
    fprintf(g_report, "  case       %u  (on disk with different case)\n", g_count.case_mismatch);
    fprintf(g_report, "  backslash  %u  (Windows path separators)\n", g_count.backslash);
    fprintf(g_report, "  missing    %u\n", g_count.missing);
    if (g_fix) fprintf(g_report, "  fixed      %u  (files rewritten, .bak backups made)\n", g_count.fixed);
    fclose(g_report);

    printf("validate: %u ok, %u case, %u backslash, %u missing -> %s\n",
           g_count.ok, g_count.case_mismatch, g_count.backslash, g_count.missing, report_path);
    if (g_count.fixed > 0) printf("validate: %u value(s) rewritten.\n", g_count.fixed);
    if (bad == 0) printf("validate: all asset paths resolve on this filesystem.\n");
    else if (!g_fix) printf("validate: re-run with --fix to rewrite the def files.\n");
    return bad == 0 ? 0 : 1;
}
