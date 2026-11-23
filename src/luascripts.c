/*
 * Port addition: older game packs omit the Lua helpers used by this engine.
 * When data/vector.lua is absent, restore missing scripts from the embedded
 * table before Lua initializes. Existing scripts belong to the game and stay.
 *
 * res/lua/ is the source of truth; the build embeds it as byte arrays
 * (tools/embed_resources.sh for Make, port/cmake/EmbedResources.cmake for the
 * CMake ports), so startup needs no archive parser and no temporary file.
 */
#include "luascripts.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "lua_scripts_data.h"

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

static int script_write(const char *path, const unsigned char *data, size_t size)
{
    FILE *file;
    size_t written;

    file = fopen(path, "wb");
    if (file == NULL)
        return -1;
    written = fwrite(data, 1, size, file);
    if (fclose(file) != 0 || written != size) {
        remove(path);
        return -1;
    }
    return 0;
}

int LuaScripts_Ensure(void)
{
    struct stat st;
    size_t i, count;
    char path[144];

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

    count = sizeof(g_lua_files) / sizeof(g_lua_files[0]);
    for (i = 0; i < count; ++i) {
        const EmbeddedFile *entry = &g_lua_files[i];

        if (!script_name_ok(entry->name))
            return -1;
        if (snprintf(path, sizeof(path), "data/%s", entry->name) >= (int)sizeof(path))
            return -1;
        if (stat(path, &st) == 0)
            continue;
        if (errno != ENOENT)
            return -1;
        if (script_write(path, entry->data, entry->size) != 0)
            return -1;
    }
    /* vector.lua is embedded last: a partial restoration retries next boot. */
    return stat("data/vector.lua", &st) == 0 ? 1 : -1;
}
