/*
 * Port addition: older game packs omit the Lua helpers used by this engine.
 * When data/vector.lua is absent, restore missing scripts from the embedded
 * ZIP before Lua initializes. Existing scripts belong to the game and stay.
 *
 * The resource generator emits a small ZIP with ordinary local headers (no
 * ZIP64 or data descriptors). Read it in memory with the existing zlib, so
 * startup needs neither an external unzip program nor a temporary ZIP file.
 */
#include "luascripts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <zlib.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "../res/lua_script_zip.h"

static unsigned long zip_u16(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

static unsigned long zip_u32(const unsigned char *p)
{
    return zip_u16(p) | (zip_u16(p + 2) << 16);
}

static int script_name_ok(const char *name)
{
    size_t i, n = strlen(name);

    if (n < 5 || name[0] == '.' || strcmp(name + n - 4, ".lua") != 0)
        return 0;
    for (i = 0; i < n; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

int LuaScripts_Ensure(void)
{
    struct stat st;
    size_t pos = 0, size = sizeof(g_luaScriptZip);

    if (stat("data/vector.lua", &st) == 0)
        return 0;
    if (errno != ENOENT)
        return -1;
#ifdef _WIN32
    if (_mkdir("data") != 0 && errno != EEXIST)
#else
    if (mkdir("data", 0777) != 0 && errno != EEXIST)
#endif
        return -1;

    while (pos + 30 <= size && zip_u32(g_luaScriptZip + pos) == 0x04034b50UL) {
        const unsigned char *entry = g_luaScriptZip + pos;
        unsigned long flags = zip_u16(entry + 6), method = zip_u16(entry + 8);
        unsigned long compressed = zip_u32(entry + 18), length = zip_u32(entry + 22);
        size_t name_length = zip_u16(entry + 26), extra = zip_u16(entry + 28);
        size_t offset = pos + 30 + name_length + extra;
        char name[128], path[144];
        unsigned char *bytes;
        FILE *file;
        int ok;

        if (flags != 0 || name_length >= sizeof(name) || offset > size ||
            compressed > size - offset || length > 1024 * 1024)
            return -1;
        memcpy(name, entry + 30, name_length);
        name[name_length] = '\0';
        if (strlen(name) != name_length || !script_name_ok(name))
            return -1;
        snprintf(path, sizeof(path), "data/%s", name);
        pos = offset + compressed;
        if (stat(path, &st) == 0)
            continue;
        if (errno != ENOENT)
            return -1;
        bytes = malloc(length + 1);
        if (bytes == NULL)
            return -1;
        ok = 0;
        if (method == 0 && compressed == length) {
            memcpy(bytes, g_luaScriptZip + offset, length);
            ok = 1;
        } else if (method == 8) {
            z_stream stream;
            memset(&stream, 0, sizeof(stream));
            stream.next_in = (Bytef *)(g_luaScriptZip + offset);
            stream.avail_in = (uInt)compressed;
            stream.next_out = bytes;
            stream.avail_out = (uInt)length + 1;
            if (inflateInit2(&stream, -MAX_WBITS) == Z_OK) {
                ok = inflate(&stream, Z_FINISH) == Z_STREAM_END &&
                     stream.total_out == length && stream.total_in == compressed;
                inflateEnd(&stream);
            }
        }
        if (!ok || crc32(0, bytes, (uInt)length) != zip_u32(entry + 14)) {
            free(bytes);
            return -1;
        }
        file = fopen(path, "wb");
        if (file == NULL) {
            free(bytes);
            return -1;
        }
        ok = fwrite(bytes, 1, length, file) == length;
        if (fclose(file) != 0)
            ok = 0;
        free(bytes);
        if (!ok) {
            remove(path);
            return -1;
        }
    }
    /* vector.lua is packed last: a partial restoration will retry next boot. */
    return stat("data/vector.lua", &st) == 0 ? 1 : -1;
}
