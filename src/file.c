/*
 * File lookup helpers (original module around 0x40C1D0-0x40C3E8, next to the
 * error reporting code).
 */
#include "mugen.h"
#include "sffatlas.h"

int Sff_AtlasSibling(const char *path, char *out, size_t capacity)
{
    const char *file = path, *ext = strrchr(path, '.');
    if (!ext || strcasecmp(ext, ".sff") || strlen(path) + 7 > capacity) return 0;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\' || *p == ':') file = p+1;
    if (!strncasecmp(file, "atlas.", 6)) return 0;
    size_t prefix = (size_t)(file-path);
    memcpy(out, path, prefix);
    memcpy(out+prefix, "atlas.", 6);
    strcpy(out+prefix+6, file);
    return 1;
}

/*
 * Port addition: the original is a Windows program, so it and every pack it
 * loads treat '\' as a directory separator. POSIX does not, and a Linux,
 * Android or Vita build given a .def line such as `cmd = files\freeza.mfg`
 * fails to open a file that is present. Convert the separators before opening
 * on those targets; on Windows this is exactly fopen. Centralised here because
 * a path can arrive from a .def, .cns, .sff entry, config value or the command
 * line, and every one of them then flows through this or Stream_OpenFile.
 */
FILE *Compat_Fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    return fopen(path, mode);
#else
    char buf[0x800];
    size_t i;

    if (strchr(path, '\\') == NULL || strlen(path) >= sizeof(buf)) {
        return fopen(path, mode);
    }
    for (i = 0; path[i] != '\0'; i++) {
        buf[i] = (path[i] == '\\') ? '/' : path[i];
    }
    buf[i] = '\0';
    return fopen(buf, mode);
#endif
}

/* 0x0040C1D0 */
int File_Exists(const char *path)
{
    FILE *fp = Compat_Fopen(path, "rb");

    if (fp != NULL) {
        fclose(fp);
    }
    return fp != NULL;
}

/*
 * 0x0040C200: looks for path under base_dir, the motif directory, "./" and
 * "data/" in that order (base_dir is skipped for absolute paths). On success
 * out receives the full path and dir_out, if given, the directory part.
 *
 * A drive letter is cut off the path and never used, as in the original.
 */
static int find_file(const char *path, char *out, char *dir_out, const char *base_dir, SpriteSet **sprites)
{
    char drive[4];
    const char *prefixes[4];
    char dir[0x200];
    char full[0x200];
    char file[0x1F0];
    size_t path_len, file_len;
    int i;

    if (path[0] != '\0' && path[1] == ':') {
        strncpy(drive, path, 2);
        drive[2] = '\0';
        path += 2;
    } else {
        drive[0] = '\0';
    }
    prefixes[0] = base_dir;
    prefixes[1] = g_game->motif_dir;
    prefixes[2] = "./";
    prefixes[3] = "data/";
    /* Port fix: Path_Split has no capacity arguments. Validate both pieces
     * before calling it, then reject candidates that cannot fit whole. */
    path_len = strlen(path);
    /* Find the final component (both slash styles are accepted). */
    file_len = 0;
    while (file_len < path_len && path[path_len - file_len - 1] != '/' &&
           path[path_len - file_len - 1] != '\\') {
        file_len++;
    }
    if (path_len - file_len >= sizeof(dir) || file_len >= sizeof(file)) {
        Error_Printf("File path too long: %s", path);
        return 0;
    }
    Path_Split(path, dir, file);
    if (dir[0] == '/' || dir[0] == '\\') {
        prefixes[0] = "";
    }
    for (i = 0; i < 4; i++) {
        FILE *fp;
        int len;
        if (prefixes[i] == NULL) {
            continue;
        }
        len = snprintf(full, sizeof(full), "%s%s%s", prefixes[i], dir, file);
        if (len < 0 || (size_t)len >= sizeof(full)) {
            continue;
        }
        char sibling[sizeof(full)+6];
        fp = sprites && Sff_AtlasSibling(full, sibling, sizeof(sibling)) ? Compat_Fopen(sibling, "rb") : NULL;
        if (!fp) fp = Compat_Fopen(full, "rb");
        if (fp != NULL) {
            fclose(fp);
            if (sprites && !(*sprites = Sff_Load(full))) continue;
            if (dir_out != NULL) {
                /* This prefix is bounded by the already checked full path. */
                size_t dir_len = (size_t)len - file_len;
                memcpy(dir_out, full, dir_len);
                dir_out[dir_len] = '\0';
            }
            strcpy(out, full);
            return 1;
        }
    }
    Error_Printf("Can't find file %s", path);
    return 0;
}

int File_Find(const char *path, char *out, char *dir_out, const char *base_dir)
{
    return find_file(path, out, dir_out, base_dir, 0);
}

SpriteSet *Sff_FindAndLoad(const char *path, char *out, const char *base_dir)
{
    SpriteSet *set = NULL;
    find_file(path, out, NULL, base_dir, &set);
    return set;
}

/*
 * 0x0040C360: copies the value of key (at most size - 1 chars) and unquotes
 * it, keeping the closing quote when truncation cut it off. Reports a missing
 * key as an error.
 */
int Ini_ReadString(Ini *ini, const char *key, unsigned int size, char *out)
{
    const char *value = Ini_GetValue(ini, key);
    int quoted = 0;
    int len;

    if (value == NULL) {
        Error_Printf("Cannot find parameter: %s", key);
        return 0;
    }
    len = (int)strlen(value);
    if (len > 0 && value[len - 1] == '"') {
        quoted = 1;
    }
    out[0] = '\0';
    if (size > 1) {
        strncat(out, value, size - 1);
    }
    if (quoted) {
        len = (int)strlen(out);
        if (len > 0) {
            out[len - 1] = '"';
        }
    }
    Str_Unquote(out);
    return 1;
}
