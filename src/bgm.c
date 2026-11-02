/*
 * Background music (original module 0x46FB50-0x470C9F): the [Music] plugin
 * list of mugen.cfg, plugin ("subsystem") registration, BGM start-up and
 * shutdown, the .def BGM parameters and the functions that queue commands
 * for the BGM thread (bgmthread.c).
 *
 * Plugins are the built-in mpg123 and sdlmix tables or DLLs exporting
 * getBGMSubsystem. Each registered plugin gets the mixer format, its
 * settings and a few engine callbacks; a BGM file is played by the first
 * registered plugin whose extension list has the file's extension.
 */
/* <SDL.h> comes from mugen.h, which pulls it in after glad.h. */
#define HAVE_STRUCT_TIMESPEC 1      /* the CRT's time.h already has it */
#include <pthread.h>
#include <libresample.h>

#include "mugen.h"

/* The shared BGM lock; src/bgm.h picks SDL or pthread per platform. */
#define BGM_LOCK()   BGM_MUTEX_LOCK()
#define BGM_UNLOCK() BGM_MUTEX_UNLOCK()

typedef const BgmSubsystem *(__cdecl *BgmGetSubsystemFn)(void);

/* An empty array of pointers (inlined by the original). Quirk: when only one allocation fails, the other leaks. */
static DynArray *dynarray_new(void)
{
    DynArray *a = calloc(1, sizeof(DynArray));
    void *data;

    if (a == NULL) {
        g_outOfMemory = 1;
    }
    data = calloc(BGM_ARRAY_CAPACITY, sizeof(void *));
    if (data == NULL) {
        g_outOfMemory = 1;
    }
    if (a == NULL || data == NULL) {
        return NULL;
    }
    a->data = data;
    a->capacity = BGM_ARRAY_CAPACITY;
    a->elem_size = sizeof(void *);
    return a;
}

/* Appends elem_size bytes from elem, doubling the capacity when full (inlined by the original). */
static int dynarray_push(DynArray *a, const void *elem)
{
    if (a->count >= a->capacity) {
        void *data = realloc(a->data, a->elem_size * a->capacity * 2);

        if (data == NULL) {
            return 0;
        }
        a->data = data;
        a->capacity *= 2;
    }
    memcpy((char *)a->data + a->count * a->elem_size, elem, a->elem_size);
    a->count++;
    return 1;
}

/* Frees a plugin's settings (inlined by the original). */
static void bgm_settings_free(BgmSettings *s)
{
    DynArray *a;

    BgmSettings_Clear(s);
    a = s->keys;
    free(a->data);
    free(a);
    a = s->values;
    free(a->data);
    free(a);
    free(s);
}

/*
 * 0x0046FB50: deinitialises the registered plugins and frees the registry.
 * Quirk: g_bgmExtensions is freed but not cleared.
 */
void Bgm_FreeSubsystems(void)
{
    DynArray *subs = g_bgmSubsystems;
    DynArray *exts;
    int i;

    for (i = 0; i < subs->count; i++) {
        ((const BgmSubsystem **)subs->data)[i]->deinit();
        subs = g_bgmSubsystems;
    }
    free(subs->data);
    free(subs);
    exts = g_bgmExtensions;
    g_bgmSubsystems = NULL;
    free(exts->data);
    free(exts);
}

