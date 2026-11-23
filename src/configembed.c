/*
 * Port addition: writes the embedded configuration defaults into the game tree.
 *
 * There is no original counterpart; the original binary always read whatever
 * data/mugen.cfg the tree carried. This project ships res/configs/ -- the base
 * file and one overlay per platform -- and compiles both into the executable,
 * so an install/ tree, an APK or a VPK needs no configuration copied into it by
 * hand.
 *
 * Only a missing file is written. An existing one is the player's: the options
 * screen rewrites [Options], the volumes and the key groups in place, so
 * replacing it would discard saved settings. Failure is not fatal either: the
 * engine then reports the same "Can't open config file" it always did.
 *
 * The bytes compiled in are written as-is; the generators normalise line
 * endings to LF, so every target extracts the same text.
 */
#include <stdio.h>
#include <string.h>

#include "mugen.h"
#include "configembed.h"
#include "config_data.h"        /* generated: g_config_mugen_* */

static void ConfigEmbedded_Write(const char *path, const char *data, size_t size)
{
    FILE *file;
    size_t written;

    file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return;             /* the tree's own file wins */
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        Log_Printf("config: could not create %s\n", path);
        return;
    }
    written = fwrite(data, 1, size, file);
    if (fclose(file) != 0 || written != size) {
        Log_Printf("config: could not write %s\n", path);
        return;
    }
    Log_Printf("config: wrote %s from the embedded default\n", path);
}

void Config_Embedded_Extract(void)
{
    ConfigEmbedded_Write("data/mugen.cfg", g_config_mugen_cfg,
                         sizeof(g_config_mugen_cfg) - 1);
#ifdef CONFIG_PORT_EMBEDDED
    ConfigEmbedded_Write(CONFIG_PORT_PATH, CONFIG_PORT_EMBEDDED,
                         sizeof(CONFIG_PORT_EMBEDDED) - 1);
#endif
}
