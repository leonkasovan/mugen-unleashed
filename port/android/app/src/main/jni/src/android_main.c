// =============================================================================
// Android entry point and asset location, for the mugen-unleashed port.
//
// The engine reads its data through the working directory (mugen.cfg,
// chars/, stages/, data/, font/). On Android the APK's assets are inside the
// package, not on the filesystem, so they must be copied out once before main()
// runs. This does that, then chdir()s there so every existing relative path in
// the engine keeps working untouched.
// =============================================================================
#ifdef __ANDROID__

#include <android/log.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

/*
 * Port fix: this translation unit must NOT see SDL_MAIN_HANDLED.
 *
 * The target defines it for the engine sources, where it is correct (they never
 * provide a main()). But SDL_main.h only renames main() to SDL_main() when
 * SDL_MAIN_HANDLED is undefined, and SDLActivity dlopen()s libmain.so and
 * dlsym()s "SDL_main" by name. With the define in force the exported symbol was
 * plain `main`, so the activity logged
 *
 *     nativeRunMain(): Couldn't find function SDL_main in library libmain.so
 *
 * and the process exited before the engine ran -- on every Android build since
 * SDL_MAIN_HANDLED was added to this target in f97752f. Undefining it here, and
 * only here, is what makes the entry point reachable again.
 */
#ifdef SDL_MAIN_HANDLED
#undef SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <SDL_main.h>

#include "android_platform.h"

#define ALOG_TAG "mugen"

/*
 * Engine global (src/globals.c): the folder picker chose a new tree, so main.c
 * should tear the engine down and run it again. Declared here rather than by
 * including symbols.h, which needs the whole engine type graph -- this is the
 * platform layer, and this is the only engine global it touches.
 */
extern int g_folderReloadRequested;

/* The engine entry point, from src/main.c. That module is compiled for its
 * helpers too (Camera_GetX, Rng_Seed, ...), so its main() is renamed and called
 * from here instead of SDL_main. */
int Mugen_Main(int argc, char *argv[]);

/*
 * Written by the Gradle build; one "rel-path size sha256" record per line.
 * Size + hash let extraction verify rather than trust `stat` (B6).
 */
#define ASSET_MANIFEST "manifest.txt"
/* Generation hash of the payload; re-extract when it differs (upgrade). */
#define ASSET_PAYLOAD  "payload.txt"
#define ASSET_STAMP    "mugen_assets.stamp"

/*
 * Written by SDLActivity.setGameFolder() when the player picks a folder with the
 * "Select Game Folder" button. It sits in the app's external files directory
 * (no permission needed) and holds the absolute path of the game tree. It has to
 * be a file rather than a JNI call because the working directory is resolved
 * here, in C, before any Java code could hand it over.
 */
#define GAME_FOLDER_MARKER "mugen_gamedir.txt"

/* Whether the tree came from the player's picker rather than the default. */
static int g_game_folder_chosen;
/* Whether chosen_game_folder() has read the marker yet. */
static int g_chosen_checked;

/*
 * The folder named by GAME_FOLDER_MARKER, or NULL when there is none.
 * Only an existing directory is accepted: a stale marker must not send the
 * engine to a path that was deleted or unmounted.
 */
static const char *chosen_game_folder(void)
{
    static char path[1024];
    const char *base;
    char marker[1200];
    FILE *f;

    if (g_chosen_checked) {
        return g_game_folder_chosen ? path : NULL;
    }
    g_chosen_checked = 1;

    base = SDL_AndroidGetExternalStoragePath();
    if (base == NULL || base[0] == '\0') {
        return NULL;
    }
    snprintf(marker, sizeof(marker), "%s/%s", base, GAME_FOLDER_MARKER);
    f = fopen(marker, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fgets(path, sizeof(path), f) == NULL) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    {
        size_t n = strlen(path);
        struct stat st;

        while (n > 0 && (path[n - 1] == '\n' || path[n - 1] == '\r')) {
            path[--n] = '\0';
        }
        if (n == 0 || stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            __android_log_print(ANDROID_LOG_WARN, ALOG_TAG,
                                "chosen game folder is missing: %s", path);
            return NULL;
        }
    }
    g_game_folder_chosen = 1;
    return path;
}