/* 0x0046FBB0: adds a plugin to the registry. 0 if it is already there, too old or out of memory. */
int Bgm_RegisterSubsystem(const BgmSubsystem *sub)
{
    DynArray *subs = g_bgmSubsystems;
    unsigned int version;
    int i;

    for (i = 0; i < subs->count; i++) {
        if (((const BgmSubsystem **)subs->data)[i] == sub) {
            return 0;
        }
    }
    version = sub->version();
    if (version < BGM_MIN_VERSION) {
        MatchLog_Printf(3, "Subsystem version %u.%u.%u.%u is too old for this version of M.U.G.E.N\n", version >> 24,
                        (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
        return 0;
    }
    return dynarray_push(g_bgmSubsystems, &sub);
}

/* 0x0046FC80: frees a [Music] plugin list (BgmPlugins_Load). */
void BgmPlugins_Free(DynArray *plugins)
{
    int i;

    for (i = 0; i < plugins->count; i++) {
        BgmPluginEntry *e = ((BgmPluginEntry **)plugins->data)[i];

        if (e == NULL) {
            continue;
        }
        if (e->name != NULL) {
            free(e->name);
        }
        if (e->file != NULL) {
            free(e->file);
        }
        if (e->extensions != NULL) {
            free(e->extensions);
        }
        if (e->settings != NULL) {
            bgm_settings_free(e->settings);
        }
        free(e);
    }
    free(plugins->data);
    free(plugins);
}

/*
 * 0x0046FD30: reads the [Music] group (selected in ini): "plugin = name
 * [, file]" lines, each followed by its settings. "extensions" is kept
 * apart (a second one becomes an ordinary setting); empty values are
 * skipped. NULL when out of memory.
 *
 * Quirks kept: blanks before the comma are trimmed by a loop that may read
 * the byte before the value (it cannot change the result); the file name
 * keeps trailing blanks; an entry is leaked when the list cannot grow; a
 * failed "extensions" allocation is written to anyway.
 */
DynArray *BgmPlugins_Load(Ini *ini)
{
    char key[0x100];
    char line[1 + 0x100];       /* line[0] is the byte the trimming loop may read before value */
    char *value = line + 1;
    DynArray *list = dynarray_new();

    line[0] = '\0';
    while (!ini->at_end) {
        const char *file = NULL;
        int file_len = 0;
        int name_len;
        const char *comma;
        BgmPluginEntry *e;
        BgmSettings *settings;
        int have_extensions;

        if (!Ini_ReadKeyValue(ini, key, value) || _stricmp(key, "plugin") != 0) {
            continue;
        }
        comma = strchr(value, ',');
        if (comma != NULL) {
            int pos = (int)(comma - value);
            int i = pos - 1;
            int j;

            if (isspace(value[i])) {
                while (i >= 0) {
                    char c = value[i - 1];
                    i--;
                    if (!isspace(c)) {
                        break;
                    }
                }
            }
            name_len = i + 1;
            j = pos + 1;
            while (isspace(value[j])) {
                j++;
            }
            file = value + j;
            file_len = (int)strlen(file);
        } else {
            name_len = (int)strlen(value);
        }
        if (name_len == 0) {
            continue;
        }

        e = calloc(1, sizeof(BgmPluginEntry));
        if (e == NULL) {
            g_outOfMemory = 1;
        }
        have_extensions = 0;
        if (e == NULL) {
            BgmPlugins_Free(list);
            return NULL;
        }
        e->name = calloc(name_len + 1, 1);
        if (e->name == NULL) {
            g_outOfMemory = 1;
            free(e);
            BgmPlugins_Free(list);
            return NULL;
        }
        if (file != NULL && file_len > 0) {
            e->file = calloc(file_len + 1, 1);
            if (e->file == NULL) {
                g_outOfMemory = 1;
                free(e->name);
                free(e);
                BgmPlugins_Free(list);
                return NULL;
            }
        } else {
            e->file = NULL;
        }
        strncpy(e->name, value, name_len);
        e->name[name_len] = '\0';
        if (file != NULL && file_len > 0) {
            strncpy(e->file, file, file_len);
            e->file[file_len] = '\0';
        } else {
            e->file = NULL;
        }
        if (!dynarray_push(list, &e)) {
            BgmPlugins_Free(list);
            return NULL;
        }
        settings = BgmSettings_New();
        if (settings == NULL) {
            BgmPlugins_Free(list);
            return NULL;
        }
        e->settings = settings;

        do {
            if (!Ini_ReadKeyValue(ini, key, value)) {
                continue;
            }
            if (_stricmp(key, "plugin") == 0) {
                Ini_PrevLine(ini);
                break;
            }
            if (strlen(value) == 0) {
                continue;
            }
            if (_stricmp(key, "extensions") == 0 && !have_extensions) {
                e->extensions = calloc(strlen(value) + 1, 1);
                if (e->extensions == NULL) {
                    g_outOfMemory = 1;
                }
                strcpy(e->extensions, value);
                have_extensions = 1;
            } else if (!BgmSettings_Set(settings, key, value)) {
                BgmPlugins_Free(list);
                return NULL;
            }
        } while (!ini->at_end);
    }
    return list;
}

/*
 * 0x00470130: 1 if ext (lower-cased) is an item of the comma-separated list.
 * Quirk: only the first occurrence of ext in list is considered ("mp3" is
 * not found in "xmp3,mp3").
 */
int Bgm_ExtensionInList(const char *ext, const char *list)
{
    char lower[0x204];
    int len = (int)strlen(ext);
    const char *p;
    int i;

    for (i = 0; ext[i] != '\0'; i++) {
        lower[i] = (char)tolower(ext[i]);
    }
    lower[len] = '\0';
    p = strstr(list, lower);
    if (p != NULL && (p == list || p[-1] == ',')) {
        char next = p[strlen(lower)];

        if (next == '\0' || next == ',') {
            return 1;
        }
    }
    return 0;
}

/* 0x00470200: plugin callback; the extension of path (after the last '.'), or NULL. */
const char *Bgm_GetExtension(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (ext == NULL) {
        return NULL;
    }
    ext++;
    if (*ext == '\0') {
        return NULL;
    }
    return strpbrk(ext, "/\\:") != NULL ? NULL : ext;
}

/* 0x00470240: plugin callback; loop[0..2] = loop, loop start, loop end of info. */
void Bgm_GetLoopInfo(const BgmInfo *info, int *loop)
{
    loop[0] = info->loop;
    loop[1] = info->loop_start;
    loop[2] = info->loop_end;
}

/*
 * 0x00470270: the registered plugin that plays path (by extension), or
 * NULL. Quirk: one-character extensions never match.
 */
const BgmSubsystem *Bgm_FindSubsystem(const char *path)
{
    const char *ext = strrchr(path, '.');
    int i;

    /* Bgm_GetExtension, inlined. */
    if (ext != NULL) {
        ext++;
        if (*ext == '\0' || strpbrk(ext, "/\\:") != NULL) {
            ext = NULL;
        }
    }
    if (g_bgmSubsystems == NULL || ext == NULL || strlen(ext) <= 1) {
        return NULL;
    }
    for (i = 0; i < g_bgmSubsystems->count; i++) {
        if (Bgm_ExtensionInList(ext, ((const char **)g_bgmExtensions->data)[i])) {
            return ((const BgmSubsystem **)g_bgmSubsystems->data)[i];
        }
    }
    return NULL;
}

/*
 * 0x00470310: registers and initialises the [Music] plugins and starts the
 * BGM thread. Returns 1 when there are no plugins or BGM is already up.
 * volume is clamped to 0..100 (NaN is kept).
 *
 * The original first checks that its frame counter callback (0x41E470) is
 * not NULL, which always holds. Quirks kept: plugins that fail to register
 * are skipped, but a plugin whose init fails ends with 0 (and the plugins
 * before it stay registered); the "unacceptable quality" method 0 fails
 * after the plugins were set up; the BGM thread's start-up result is only
 * logged.
 */
int Bgm_Init(DynArray *plugins, int resample_method, int resample_quality, double volume)
{
    int i;
#ifdef ANDROID_PORT
    BgmPluginEntry native_entry;
    BgmPluginEntry *native_entries[1];
    DynArray native_plugins;
#endif

#ifdef ANDROID_PORT
    if (g_bgmInitialized != 0) {
#else
    if (plugins == NULL || g_bgmInitialized != 0) {
#endif
        return 1;
    }
#ifdef ANDROID_PORT
    /* Deliberate port behavior: Windows plugin DLLs cannot run on Android.
     * One native SDL2_mixer subsystem owns WAV/OGG/MP3 music playback. */
    memset(&native_entry, 0, sizeof(native_entry));
    native_entry.name = "sdlmix";
    native_entry.extensions = (char *)Sdlmix_Extensions();
    native_entry.settings = BgmSettings_New();
    if (native_entry.settings == NULL) {
        return 0;
    }
    native_entries[0] = &native_entry;
    native_plugins.data = native_entries;
    native_plugins.elem_size = sizeof(native_entries[0]);
    native_plugins.capacity = 1;
    native_plugins.count = 1;
    plugins = &native_plugins;
    Log_Printf("Android BGM: native SDL2_mixer WAV/OGG/MP3 backend\n");
#endif
    g_bgmFormat = g_sndFormat;
    g_bgmChunk = g_sndChunkSize;
    if (volume < 0.0) {
        g_bgmVolume = 0.0;
    } else if (volume > 100.0) {
        g_bgmVolume = 100.0;
    } else {
        g_bgmVolume = volume;
    }
    g_bgmGetHandle = Game_GetFrameCount;
    g_bgmSubsystems = dynarray_new();
    g_bgmExtensions = dynarray_new();

    for (i = 0; i < plugins->count; i++) {
        BgmPluginEntry *e = ((BgmPluginEntry **)plugins->data)[i];
        const BgmSubsystem *sub;
        BgmPluginConfig config;
        BgmHostCallbacks host;
        int err;

        if (_stricmp(e->name, "mpg123") == 0) {
            sub = &g_bgmMpg123Subsystem;
        } else if (_stricmp(e->name, "sdlmix") == 0) {
            sub = &g_bgmSdlmixSubsystem;
        } else {
            HMODULE dll;
            BgmGetSubsystemFn get;

            if (e->file == NULL || (dll = LoadLibraryA(e->file)) == NULL ||
                (get = (BgmGetSubsystemFn)GetProcAddress(dll, "getBGMSubsystem")) == NULL) {
                MatchLog_Printf(5, "Unrecognized plugin: %s\n", e->name);
                continue;
            }
            sub = get();
            if (sub == NULL) {
                MatchLog_Printf(5, "Invalid plugin: %s\n", e->name);
                continue;
            }
        }
        if (e->extensions == NULL) {
            const char *def = sub->extensions();

            /* Not checked for NULL in the original either. */
            e->extensions = calloc(strlen(def) + 1, 1);
            if (e->extensions == NULL) {
                g_outOfMemory = 1;
            }
            strcpy(e->extensions, def);
        }
        config.format = g_bgmFormat;
        config.settings = e->settings;
        host.get_extension = Bgm_GetExtension;
        host.get_loop = Bgm_GetLoopInfo;
        host.reserved = NULL;
        err = sub->init(&config, &host, BgmSettings_Get);
        bgm_settings_free(e->settings);
        e->settings = NULL;
        if (err != 0) {
#if defined(MUGEN_SDLMIX_NATIVE_CODECS) && !defined(ANDROID_PORT)
            /*
             * Static-build fix. The original stopped at the first plugin that
             * failed to initialise, which is harmless when every plugin is a
             * DLL the game ships: a missing one means a broken install. This
             * build is meant to run with no plugin DLLs at all, and mugen.cfg
             * lists mpg123 first, so a faithful abort made a perfectly usable
             * sdlmix (which decodes WAV/MP3/OGG through SDL2_mixer)
             * unreachable.
             *
             * Skipping is not enough on its own: the plugin must also not be
             * registered, or Bgm_FindSubsystem() keeps handing .mp3 to the
             * decoder whose init just failed and the file never reaches sdlmix.
             * The registration and the extension entry are therefore deferred
             * below, to after a successful init.
             */
            continue;
#else
            return 0;
#endif
        }
        /*
         * Registered only once init() has succeeded. The original registered
         * first and initialised second; on the 32-bit reference build that is
         * unobservable, because a failed init ends Bgm_Init there anyway.
         */
        if (!Bgm_RegisterSubsystem(sub)) {
            MatchLog_Printf(3, "Internal error while registering plugin: %s\n", e->name);
            continue;
        }
        DynArray_Push(g_bgmExtensions, &e->extensions);
    }

    switch (resample_method) {
    case SND_RESAMPLE_SDL:
        MatchLog_Printf(3, "The chosen resampling method is of unacceptable quality.\n");
        return 0;
    case SND_RESAMPLE_LIBRESAMPLE:
        resample_init(resample_quality == 0, resample_quality == 1);
        /* The original calls it through the resampler table (0x4F1890 + 4). */
        g_bgmResampler = Resampler_Create(&g_bgmFormat, g_bgmChunk, &resample_quality);
        if (g_bgmResampler == NULL) {
            MatchLog_Printf(3, "Couldn't initialize resampling.\n");
            return 0;
        }
#ifdef ANDROID_PORT
        {
            int thread_error = BgmThread_Init(&g_bgmFormat, g_bgmChunk);
            MatchLog_Printf(9, "BGMThreadInit returned %d\n", thread_error);
            if (thread_error != 0) {
                free(g_bgmResampler->quality);
                free(g_bgmResampler);
                g_bgmResampler = NULL;
                Bgm_FreeSubsystems();
                return 0;
            }
        }
#else
        {
            /*
             * Fix (port change): the original only logged this result and then
             * declared BGM initialized, so a failed thread start left the
             * engine queueing commands nothing would ever run -- silently no
             * music, or a deadlock the first time a command filled the queue.
             * Treat it as an init failure like the Android branch does.
             */
            int thread_error = BgmThread_Init(&g_bgmFormat, g_bgmChunk);

            MatchLog_Printf(9, "BGMThreadInit returned %d\n", thread_error);
            if (thread_error != 0) {
                free(g_bgmResampler->quality);
                free(g_bgmResampler);
                g_bgmResampler = NULL;
                Bgm_FreeSubsystems();
                return 0;
            }
        }
#endif
        g_bgmInitialized = 1;
        g_bgmActive = 1;
        g_bgmBackgroundMode = BGM_BG_PLAY;
        return 1;
    default:
        MatchLog_Printf(3, "Unrecognized resampling method: %d\n", resample_method);
        return 0;
    }
}

/* 0x00470740: stops the BGM thread and frees the plugins and the resampler (libresample stays initialised). */
void Bgm_Deinit(void)
{
    Resampler *r;
    int err;

    if (g_bgmInitialized == 0) {
        return;
    }
    err = BgmThread_Shutdown();
    if (err != 0) {
        MatchLog_Printf(3, "error code %d in deinitialization!\n", err);
    }
    if (g_bgmSubsystems != NULL) {
        Bgm_FreeSubsystems();
    }
    r = g_bgmResampler;
    g_bgmInitialized = 0;
    g_bgmActive = 0;
    g_bgmVolume = 0.0;
    memset(&g_bgmFormat, 0, sizeof(g_bgmFormat));
    g_bgmChunk = 0;
    g_bgmBackgroundMode = 0;
    if (r->quality != NULL) {
        free(r->quality);
    }
    free(r);
    g_bgmResampler = NULL;
    g_bgmGetHandle = NULL;
}

/* 0x004707F0: a stream volume (percent, negative = 0) scaled by the BGM volume. */
double Bgm_ScaleVolume(double volume)
{
    return (volume < 0.0 ? 0.0 : volume) * g_bgmVolume / 100.0;
}

/* 0x00470820: sets the BGM volume (percent, not clamped here) and applies it to the playing stream. */
void Bgm_SetVolume(double volume)
{
    if (g_bgmInitialized != 0 && volume != g_bgmVolume) {
        g_bgmVolume = volume;
        Bgm_SetStreamVolume(-1.0);
    }
}

/*
 * 0x00470860: reads "<key>" and its ".volume", ".loop", ".loopstart" and
 * ".loopend" parameters from the selected group of ini (Bgm_ReadInfoKeys).
 * Quirk: key names longer than 0x4E characters are cut without a
 * terminator.
 */
int Bgm_ReadInfo(Ini *ini, const char *key, BgmInfo *info)
{
    char volume_key[0x50];
    char loop_key[0x50];
    char start_key[0x50];
    char end_key[0x50];

    _snprintf(volume_key, 0x4F, "%s.volume", key);
    _snprintf(loop_key, 0x4F, "%s.loop", key);
    _snprintf(start_key, 0x4F, "%s.loopstart", key);
    _snprintf(end_key, 0x4F, "%s.loopend", key);
    return Bgm_ReadInfoKeys(info, key, ini, volume_key, loop_key, start_key, end_key);
}

/*
 * 0x00470910: fills info from the selected group of ini: the path (key,
 * lower-cased) and the parameters, with their defaults when missing or
 * unreadable. Returns 0 (with the other fields untouched) when there is no
 * path.
 */
int Bgm_ReadInfoKeys(BgmInfo *info, const char *key, Ini *ini, const char *volume_key, const char *loop_key,
                     const char *start_key, const char *end_key)
{
    const char *v;

    info->path[0] = '\0';
    v = Ini_GetValue(ini, key);
    if (v == NULL) {
        return 0;
    }
    info->path[0] = '\0';
    strncat(info->path, v, 0x200);
    if (info->path[0] == '\0') {
        return 0;
    }
    /*
     * Fix (port change): the original lower-cased the whole path here. That
     * only ever mattered for the extension lookup, and Bgm_ExtensionInList()
     * lower-cases the extension itself, so the spelling is preserved now. On
     * a case-sensitive filesystem (Linux, Android) lower-casing made
     * "sound/StageTheme.ogg" unopenable even though the file existed. Windows
     * is unaffected either way.
     */
    v = Ini_GetValue(ini, loop_key);
    if (v == NULL || sscanf(v, "%d", &info->loop) != 1) {
        info->loop = 1;
    }
    v = Ini_GetValue(ini, start_key);
    if (v == NULL || sscanf(v, "%d", &info->loop_start) != 1) {
        info->loop_start = -1;
    }
    v = Ini_GetValue(ini, end_key);
    /*
     * Fix (port change): the original scanned "%ld" straight into
     * (long *)&info->loop_end, a 4-byte int. On a 32-bit build long is 4
     * bytes and it worked; on LP64 sscanf wrote 8 bytes over loop_end and the
     * field after it, whatever that happened to be. Scan into a long and
     * narrow, which is also what the original's intent was (it read a long).
     */
    if (v == NULL) {
        info->loop_end = -1;
    } else {
        long loop_end;
        int scanned = sscanf(v, "%ld", &loop_end);

        info->loop_end = (scanned == 1 && loop_end >= -1 && loop_end <= 0x7FFFFFFFL) ? (int)loop_end : -1;
    }
    if (info->loop_end <= info->loop_start) {
        info->loop_end = -1;
    }
    v = Ini_GetValue(ini, volume_key);
    if (v == NULL || sscanf(v, "%lf", &info->volume) != 1) {
        info->volume = 100.0;
    }
    return 1;
}

/*
 * Commands queued by the functions below carry two words of data; when only
 * the first is set, the second is whatever was on the stack (as in the
 * original).
 */

/* 0x00470A50: pauses the BGM (game pause; see BGM_STREAM_HELD). */
void Bgm_Pause(void)
{
    int data[2] = {0};   /* fix: data[1] used to be uninitialised stack */

    if (g_bgmActive == 0) {
        return;
    }
    BGM_LOCK();
    data[0] = BGM_STREAM_HELD;
    Bgm_QueueCommand(g_bgmGetHandle(), data, BGM_CMD_HOLD, 0);
    BGM_UNLOCK();
    g_bgmBackgroundChanged = 1;
}

/* 0x00470AB0: undoes Bgm_Pause. */
void Bgm_Unpause(void)
{
    int data[2] = {0};   /* fix: data[1] used to be uninitialised stack */

    if (g_bgmActive == 0) {
        return;
    }
    BGM_LOCK();
    data[0] = BGM_STREAM_HELD;
    Bgm_QueueCommand(g_bgmGetHandle(), data, BGM_CMD_UNHOLD, 0);
    BGM_UNLOCK();
    g_bgmBackgroundChanged = 1;
}

/*
 * 0x00470B10: sets the volume of the playing stream (percent of the BGM
 * volume, negative = 0); -1 re-applies the stream's own volume.
 */
void Bgm_SetStreamVolume(double volume)
{
    double scaled = (volume < 0.0 ? 0.0 : volume) * g_bgmVolume / 100.0;

    if (g_bgmActive == 0) {
        return;
    }
    BGM_LOCK();
    if (volume != -1.0) {
        volume = scaled;
    }
    Bgm_QueueCommand(g_bgmGetHandle(), (const int *)&volume, BGM_CMD_VOLUME, 0);
    BGM_UNLOCK();
    g_bgmBackgroundChanged = 1;
}

/* 0x00470BC0: loads info's BGM (stopped). Always 1. */
int Bgm_Load(const BgmInfo *info)
{
    if (g_bgmActive == 0) {
        return 1;
    }
    BGM_LOCK();
    BgmQueue_Load(g_bgmGetHandle(), info);
    BGM_UNLOCK();
    g_bgmBackgroundChanged = 1;
    return 1;
}

/* 0x00470C10: plays info's BGM (loaded unless it is already playing). Always 1. */
int Bgm_Play(const BgmInfo *info)
{
    if (g_bgmActive == 0) {
        return 1;
    }
    BGM_LOCK();
    BgmQueue_Play(g_bgmGetHandle(), info);
    BGM_UNLOCK();
    g_bgmBackgroundChanged = 1;
    return 1;
}

/* 0x00470C60: stops and unloads the BGM. */
void Bgm_Stop(void)
{
    if (g_bgmActive == 0) {
        return;
    }
    BGM_LOCK();
    BgmQueue_Unload(g_bgmGetHandle());
    BGM_UNLOCK();
    g_bgmBackgroundChanged = 1;
}
