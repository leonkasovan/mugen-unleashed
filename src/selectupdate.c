/*
 * Port addition: automatic select.def discovery, based on updateCharInSelectDef
 * and updateStageInSelectDef in leonkasovan/Ikemen-GO stable/src/main.go.
 * Scan chars/<name>/<name>.def and stage .def files, append missing entries, and
 * keep a .bak. This does not change the original select loader or grid layout.
 * Unlike the reference updater, preserve the existing bytes and commit both
 * sections together, only after discovery and all file writes have succeeded.
 */
#include "mugen.h"
#include "selectupdate.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define UPDATE_PATH 512 /* Existing select loader's path buffers. */
#define UPDATE_MAX_FILE (16 * 1024 * 1024)

typedef struct UpdateList {
    char **items;
    size_t count, capacity;
} UpdateList;

static int auto_characters, auto_stages;

static int space(unsigned char c) { return c == ' ' || c == '\t' || c == '\r'; }
static void trim(const char **p, const char **end)
{
    while (*p < *end && space((unsigned char)**p)) ++*p;
    while (*end > *p && space((unsigned char)(*end)[-1])) --*end;
}
static void list_free(UpdateList *list)
{
    size_t i;
    for (i = 0; i < list->count; ++i) free(list->items[i]);
    free(list->items);
}
static int list_add(UpdateList *list, const char *text)
{
    char *copy;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 16;
        char **grown;
        if (capacity < list->capacity || capacity > SIZE_MAX / sizeof(*grown)) return 0;
        grown = realloc(list->items, capacity * sizeof(*grown));
        if (grown == NULL) return 0;
        list->items = grown; list->capacity = capacity;
    }
    copy = malloc(strlen(text) + 1);
    if (copy == NULL) return 0;
    strcpy(copy, text); list->items[list->count++] = copy;
    return 1;
}
static int list_has(const UpdateList *list, const char *text)
{
    size_t i;
    for (i = 0; i < list->count; ++i)
        if (strcmp(list->items[i], text) == 0) return 1;
    return 0;
}
static int compare_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static int suffix_def(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && _stricmp(name + n - 4, ".def") == 0;
}
static int safe_name(const char *name)
{
    size_t n = strlen(name);
    return n > 0 && !space((unsigned char)name[0]) &&
        !space((unsigned char)name[n - 1]) && strpbrk(name, "\r\n,;\"[]\\:") == NULL;
}
/* Sorted and nonrecursive, like os.ReadDir / filepath.Glob in the reference.
 * Missing content directories are normal; other discovery failures abort. */
static int list_directory(const char *path, int directories, UpdateList *list)
{
    DIR *dir = opendir(path);
    struct dirent *de;
    int ok = 1, saved;
    if (dir == NULL) return errno == ENOENT;
    for (;;) {
        char full[UPDATE_PATH];
        struct stat st;
        int n;
        errno = 0;
        de = readdir(dir);
        if (de == NULL) { if (errno != 0) ok = 0; break; }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0 ||
            !safe_name(de->d_name)) continue;
        n = snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(full)) continue;
        if (stat(full, &st) != 0) { if (errno == ENOENT) continue; ok = 0; break; }
        if (directories ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode) && suffix_def(de->d_name)) {
            if (!list_add(list, de->d_name)) { errno = ENOMEM; ok = 0; break; }
        }
    }
    saved = errno;
    if (closedir(dir) != 0 && ok) return 0;
    errno = saved;
    if (ok && list->count > 1) qsort(list->items, list->count, sizeof(*list->items), compare_names);
    return ok;
}
static char *read_file(const char *path, size_t *length)
{
    FILE *fp = fopen(path, "rb");
    char *data = NULL;
    long size;
    int saved;
    if (fp == NULL) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
        size > UPDATE_MAX_FILE || fseek(fp, 0, SEEK_SET) != 0) goto done;
    data = malloc((size_t)size + 1);
    if (data == NULL) { errno = ENOMEM; goto done; }
    if (fread(data, 1, (size_t)size, fp) != (size_t)size || ferror(fp) ||
        memchr(data, '\0', (size_t)size) != NULL) {
        free(data); data = NULL; errno = EIO; goto done;
    }
    data[size] = '\0'; *length = (size_t)size;