/*
 * Re-read the marker from disk.
 *
 * chosen_game_folder() caches its answer because every relative path in the
 * engine asks for it. Switching folders has to invalidate that cache, or the
 * restart would resolve the tree it was just told to leave.
 */
static void chosen_game_folder_forget(void)
{
    g_game_folder_chosen = 0;
    g_chosen_checked = 0;
}

/*
 * Whether a folder the player picked must never be written into.
 *
 * tree_present() above catches a folder that holds a game, but a pack can be
 * missing all three markers and still be full of the player's files -- a
 * half-copied install, or a tree whose mugen.cfg was deleted. Extracting into
 * that would silently replace files they own, so any non-empty folder is
 * treated as theirs. They can still choose an empty folder to bootstrap into.
 */
static int folder_holds_files(const char *dir)
{
    DIR *d;
    struct dirent *ent;

    d = opendir(dir);
    if (d == NULL) {
        return 0;
    }
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

/* Whether dir already holds a game tree, so extracting into it would be wrong. */
static int tree_present(const char *dir)
{
    static const char *const marks[] = {
        "data/mugen.cfg", "data/system.def", "mugen.cfg", NULL
    };
    char probe[1200];
    int i;

    for (i = 0; marks[i] != NULL; i++) {
        struct stat st;
        snprintf(probe, sizeof(probe), "%s/%s", dir, marks[i]);
        if (stat(probe, &st) == 0 && S_ISREG(st.st_mode)) {
            return 1;
        }
    }
    return 0;
}

static char g_assets_buf[1024];

static const char *assets_dir(void)
{
    /*
     * g_assets_buf is used directly, never through a `char *buf = ...` alias:
     * the alias made sizeof(buf) the size of a pointer, so snprintf kept only
     * the first 7 bytes ("/storag") and the engine looked for its tree there.
     * The buffer lives at file scope only so the folder picker can reset it.
     */
    const char *base;
    const char *chosen;

    if (g_assets_buf[0] != '\0') {
        return g_assets_buf;
    }
    /*
     * A folder the player picked wins over the default. This is what makes
     * "Select Game Folder" take effect: the engine resolves every path against
     * the working directory, so pointing it at their tree is the whole feature.
     */
    chosen = chosen_game_folder();
    if (chosen != NULL) {
        snprintf(g_assets_buf, sizeof(g_assets_buf), "%s", chosen);
        return g_assets_buf;
    }
    /*
     * SDL_AndroidGetExternalStoragePath returns a directory the app can write to
     * without runtime permissions: /sdcard/Android/data/<pkg>/files
     * That is deliberately used in place of the app's internal data dir, because
     * a M.U.G.E.N tree (chars, stages, sound) is routinely larger than the
     * internal storage apps are given, and users expect to drop new characters
     * in from a file manager.
     */
    base = SDL_AndroidGetExternalStoragePath();
    if (base == NULL || base[0] == '\0') {
        base = SDL_AndroidGetInternalStoragePath();
    }
    if (base == NULL || base[0] == '\0') {
        snprintf(g_assets_buf, sizeof(g_assets_buf), "/data/local/tmp/mugen");
    } else {
        snprintf(g_assets_buf, sizeof(g_assets_buf), "%s/mugen", base);
    }
    return g_assets_buf;
}

static int mkdir_p(const char *path)
{
    char tmp[1024];
    size_t len;
    size_t i;

    len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, len + 1);
    for (i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
    return mkdir(tmp, 0777);
}

static int file_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

/* FNV-1a 64-bit over a file; empty string (not NULL) when unreadable. */
static void file_hash(const char *path, char out[17])
{
    FILE *f = fopen(path, "rb");
    /* FNV-1a 64 basis 14695981039346656037 (0xcbf29ce484222325); this MUST
     * match the Gradle manifest computation or every file hash-mismatches. */
    unsigned long long h = 14695981039346656037ULL;
    unsigned char buf[16384];
    size_t n;

    out[0] = '\0';
    if (f == NULL) {
        return;
    }
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        size_t i;
        for (i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
    }
    fclose(f);
    snprintf(out, 17, "%016llx", h);
}

/*
 * User-owned files that extraction must never overwrite. mugen.cfg holds
 * remaps and FirstRun state the engine rewrites at runtime, and
 * mugen.android.cfg holds device-specific overlay settings; clobbering
 * either on upgrade would destroy the player's configuration (B6).
 */
static int is_user_owned(const char *rel)
{
    return strcmp(rel, "data/mugen.cfg") == 0 ||
           strcmp(rel, "data/mugen.android.cfg") == 0;
}

/*
 * Copies one asset, creating parent directories.
 *
 * B6: write to a sibling temp, verify size + FNV-1a against the manifest,
 * then rename atomically. An existing destination with matching size+hash is
 * skipped (verified, not `stat`-trusted). An existing file whose content
 * matches NEITHER the manifest NOR nothing (i.e. user-modified) is preserved
 * for user-owned paths (data/mugen.cfg) with a logged conflict; other stale
 * files are replaced by the verified payload.
 *
 * Returns 1 when written, 0 when skipped, -1 on failure.
 */
static int extract_asset(const char *rel, long expect_size, const char *expect_hash)
{
    char dst[1024];
    char tmp[1088];
    SDL_RWops *in;
    FILE *out;
    char chunk[16384];
    long written = 0;
    char got[17];

    /* Reject escapes and absolute paths: the manifest is build-generated, but
     * a crafted APK must not write outside the data dir. */
    if (rel[0] == '\0' || rel[0] == '/' || strstr(rel, "..") != NULL || strchr(rel, '\\') != NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "bad manifest entry: %s", rel);
        return -1;
    }
    snprintf(dst, sizeof(dst), "%s/%s", assets_dir(), rel);
    if (rel[strlen(rel) - 1] == '/') {
        return 0;
    }
    if (file_exists(dst)) {
        struct stat st;
        stat(dst, &st);
        if ((long)st.st_size == expect_size) {
            file_hash(dst, got);
            if (strcmp(got, expect_hash) == 0) {
                return 0;
            }
        }
        if (is_user_owned(rel)) {
            __android_log_print(ANDROID_LOG_INFO, ALOG_TAG,
                                "keeping user-modified %s (payload differs)", rel);
            return 0;
        }
    }
    {
        char dir[1024];
        size_t di = strlen(dst);
        while (di > 0 && dst[di - 1] != '/') {
            di--;
        }
        if (di > 0) {
            memcpy(dir, dst, di);
            dir[di] = '\0';
            mkdir_p(dir);
        }
    }

    in = SDL_RWFromFile(rel, "rb");
    if (in == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "asset missing from APK: %s", rel);
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s.part", dst);
    out = fopen(tmp, "wb");
    if (out == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "cannot write %s", tmp);
        SDL_RWclose(in);
        return -1;
    }
    while (1) {
        Sint64 n = SDL_RWread(in, chunk, 1, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        if (fwrite(chunk, 1, (size_t)n, out) != (size_t)n) {
            __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "short write on %s", tmp);
            written = -1;
            break;
        }
        written += (long)n;
    }
    SDL_RWclose(in);
    fclose(out);

    if (written < 0) {
        remove(tmp);
        return -1;
    }
    if (written != expect_size) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG,
                            "asset %s truncated: %ld of %ld bytes", rel, written, expect_size);
        remove(tmp);
        return -1;
    }
    file_hash(tmp, got);
    if (strcmp(got, expect_hash) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "asset %s hash mismatch", rel);
        remove(tmp);
        return -1;
    }
    if (rename(tmp, dst) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "cannot install %s", dst);
        remove(tmp);
        return -1;
    }
    return 1;
}

