/* Port addition: native parameter adapter for the ZSS front-end. The existing
 * CNS parameter compilers can consume these values without an INI text parse.
 * Legacy Ini layout and parsing behavior are unchanged. */
#include "mugen.h"
#include "scriptparams.h"
#include <limits.h>

static int valid_key(const char *s)
{
    unsigned char c;
    if (s == NULL || *s == '\0') return 0;
    for (; *s; s++) {
        c = (unsigned char)*s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.')) return 0;
    }
    return 1;
}
Ini *ScriptParams_ToIni(const ScriptParam *pairs, size_t count, const char **error)
{
    Ini *ini;
    size_t i, j, key, value, bytes = 14;
    char *out;
    const char *why = "Invalid script parameters";
    if (error != NULL) *error = NULL;
    if ((pairs == NULL && count != 0) || count > INT_MAX - 1u) goto invalid;
    for (i = 0; i < count; i++) {
        if (!valid_key(pairs[i].key) || pairs[i].value == NULL || pairs[i].line < 1) goto invalid;
        for (j = 0; j < i; j++) {
            if (_stricmp(pairs[i].key, pairs[j].key) == 0) {
                why = "Duplicate script parameter"; goto invalid;
            }
        }
        key = strlen(pairs[i].key); value = strlen(pairs[i].value);
        if (key > INT_MAX - 2u || value > INT_MAX - 2u - key ||
            bytes > INT_MAX - (key + value + 2)) goto invalid;
        bytes += key + value + 2;
    }
    /* Counts also bound pointer-array multiplication on 32-bit hosts. */
    if (count + 1 > (size_t)-1 / sizeof(char *)) goto invalid;
    ini = calloc(1, sizeof(*ini));
    if (ini == NULL) goto nomem;
    ini->refs = calloc(1, sizeof(*ini->refs));
    if (ini->refs == NULL) { free(ini); goto nomem; }
    *ini->refs = 1;
    ini->text = calloc(1, bytes);
    ini->lines = calloc(count + 1, sizeof(*ini->lines));
    ini->group_lines = calloc(1, sizeof(*ini->group_lines));
    ini->src = calloc(1, sizeof(*ini->src));
    if (ini->text == NULL || ini->lines == NULL || ini->group_lines == NULL || ini->src == NULL) goto cleanup;
    ini->src->line_map = calloc(count + 1, sizeof(*ini->src->line_map));
    if (ini->src->line_map == NULL) goto cleanup;
    out = ini->text;
    memcpy(out, "[zss]", 6); ini->lines[0] = out; out += 6;
    for (i = 0; i < count; i++) {
        ini->lines[i + 1] = out;
        key = strlen(pairs[i].key); value = strlen(pairs[i].value);
        for (j = 0; j < key; j++) {
            unsigned char c = (unsigned char)pairs[i].key[j];
            *out++ = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        }
        *out++ = '=';
        memcpy(out, pairs[i].value, value); out += value;
        *out++ = '\0';
        ini->src->line_map[i + 1] = pairs[i].line - 1;
    }
    memcpy(out, "[TPEnd]", 8);
    ini->line_count = (int)count + 1; ini->group_count = 1;
    ini->text_size = (long)bytes;
    Ini_SelectGroupIndex(ini, 0);
    return ini;
cleanup:
    Ini_Free(ini);
nomem:
    why = "Out of memory compiling script parameters";
invalid:
    if (error != NULL) *error = why;
    return NULL;
}
