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
 * The two platform primitives the default's publication rests on.
 *
 * Both are port additions, and both are here rather than in a platform file
 * because configembed.c is compiled by every target (Windows, Linux x64/arm64,
 * Android, Vita) and the extraction happens before anything else exists.
 */
#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#define ConfigEmbedded_Pid()     ((unsigned long)_getpid())
#else
#include <unistd.h>
#define ConfigEmbedded_Pid()     ((unsigned long)getpid())
#endif

/*
 * Publishes tmp at path without ever replacing what is already there.
 *
 * Returns 0 on success, -1 when path already exists (the winner's file is left
 * untouched) or on any other failure. rename() is deliberately NOT used: it
 * would silently replace a file created between the absence check and this
 * call, which on Linux and Android is exactly the player's own config.
 *
 * Windows has no link(), and MinGW-w64's io.h declares neither link nor _link,
 * so the Win32 call is used directly. CreateHardLinkA is the same primitive and
 * fails with ERROR_ALREADY_EXISTS without touching the existing file; both the
 * Windows targets here run on NTFS, which supports it.
 */
static int ConfigEmbedded_Publish(const char *tmp, const char *path)
{
#if defined(_WIN32)
    if (CreateHardLinkA(path, tmp, NULL) == 0) {
        return -1;
    }
    return 0;
#else
    if (link(tmp, path) != 0) {
        return -1;
    }
    return 0;
#endif
}

/*
 * Port fix: an interrupted write must not look like a finished one, and a write
 * that loses a race must not replace the winner.
 *
 * The extractor used to open the destination directly, so a short write or a
 * failed close left a truncated or empty file there. The next start found that
 * file, took it for the player's own and skipped extraction for good -- a
 * transient disk-full or a kill during start-up turned into a broken instal-
 * lation that only deleting the file could repair.
 *
 * The default is now written to a temporary beside the destination and
 * published only after fwrite and fclose both succeeded, so a partial file never
 * survives under the real name. Publishing uses link()+unlink() rather than
 * rename(), because rename() would *replace* a file that appeared after the
 * absence check -- on Linux and Android a second instance, or the player's own
 * editor, could have created the very file this is supposed to leave alone.
 * link() fails with EEXIST in that case and the temporary is discarded.
 *
 * The temporary's name carries the process id, so two instances starting at
 * once write different files instead of interleaving into one. A leftover from
 * a killed process is overwritten by the next start with the same pid, and its
 * content could only have been a partial default that is never linked into
 * place.
 */
static void ConfigEmbedded_Write(const char *path, const char *data, size_t size)
{
    FILE *file;
    size_t written;
    char tmp[512];
    int tmplen;
    int ok;
    int published;

    file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return;             /* the tree's own file wins */
    }
    /*
     * The temporary lives in the same directory as the destination, so it is on
     * the same filesystem and link() cannot fail with EXDEV. data/ exists by
     * now in every target that gets this far; a path too long to name a sibling
     * is refused rather than silently using the plain name, which would lose
     * both guarantees this exists for.
     */
    tmplen = snprintf(tmp, sizeof(tmp), "%s.%lu.tmp", path,
                      (unsigned long)ConfigEmbedded_Pid());
    if (tmplen < 0 || tmplen >= (int)sizeof(tmp)) {
        Log_Printf("config: path too long to write safely: %s\n", path);
        return;
    }

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
    /*
     * link() then remove(), not rename(): the destination must not be replaced
     * if something created it while this was being written.
     *
     * The temporary is removed whether or not publication succeeded -- on
     * success it is a second name for the file now at path, on failure it is
     * the only copy of a default nobody wants. Both are unlink()s of a file
     * that is never the destination.
     */
    published = (ConfigEmbedded_Publish(tmp, path) == 0);
    remove(tmp);
    if (!published) {
        Log_Printf("config: %s appeared while writing; kept it\n", path);
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