static void extract_assets(void);

/* Discards every cached answer about which folder to use. */
static void forget_folder_choice(void)
{
    chosen_game_folder_forget();
    g_assets_buf[0] = '\0';
}

/*
 * Opens a folder: extracts the bundled assets into it if it has none, then makes
 * it the working directory.
 *
 * Called for a cold start and for a live switch, so a folder means the same
 * thing either way -- the versions that differed let a folder bootstrap from the
 * launcher but not from the overlay.
 *
 * Extract, then chdir: the payload and manifest are APK assets, which
 * SDL_RWFromFile resolves through its Android asset path rather than the working
 * directory. Swapping these two ran the extraction from the wrong directory and
 * the engine then failed to open data/mugen.cfg.
 */
static void open_folder(const char *dir, const char *why)
{
    snprintf(g_assets_buf, sizeof(g_assets_buf), "%s", dir);
    extract_assets();
    if (chdir(dir) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "%s: chdir(%s) failed", why, dir);
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, ALOG_TAG, "%s: working directory %s", why, dir);
}

void Android_ApplyChosenFolder(void)
{
    const char *dir;

    forget_folder_choice();
    dir = assets_dir();
    if (!g_game_folder_chosen) {
        __android_log_print(ANDROID_LOG_INFO, ALOG_TAG,
                            "assets: no chosen folder; using the default tree");
    }
    open_folder(dir, "startup");
}