done:
    saved = errno;
    if (fclose(fp) != 0) { free(data); data = NULL; saved = errno; }
    errno = saved;
    return data;
}
/* Header kind: 0 = ordinary line, 1/2 = target sections, 3 = other header. */
static int header(const char *p, const char *end)
{
    const char *close;
    trim(&p, &end);
    if (end - p >= 3 && memcmp(p, "\xef\xbb\xbf", 3) == 0) p += 3;
    trim(&p, &end);
    if (p == end || *p != '[' || (close = memchr(p, ']', (size_t)(end - p))) == NULL) return 0;
    ++p; end = close; trim(&p, &end);
    if (end - p == 10 && _strnicmp(p, "Characters", 10) == 0) return 1;
    if (end - p == 11 && _strnicmp(p, "ExtraStages", 11) == 0) return 2;
    return 3;
}
/* First comma-separated field, with comments/whitespace/quotes removed. */
static int field(const char *p, const char *end, char out[UPDATE_PATH])
{
    const char *q;
    int quoted = 0;
    trim(&p, &end);
    for (q = p; q < end; ++q) {
        if (*q == '"') quoted = !quoted;
        if (!quoted && (*q == ',' || *q == ';')) break;
    }
    end = q; trim(&p, &end);
    if (end - p >= 2 && *p == '"' && end[-1] == '"') { ++p; --end; }
    if (end == p || (size_t)(end - p) >= UPDATE_PATH) return 0;
    memcpy(out, p, (size_t)(end - p)); out[end - p] = '\0';
    return 1;
}
static void normalize(char *text)
{
    char *p = text, *out = text;
    while (p[0] == '.' && (p[1] == '/' || p[1] == '\\')) p += 2;
    while (*p) {
        if ((out == text || out[-1] == '/') && p[0] == '.' &&
            (p[1] == '/' || p[1] == '\\')) { p += 2; continue; }
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') c = '/';
        if (c == '/' && out > text && out[-1] == '/') continue;
        *out++ = (char)tolower(c);
    }
    *out = '\0';
}
/* Match the loose character resolver: kfm, kfm/kfm and kfm/kfm.def share a key. */
static int character_key(const char *name, char out[UPDATE_PATH])
{
    int n;
    if (strchr(name, '/') == NULL && strchr(name, '\\') == NULL)
        n = snprintf(out, UPDATE_PATH, "chars/%s/%s%s", name, name, suffix_def(name) ? "" : ".def");
    else
        n = snprintf(out, UPDATE_PATH, "%s%s%s", name[0] == '/' || name[0] == '\\' ||
                     strchr(name, ':') != NULL ? "" : "chars/", name, suffix_def(name) ? "" : ".def");
    if (n < 0 || n >= UPDATE_PATH) return 0;
    normalize(out); return 1;
}
static int valid_definition(const char *path, int character)
{
    size_t length, pos = 0;
    char *data = read_file(path, &length);
    int info = 0, required = 0;
    if (data == NULL) return -1;
    while (pos < length) {
        const char *p = data + pos, *end = memchr(p, '\n', length - pos), *close;
        if (end == NULL) end = data + length;
        pos = (size_t)(end - data) + (end < data + length);
        trim(&p, &end);
        if (end - p >= 3 && memcmp(p, "\xef\xbb\xbf", 3) == 0) p += 3;
        trim(&p, &end);
        if (p == end || *p != '[' || (close = memchr(p, ']', (size_t)(end - p))) == NULL) continue;
        ++p; end = close; trim(&p, &end);
        if (end - p == 4 && _strnicmp(p, "Info", 4) == 0) info = 1;
        if (character ? end - p == 5 && _strnicmp(p, "Files", 5) == 0 :
                        end - p == 9 && _strnicmp(p, "StageInfo", 9) == 0) required = 1;
    }
    free(data); return info && required;
}
static int discover(int characters, UpdateList *existing, UpdateList *additions)
{
    UpdateList names = {0};
    size_t i;
    int ok = 0;
    if (!list_directory(characters ? "chars" : "stages", characters, &names)) goto done;
    for (i = 0; i < names.count; ++i) {
        char path[UPDATE_PATH], token[UPDATE_PATH], key[UPDATE_PATH], line[UPDATE_PATH + 16];
        int n, valid;
        if (characters) {
            UpdateList defs = {0};
            size_t j;
            char expected[UPDATE_PATH];
            if (_stricmp(names.items[i], "randomselect") == 0) continue;
            n = snprintf(path, sizeof(path), "chars/%s", names.items[i]);
            if (n < 0 || (size_t)n >= sizeof(path)) continue;
            if (!list_directory(path, 0, &defs)) { list_free(&defs); goto done; }
            n = snprintf(expected, sizeof(expected), "%s.def", names.items[i]);
            if (n < 0 || (size_t)n >= sizeof(expected)) { list_free(&defs); continue; }
            for (j = 0; j < defs.count; ++j) if (strcmp(defs.items[j], expected) == 0) break;
            if (j == defs.count)
                for (j = 0; j < defs.count; ++j) if (_stricmp(defs.items[j], expected) == 0) break;
            if (j == defs.count) { list_free(&defs); continue; }
            /* Preserve actual case on case-sensitive filesystems. */
            n = snprintf(token, sizeof(token), "%s/%s", names.items[i], defs.items[j]);
            if (strcmp(defs.items[j], expected) == 0)
                snprintf(token, sizeof(token), "%s", names.items[i]);
            n = snprintf(path, sizeof(path), "chars/%s/%s", names.items[i], defs.items[j]);
            list_free(&defs);
            if (n < 0 || (size_t)n >= sizeof(path) || !character_key(token, key)) continue;
        } else {
            n = snprintf(path, sizeof(path), "stages/%s", names.items[i]);
            if (n < 0 || (size_t)n >= sizeof(path)) continue;
            strcpy(token, path); strcpy(key, path); normalize(key);
        }
        if (list_has(existing, key)) continue;
        valid = valid_definition(path, characters);
        if (valid < 0) goto done;
        if (!valid) continue;
        snprintf(line, sizeof(line), "%s%s", token, characters ? ", random" : "");
        if (!list_add(additions, line) || !list_add(existing, key)) { errno = ENOMEM; goto done; }
    }
    ok = 1;
done:
    list_free(&names); return ok;
}
static FILE *create_temp(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
    FILE *fp;
    if (fd < 0) return NULL;
    fp = fdopen(fd, "wb");
    if (fp == NULL) { int saved = errno; close(fd); remove(path); errno = saved; }
    return fp;
}
static int write_bytes(FILE *fp, const char *p, size_t n)
{
    return n == 0 || fwrite(p, 1, n, fp) == n;
}
static int finish_file(FILE *fp)
{
    int ok = fflush(fp) == 0 && !ferror(fp);
    if (fclose(fp) != 0) ok = 0;
    return ok;
}
static int replace_file(const char *from, const char *to)
{
#ifdef _WIN32
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) return 1;
    errno = EIO; return 0;
