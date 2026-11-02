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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_main.h>

#include "android_platform.h"

#define ALOG_TAG "mugen"

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

static const char *assets_dir(void)
{
    static char buf[1024];
    const char *base;

    if (buf[0] != '\0') {
        return buf;
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
        snprintf(buf, sizeof(buf), "/data/local/tmp/mugen");
    } else {
        snprintf(buf, sizeof(buf), "%s/mugen", base);
    }
    return buf;
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
    unsigned long long h = 1469598103934665603ULL;
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
 * remaps and FirstRun state the engine rewrites at runtime; clobbering it
 * on every upgrade would destroy the player's configuration (B6).
 */
static int is_user_owned(const char *rel)
{
    return strcmp(rel, "data/mugen.cfg") == 0;
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
            if (n > 0 && sscanf(line, "%1023s %ld %16s", rel, &expect_size, expect_hash) == 3) {
                int r = extract_asset(rel, expect_size, expect_hash);
                if (r < 0) {
                    ok = 0;
                } else if (r == 0) {
                    skipped++;
                } else {
                    count++;
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
    char cwd[1024];

    SDL_SetMainReady();
    /*
     * M5: argv arrives from SDLActivity's Java side, which SDL2 has already
     * parsed from the intent. Discarding it meant an argument supplied by an
     * intent silently did nothing. It is kept as the base, and the `cmdline`
     * file below is appended to it, so a device-local override is still possible
     * without rebuilding.
     */

    if (SDL_Init(0) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "SDL_Init(0): %s", SDL_GetError());
    }
    extract_assets();

    snprintf(cwd, sizeof(cwd), "%s", assets_dir());
    if (chdir(cwd) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "chdir(%s) failed", cwd);
    }

    /*
     * The engine's own command line. It parses -nosound, -rounds, -p1 ... exactly
     * as on desktop; passing none is the normal case. A file called "cmdline" in
     * the data directory lets a user add options without rebuilding -- useful on
     * a device, where there is no shell.
     */
    {
        static char *android_argv[32];
        static char cmdline[512];
        int android_argc = 0;
        int i;
        FILE *f;

        /* SDL's arguments first (argc includes argv[0]). */
        for (i = 0; i < argc && android_argc < 30; i++) {
            if (argv[i] != NULL) {
                android_argv[android_argc++] = argv[i];
            }
        }
        if (android_argc == 0) {
            android_argv[android_argc++] = argv[0] != NULL ? argv[0] : (char *)"mugen";
        }
        f = fopen("cmdline", "rb");
        if (f != NULL) {
            if (fgets(cmdline, sizeof(cmdline), f) != NULL) {
                char *tok = strtok(cmdline, " \t\r\n");
                while (tok != NULL && android_argc < 31) {
                    android_argv[android_argc++] = tok;
                    tok = strtok(NULL, " \t\r\n");
                }
            }
            fclose(f);
        }
        android_argv[android_argc] = NULL;
        __android_log_print(ANDROID_LOG_INFO, ALOG_TAG, "engine starting, %d argument(s)", android_argc);
        return Mugen_Main(android_argc, android_argv);
    }
}

/*
 * No JNI exports are needed here: SDLActivity dlopen()s libmain.so and dlsym()s
 * "SDL_main" (getMainFunction() in SDLActivity.java), so the entry point below
 * is reached by name. An earlier revision exported an empty
 * Java_org_libsdl_app_SDLActivity_nativeInit for a symbol SDLActivity does not
 * define; it has been removed.
 */
#endif /* __ANDROID__ */