/*
 * A live folder switch requested from the UI thread.
 *
 * Android_RequestFolderRestart only records the request. It deliberately does
 * not chdir: the engine thread is still running a gameflow in the tree being
 * left, and the working directory is process-wide, so moving it there means
 * every relative load or save until the engine notices resolves against the new
 * tree -- a save from the options screen in that window would land in the pack
 * the player just selected. The engine thread takes the request with
 * Android_TakeFolderRequest after teardown and opens it with
 * Android_OpenRequestedFolder, when nothing is reading the old tree.
 */
static SDL_mutex *g_folder_lock;
static char g_folder_request[1024];

static SDL_mutex *folder_lock(void)
{
    if (g_folder_lock == NULL) {
        g_folder_lock = SDL_CreateMutex();
    }
    return g_folder_lock;
}

void Android_RequestFolderRestart(const char *path)
{
    SDL_mutex *lock = folder_lock();

    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (lock == NULL) {
        return;
    }
    SDL_LockMutex(lock);
    snprintf(g_folder_request, sizeof(g_folder_request), "%s", path);
    SDL_UnlockMutex(lock);
    g_folderReloadRequested = 1;
    __android_log_print(ANDROID_LOG_INFO, ALOG_TAG, "folder switch requested: %s", path);
}

int Android_TakeFolderRequest(char *out, size_t out_size)
{
    SDL_mutex *lock = folder_lock();
    int have = 0;

    if (lock == NULL) {
        return 0;
    }
    SDL_LockMutex(lock);
    if (g_folder_request[0] != '\0') {
        have = 1;
        if (out != NULL && out_size > 0) {
            snprintf(out, out_size, "%s", g_folder_request);
        }
        g_folder_request[0] = '\0';
    }
    SDL_UnlockMutex(lock);
    return have;
}

void Android_OpenRequestedFolder(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return;
    }
    /* The requested tree may not be the one the marker names any more, so pin
     * the cache to it and clear the marker-derived answers. */
    chosen_game_folder_forget();
    g_assets_buf[0] = '\0';
    open_folder(path, "switch");
}

/*
 * Extracts the asset payload on first run (or after an upgrade).
 *
 * B6: the stamp holds the payload generation hash, not a static version.
 * A differing payload (APK upgrade) re-extracts; a failed run leaves no
 * stamp, so the next launch retries. The manifest is buffered in memory
 * (no tmpfile: its directory may not be writable) and parsed as
 * "rel-path size hash" records. Missing game data fails startup loudly
 * in main(), not here with a misleading stamp.
 */
