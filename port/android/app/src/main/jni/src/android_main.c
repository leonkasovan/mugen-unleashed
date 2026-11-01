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

#include "android_main.h"

#define ALOG_TAG "mugen"

/* The engine entry point, from src/main.c. That module is compiled for its
 * helpers too (Camera_GetX, Rng_Seed, ...), so its main() is renamed and called
 * from here instead of SDL_main. */
int Mugen_Main(int argc, char *argv[]);

/* Written by the Gradle build; lists every asset to extract, one path per line. */
#define ASSET_MANIFEST "manifest.txt"
/* Bump when the asset payload changes, to force re-extraction on upgrade. */
#define ASSET_STAMP    "mugen_assets_v1.stamp"

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

/*
 * Copies one asset, creating parent directories.
 *
 * Returns 1 when written, 0 when skipped, -1 on failure. A zero-byte result and
 * "file missing" are both failures worth logging individually: SDL's asset API
 * cannot list a directory, which is exactly why the build generates a manifest,
 * and a file listed there that does not open indicates a packaging problem
 * rather than a runtime one.
 */
static int extract_asset(const char *rel)
{
    char dst[1024];
    SDL_RWops *in;
    FILE *out;
    char chunk[16384];
    Sint64 total;
    int written = 0;

    snprintf(dst, sizeof(dst), "%s/%s", assets_dir(), rel);

    /* A directory entry in the manifest is a no-op; the file copy creates it. */
    if (rel[0] == '\0') {
        return 0;
    }
    if (rel[strlen(rel) - 1] == '/') {
        return 0;
    }
    if (file_exists(dst)) {
        return 0;
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
    total = SDL_RWsize(in);
    out = fopen(dst, "wb");
    if (out == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "cannot write %s", dst);
        SDL_RWclose(in);
        return -1;
    }
    while (1) {
        Sint64 n = SDL_RWread(in, chunk, 1, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        if (fwrite(chunk, 1, (size_t)n, out) != (size_t)n) {
            __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "short write on %s", dst);
            written = -1;
            break;
        }
        written += (int)n;
    }
    SDL_RWclose(in);
    fclose(out);

    if (written < 0) {
        return -1;
    }
    if (total > 0 && written != (int)total) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG,
                            "asset %s truncated: %d of %lld bytes", rel, written, (long long)total);
        return -1;
    }
    return 1;
}

/*
 * Extracts the asset payload on first run (or after an upgrade).
 *
 * A stamp file records that extraction completed, so the (slow) copy happens
 * once. It is written only after every asset succeeded, so an interrupted first
 * run simply retries rather than leaving a half-populated tree that would fail
 * later in a confusing place.
 */
static void extract_assets(void)
{
    char stamp[1024];
    SDL_RWops *manifest;
    FILE *list;
    char line[1024];
    int ok = 1;
    int count = 0;
    int skipped = 0;

    snprintf(stamp, sizeof(stamp), "%s/%s", assets_dir(), ASSET_STAMP);
    if (file_exists(stamp)) {
        return;
    }

    mkdir_p(assets_dir());
    __android_log_print(ANDROID_LOG_INFO, ALOG_TAG, "extracting assets to %s", assets_dir());

    manifest = SDL_RWFromFile(ASSET_MANIFEST, "rb");
    if (manifest == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG,
                            "asset manifest missing; the APK was built without one");
        return;
    }
    /* SDL_RWops has no line reader; copy to a FILE so fgets can be used. */
    list = tmpfile();
    if (list != NULL) {
        char chunk[4096];
        Sint64 n;
        while ((n = SDL_RWread(manifest, chunk, 1, sizeof(chunk))) > 0) {
            fwrite(chunk, 1, (size_t)n, list);
        }
        rewind(list);
    }
    SDL_RWclose(manifest);

    if (list == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, ALOG_TAG, "cannot buffer asset manifest");
        return;
    }
    while (fgets(line, sizeof(line), list) != NULL) {
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) {
            continue;
        }
        {
            int r = extract_asset(line);
            if (r < 0) {
                ok = 0;
            } else if (r == 0) {
                skipped++;
            } else {
                count++;
            }
        }
    }
    fclose(list);

    __android_log_print(ANDROID_LOG_INFO, ALOG_TAG,
                        "assets: %d extracted, %d already present, %s",
                        count, skipped, ok ? "complete" : "INCOMPLETE (will retry next launch)");

    if (ok) {
        FILE *f = fopen(stamp, "wb");
        if (f != NULL) {
            fputs("ok\n", f);
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
