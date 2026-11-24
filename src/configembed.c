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

/*
 * Port fix: an interrupted write must not look like a finished one.
 *
 * The extractor used to open the destination directly, so a short write or a
 * failed close left a truncated or empty file there. The next start found that
 * file, took it for the player's own and skipped extraction for good -- a
 * transient disk-full or a kill during start-up turned into a broken instal-
 * lation that only deleting the file could repair.
 *
 * The default is now written beside the destination and renamed into place
 * only after fwrite and fclose both succeeded. rename() over an existing file
 * is atomic on every target here, and it is reached only when the destination
 * was absent a moment earlier, so a file that appeared in between (a second
 * instance starting at the same time) is still not replaced: the tmp file is
 * discarded instead. A partial file never survives under the real name.
 */
static void ConfigEmbedded_Write(const char *path, const char *data, size_t size)
{
    FILE *file;
    size_t written;
    char tmp[512];
    size_t pathlen;
    int ok;

    file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return;             /* the tree's own file wins */
    }
    /*
     * The temporary lives in the same directory as the destination, so the
     * rename cannot cross a filesystem boundary. data/ exists by now in every
     * target that gets this far; a path too long to name a sibling is refused
     * rather than silently using the plain name (which would lose the
     * atomicity this exists for).
     */
    pathlen = strlen(path);
    if (pathlen + 5 > sizeof(tmp)) {
        Log_Printf("config: path too long to write safely: %s\n", path);
        return;
    }
    memcpy(tmp, path, pathlen);
    memcpy(tmp + pathlen, ".tmp", 5);

    file = fopen(tmp, "wb");
    if (file == NULL) {
        Log_Printf("config: could not create %s\n", tmp);
        return;
    }
    written = fwrite(data, 1, size, file);
    ok = (fclose(file) == 0) && (written == size);
    if (!ok) {
        remove(tmp);
        Log_Printf("config: could not write %s\n", path);
        return;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
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
#ifdef CONFIG_PORT_LEGACY
    /*
     * Port fix: an upgraded arm64 tree keeps the overlay it already had.
     * Writing the new default beside the old file would leave two overlays
     * with different values and no way for the player to tell which one the
     * engine reads. The old one is honoured (config.c merges it) and the new
     * name is left absent, which is exactly the pre-upgrade behaviour.
     */
    {
        FILE *legacy = fopen(CONFIG_PORT_LEGACY, "rb");
        if (legacy != NULL) {
            fclose(legacy);
            Log_Printf("config: keeping %s (the previous name of %s)\n",
                       CONFIG_PORT_LEGACY, CONFIG_PORT_PATH);
            return;
        }
    }
#endif
    ConfigEmbedded_Write(CONFIG_PORT_PATH, CONFIG_PORT_EMBEDDED,
                         sizeof(CONFIG_PORT_EMBEDDED) - 1);
#endif
}