static void extract_assets(void)
{
    char stamp[1024];
    char stamp_hash[64];
    char payload_hash[64];
    SDL_RWops *manifest;
    SDL_RWops *payload;
    char *buf;
    Sint64 size;
    Sint64 payload_bytes;
    int ok = 1;
    int count = 0;
    int skipped = 0;

    mkdir_p(assets_dir());

    /*
     * A tree the player picked is theirs: extract into it only when it is empty
     * (a folder they created for this purpose). Otherwise the bundled assets
     * would be copied on top of a pack they installed themselves, adding the
     * sample characters and stages they did not ask for -- and, when the pack
     * lacks the marker files, overwriting their own files in place. The default
     * folder keeps the old behaviour, where extracting is what bootstraps it.
     */
    if (g_game_folder_chosen &&
        (tree_present(assets_dir()) || folder_holds_files(assets_dir()))) {
        __android_log_print(ANDROID_LOG_INFO, ALOG_TAG,
                            "assets: %s is not empty; leaving it untouched",
                            assets_dir());
        return;
    }

    /* Read the payload generation first: it decides whether work is needed. */
    payload = SDL_RWFromFile(ASSET_PAYLOAD, "rb");
    if (payload == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG,
                            "asset payload missing; the APK was built without one");
        return;
    }
    payload_bytes = (Sint64)SDL_RWread(payload, payload_hash, 1, sizeof(payload_hash) - 1);
    if (payload_bytes <= 0) {
        SDL_RWclose(payload);
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "cannot read asset payload");
        return;
    }
    SDL_RWclose(payload);
    payload_hash[payload_bytes] = '\0';

    snprintf(stamp, sizeof(stamp), "%s/%s", assets_dir(), ASSET_STAMP);
    {
        FILE *f = fopen(stamp, "rb");
        if (f != NULL) {
            if (fgets(stamp_hash, sizeof(stamp_hash), f) != NULL &&
                strcmp(stamp_hash, payload_hash) == 0) {
                fclose(f);
                return;
            }
            fclose(f);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, ALOG_TAG, "extracting assets to %s", assets_dir());

    manifest = SDL_RWFromFile(ASSET_MANIFEST, "rb");
    if (manifest == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG,
                            "asset manifest missing; the APK was built without one");
        return;
    }
    size = SDL_RWsize(manifest);
    if (size <= 0 || size > 1024 * 1024) {
        SDL_RWclose(manifest);
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "bad asset manifest size");
        return;
    }
    buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        SDL_RWclose(manifest);
        return;
    }
    if (SDL_RWread(manifest, buf, 1, size) != size) {
        free(buf);
        SDL_RWclose(manifest);
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "cannot read asset manifest");
        return;
    }
    SDL_RWclose(manifest);
    buf[size] = '\0';
    {
        char *line = strtok(buf, "\n");
        while (line != NULL) {
            char rel[1024];
            long expect_size;
            char expect_hash[17];
            size_t n = strlen(line);
            while (n > 0 && line[n - 1] == '\r') {
                line[--n] = '\0';
            }
            {
                /*
                 * Record format is "size hash path-to-end-of-line": size and
                 * hash contain no spaces, so paths with spaces parse exactly.
                 * A record that does not parse fails the whole extraction
                 * (ok = 0, no stamp) -- silently skipping it would report a
                 * complete install with zero files extracted.
                 */
                int fields;
                int path_off = 0;

                fields = sscanf(line, "%ld %16s %n", &expect_size, expect_hash, &path_off);
                if (n > 0 && fields == 2 && path_off > 0 && line[path_off] != '\0') {
                    int r;

                    strncpy(rel, line + path_off, sizeof(rel) - 1);
                    rel[sizeof(rel) - 1] = '\0';
                    r = extract_asset(rel, expect_size, expect_hash);
                    if (r < 0) {
                        ok = 0;
                    } else if (r == 0) {
                        skipped++;
                    } else {
                        count++;
                    }
                } else if (n > 0) {
                    __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG,
                                        "bad manifest record: %.64s", line);
                    ok = 0;
                }
            }
            line = strtok(NULL, "\n");
        }
    }
    free(buf);

    __android_log_print(ANDROID_LOG_INFO, ALOG_TAG,
                        "assets: %d extracted, %d already present, %s",
                        count, skipped, ok ? "complete" : "INCOMPLETE (will retry next launch)");

    if (ok) {
        FILE *f = fopen(stamp, "wb");
        if (f != NULL) {
            fputs(payload_hash, f);
            fclose(f);
        }
    }
}