#else
    return rename(from, to) == 0;
#endif
}
static int write_additions(FILE *fp, const UpdateList *list, const char *eol,
                           const char *text, size_t pos, int missing, const char *group)
{
    size_t i;
    if (list->count == 0) return 1;
    if (pos > 0 && text[pos - 1] != '\n' && fputs(eol, fp) == EOF) return 0;
    if (missing && (fprintf(fp, "[%s]%s", group, eol) < 0)) return 0;
    for (i = 0; i < list->count; ++i)
        if (fprintf(fp, "%s%s", list->items[i], eol) < 0) return 0;
    return 1;
}
int Select_UpdateSelectDef(const char *path, int characters, int stages)
{
    UpdateList existing[2] = {{0}, {0}}, additions[2] = {{0}, {0}};
    const char *eol = "\n";
    size_t length = 0, pos = 0, insert[2] = {SIZE_MAX, SIZE_MAX}, offset = 0, i;
    int section = 0, seen[2] = {0}, result = -1, saved = 0, created = 0, created_backup = 0;
    char *text = NULL, *check = NULL;
    char update[UPDATE_PATH + 16], backup[UPDATE_PATH + 16], backup_update[UPDATE_PATH + 32];
    FILE *fp = NULL;
    struct stat st;
    if (!characters && !stages) return 0;
    if (path == NULL || path[0] == '\0') { errno = EINVAL; return -1; }
    if (strlen(path) >= UPDATE_PATH) { errno = ENAMETOOLONG; goto done; }
    snprintf(update, sizeof(update), "%s.update", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    snprintf(backup_update, sizeof(backup_update), "%s.bak.update", path);
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) goto done;
    text = read_file(path, &length);
    if (text == NULL) goto done;
    {
        const char *lf = memchr(text, '\n', length);
        if (lf != NULL && lf > text && lf[-1] == '\r') eol = "\r\n";
    }
    while (pos < length) {
        const char *p = text + pos, *end = memchr(p, '\n', length - pos);
        int h;
        char token[UPDATE_PATH], key[UPDATE_PATH];
        if (end == NULL) end = text + length;
        h = header(p, end);
        if (h != 0) {
            if (section > 0 && section <= 2 && insert[section - 1] == SIZE_MAX)
                insert[section - 1] = pos;
            section = h;
            if (h <= 2) seen[h - 1] = 1;
        } else if (section > 0 && section <= 2 && field(p, end, token)) {
            if (section == 1) {
                if (_stricmp(token, "randomselect") != 0 && character_key(token, key) &&
                    !list_add(&existing[0], key)) { errno = ENOMEM; goto done; }
            } else {
                normalize(token);
                if (!list_add(&existing[1], token)) { errno = ENOMEM; goto done; }
            }
        }
        pos = (size_t)(end - text) + (end < text + length);
    }
    for (i = 0; i < 2; ++i) if (insert[i] == SIZE_MAX) insert[i] = length;
    if ((characters && !discover(1, &existing[0], &additions[0])) ||
        (stages && !discover(0, &existing[1], &additions[1]))) goto done;
    if (additions[0].count == 0 && additions[1].count == 0) { result = 0; goto done; }
    fp = create_temp(update);
    if (fp == NULL) goto done;
    created = 1;
    /* Insert in source order, independent of section order or presence. */
    for (i = 0; i < 2; ++i) {
        size_t first = insert[0] < insert[1] ||
            (insert[0] == insert[1] && (seen[0] || !seen[1])) ? 0 : 1;
        size_t which = i == 0 ? first : 1 - first;
        size_t at = insert[which];
        if (!write_bytes(fp, text + offset, at - offset) ||
            !write_additions(fp, &additions[which], eol, text, at, !seen[which],
                             which == 0 ? "Characters" : "ExtraStages")) goto done;
        offset = at;
    }
    if (!write_bytes(fp, text + offset, length - offset)) goto done;
    if (!finish_file(fp)) { fp = NULL; goto done; }
    fp = NULL;
    if (chmod(update, st.st_mode & 0777) != 0) goto done;
    /* A failed backup or final rename leaves the original in place. */
    fp = create_temp(backup_update);
    if (fp == NULL) goto done;
    created_backup = 1;
    if (!write_bytes(fp, text, length)) goto done;
    if (!finish_file(fp)) { fp = NULL; goto done; }
    fp = NULL;
    if (chmod(backup_update, st.st_mode & 0777) != 0) goto done;
    {
        size_t check_length;
        check = read_file(path, &check_length);
        if (check == NULL || check_length != length || memcmp(check, text, length) != 0) {
            errno = EBUSY; goto done;
        }
    }
    if (!replace_file(backup_update, backup)) goto done;
    created_backup = 0;
    if (!replace_file(update, path)) goto done;
    created = 0;
    result = (int)(additions[0].count + additions[1].count);
    for (i = 0; i < 2; ++i) {
        size_t j;
        for (j = 0; j < additions[i].count; ++j)
            Log_Printf("select-update: added %s: %s\n", i == 0 ? "character" : "stage", additions[i].items[j]);
    }
    Log_Printf("select-update: %s: %zu characters, %zu stages added; backup: %s\n",
               path, additions[0].count, additions[1].count, backup);
done:
    saved = errno;
    if (fp != NULL) fclose(fp);
    if (created) remove(update);
    if (created_backup) remove(backup_update);
    free(check); free(text);
    for (i = 0; i < 2; ++i) { list_free(&existing[i]); list_free(&additions[i]); }
    if (result < 0) Log_Printf("select-update: could not update %s: %s; using existing roster\n",
                              path, strerror(saved ? saved : EIO));
    errno = saved;
    return result;
}
int Select_UpdateCharInSelectDef(const char *path) { return Select_UpdateSelectDef(path, 1, 0); }
int Select_UpdateStageInSelectDef(const char *path) { return Select_UpdateSelectDef(path, 0, 1); }
void SelectUpdate_Configure(int characters, int stages)
{
    auto_characters = characters != 0; auto_stages = stages != 0;
}
int SelectUpdate_Enabled(void) { return auto_characters || auto_stages; }
void SelectUpdate_Auto(const char *path)
{
    Select_UpdateSelectDef(path, auto_characters, auto_stages);
}