/*
 * The real entry point.
 *
 * SDL_main.h renames the engine's main() to SDL_main, and SDL2's Java activity
 * calls this native entry after the library is loaded. Assets are extracted and
 * the working directory is moved before the engine sees anything, so every
 * relative path in mugen.cfg resolves exactly as it does on desktop.
 */
int main(int argc, char *argv[])
{
    SDL_SetMainReady();
    /*
     * M5: argv arrives from SDLActivity's Java side, which SDL2 has already
     * parsed from the intent. Discarding it meant an argument supplied by an
     * intent silently did nothing. It is kept as the base, and the `cmdline`
     * file below is appended to it, so a device-local override is still possible
     * without rebuilding.
     */

    /*
     * The accelerometer must not be a joystick.
     *
     * SDL defaults SDL_ACCELEROMETER_AS_JOYSTICK to on, which adds a phantom
     * "Android Accelerometer" joystick at slot 0 on every device. Nothing in
     * this engine reads it, but it still consumes a player slot: the on-screen
     * virtual controller (TouchControllerOverlay) then attached to slot 1 and
     * played as player 2. Turn it off so the first real controller -- virtual
     * or physical -- is player 1. Must be set before SDL_Init.
     */
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    if (SDL_Init(0) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "SDL_Init(0): %s", SDL_GetError());
    }
    /*
     * The working directory and the extracted tree are settled by
     * Android_ApplyChosenFolder(), which the engine's main() calls before
     * System_Init -- it has to happen there because that is where a chosen
     * folder takes effect, and doing it in both places ran the extraction
     * twice with the second run in a directory that no longer had the payload.
     */

    /*
     * The engine's own command line. It parses -nosound, -rounds, -p1 ... exactly
     * as on desktop; passing none is the normal case.
     *
     * A file called "cmdline" in the data directory lets a user add options
     * without rebuilding -- useful on a device, where there is no shell. It is
     * NO LONGER read here: this runs before the chosen folder is opened, so the
     * handle resolved against whatever directory the process started in and the
     * selected tree's cmdline was silently ignored. The engine reads it in
     * Mugen_Main, immediately after the tree is open, and re-reads it on every
     * folder switch.
     */
    {
        static char *android_argv[32];
        int android_argc = 0;
        int i;

        /* SDL's arguments first (argc includes argv[0]). */
        for (i = 0; i < argc && android_argc < 30; i++) {
            if (argv[i] != NULL) {
                android_argv[android_argc++] = argv[i];
            }
        }
        if (android_argc == 0) {
            android_argv[android_argc++] = argv[0] != NULL ? argv[0] : (char *)"mugen";
        }
        android_argv[android_argc] = NULL;
        __android_log_print(ANDROID_LOG_INFO, ALOG_TAG, "engine starting, %d argument(s)", android_argc);
        return Mugen_Main(android_argc, android_argv);
    }
}

/*
 * JNI exports.
 *
 * The engine entry point needs none: SDLActivity dlopen()s libmain.so and
 * dlsym()s "SDL_main" (getMainFunction() in SDLActivity.java), so main() above
 * is reached by name. An earlier revision exported an empty
 * Java_org_libsdl_app_SDLActivity_nativeInit for a symbol SDLActivity does not
 * define; it has been removed.
 *
 * "Select Game Folder" does need one, because the player can pick a folder while
 * the engine is running and the choice has to reach a process that is already
 * past the point where its working directory was decided.
 */
JNIEXPORT void JNICALL
Java_org_libsdl_app_SDLActivity_nativeApplyGameFolder(JNIEnv *env, jclass cls, jstring path)
{
    const char *chars = NULL;

    if (path != NULL) {
        chars = (*env)->GetStringUTFChars(env, path, NULL);
    }
    if (chars != NULL) {
        Android_RequestFolderRestart(chars);
        (*env)->ReleaseStringUTFChars(env, path, chars);
    }
}
#endif /* __ANDROID__ */
