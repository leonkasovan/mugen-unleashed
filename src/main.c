/*
 * Program entry (original SDL_main at 0x421500, called by SDLmain's WinMain)
 * and the helpers that follow it in the module (0x4216F0-0x421A3F): the
 * special screens, the camera position, version numbers and the session
 * RNG.
 */
#include "mugen.h"
#include "memprof.h"
#include "profiler.h"

#ifdef MUGEN_VITA_GL
/*
 * PS Vita port: the write half of the stdout/stderr sink installed below.
 *
 * It accepts whatever it is given and reports it as written, and there is no
 * descriptor behind it, so a write can neither fault in sceIoWrite nor create
 * a file. read/seek/close are NULL in the funopen call: nothing reads stdin
 * through these streams and nothing reopens or seeks them.
 *
 * _READ_WRITE_BUFSIZE_TYPE is int on this target (newlib sys/config.h), and is
 * spelled out so the callback matches funopen's prototype exactly.
 */
static int Vita_StdioDiscard(void *cookie, const char *buf,
                             _READ_WRITE_BUFSIZE_TYPE len)
{
    (void)cookie;
    (void)buf;
    return len;
}
#endif

#ifdef ANDROID_PORT
/*
 * Applies a pending game-folder switch: tears the running session down, then
 * opens the tree the request names, ready for Mugen_Main to start again.
 *
 * Returns 1 when the caller should goto restart, 0 when the request could not be
 * honoured (in which case the engine is already down and the caller must unwind).
 *
 * Order matters and is the point of this function. The old session is freed
 * FIRST, while every relative path still means what it meant when the engine
 * opened it; only then is the working directory moved. Doing it the other way
 * round is what let a save during the switch land in the pack just selected.
 */
static int Folder_SwitchPending(void)
{
    char switch_to[1024];

    g_folderReloadRequested = 0;
    Log_Printf("Game folder changed: restarting the engine.\n");
    System_Deinit();
    /*
     * The old log belongs to the tree being left; the new tree gets its own
     * mugen.log, which is the only way to tell the two runs apart afterwards.
     */
    Log_Close();
    g_log = NULL;
    if (!Android_TakeFolderRequest(switch_to, sizeof(switch_to))) {
        /* The flag was seen but the path is gone: there is nothing to open. */
        Log_Printf("Game folder request lost the path; not restarting.\n");
        free(g_cmdline);
        return 0;
    }
    Android_OpenRequestedFolder(switch_to);
    Log_Init();
    return 1;
}
#endif

/* 0x00421500 (SDL.h renames main to SDL_main) */
#if defined(MUGEN_NO_ENTRY)
/*
 * Tools build (tools/sffcli.c, "make sffcli"): main.c is compiled for its
 * helpers, not its entry point -- Screen_IsSpecial, Camera_GetX/Y,
 * Str_IsVersionNumber, Version_Parse, Rng_Seed and Random_Next, which the rest
 * of the engine calls unconditionally. The same renaming the Android port uses
 * below, for the same reason: the tool supplies main(), and two definitions of
 * one symbol is a link error rather than a choice. Nothing calls Mugen_Main in
 * a tool build, so it is dropped by --gc-sections.
 */

int Mugen_Main(int argc, char *argv[])
#elif defined(ANDROID_PORT)
/*
 * Android port: main.c is compiled for its helpers, not its entry point.
 *
 * This module also holds Screen_IsSpecial, Camera_GetX/Y, Str_IsVersionNumber,
 * Version_Parse, Rng_Seed and Random_Next, which the rest of the engine calls
 * unconditionally -- excluding the file leaves them undefined. The entry point
 * is instead provided by port/android/app/src/main/jni/src/android_main.c,
 * which prepares the asset tree and then calls Mugen_Main.
 *
 * Renaming rather than duplicating keeps the reconstructed flow identical; only
 * the symbol name and the caller change.
 */
int Mugen_Main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
#ifdef MUGEN_VITA_GL
    /*
     * PS Vita port (port/psvita): stdout and stderr go to a sink in memory.
     *
     * The console has no standard output, and the descriptor newlib hands out
     * for fds 1 and 2 is not a valid SceIo fd: writing to it faults inside
     * sceIoWrite -- core dump 1793497610 (delta 0x4e000) died exactly there,
     * reached from _write_r. So the streams cannot be left as they are; the
     * engine writes to them on its failure paths (src/system.c's allocation
     * failures, and Error_Printf's vfprintf fallback when the error clipboard
     * does not exist yet), and reporting a fault would take the process down.
     *
     * What is deliberately NOT done here is redirecting them to a file.
     * ux0:data/mugen-stdout.txt and mugen-stderr.txt used to be created for
     * this, and were always empty: the engine reports through Log_Printf into
     * mugen.log, and Error_Printf / Error_Fatal write into g_game->error_text,
     * which reaches the log and, on this port, a message box
     * (SDL2 system message box before native GL ownership). stderr is only the fallback for the few
     * failures that happen before those exist, and those runs end at once.
     * Two empty files on the memory card are not worth that trade, so the sink
     * owns no file at all.
     */
    {
        FILE *sink = funopen(NULL, NULL, Vita_StdioDiscard, NULL, NULL);

        if (sink != NULL) {
            stdout = sink;
            stderr = sink;
        }
    }
#endif
#ifdef MUGEN_VITA_GL
    /* Port addition: identify the executing build before engine initialization. */
    VitaDiag_Boot();
#endif
    g_cmdline = malloc(sizeof(CmdLineArgs));
    if (g_cmdline == NULL) {
        return 0;
    }
    /*
     * Android: the folder picker writes the chosen tree to a marker file, and
     * this is what moves the process into it. It has to run before Log_Init(),
     * because Log_Init opens mugen.log relative to the working directory --
     * resolving the folder afterwards left the log in one tree and the engine
     * in another, and the engine then failed to find data/mugen.cfg at all.
     */
#ifdef ANDROID_PORT
    Android_ApplyChosenFolder();
#endif
    Log_Init();
#ifdef MUGEN_VITA_GL
    /*
     * Port addition: unsigned code runs below the clocks retail games get.
     * After Log_Init, not before it: the request reports what the console
     * actually granted, and that report is the only way a log can say whether
     * it was honoured. The clocks still apply to everything that loads below.
     */
    VitaPower_Init();
#endif
    /*
     * Android: "<tree>/cmdline" adds engine options without a shell. Read here,
     * not in android_main.c, because this only runs once the chosen folder is
     * open -- reading it before the chdir resolved the handle against the
     * directory the process happened to start in and ignored the selected
     * tree's file. Re-read on every folder switch, so the new tree's options
     * apply to the run they belong to.
     */
#ifdef ANDROID_PORT
    {
        /*
         * The engine's own argv storage: CmdLine_Parse keeps pointers into it,
         * so it must outlive this function and be rebuilt on every switch.
         */
        static char *android_argv[64];
        static char android_store[512];
        int n = argc < 63 ? argc : 63;
        int i;

        for (i = 0; i < n; i++) {
            android_argv[i] = argv[i];
        }
        argc = CmdLine_AppendFile(android_argv, n, 63, android_store, sizeof(android_store), "cmdline");
        android_argv[argc] = NULL;
        argv = android_argv;
    }
#endif
    Log_Printf("Parsing command line...\n");
    CmdLine_Parse(argc, argv);
    Log_Printf("Parse command line OK\n");
restart:
    /* Port addition: allocate in the counter's lifetime, not before it. */
    Memprof_Start();
    Log_Printf("Initializing...\n");
    System_Init();
    /* Port addition: arm the sampling profiler when MUGEN_PROF=1. */
    Prof_Start();
    Log_Printf("Initialize OK\n");

    Log_Printf("Set up graphics...");
    Gfx_SetRenderTarget(g_game->screen, 0);
    g_gfxClear(0);
    Gfx_SetClipRect(0, 0, g_targetWidth, g_targetHeight);
    Log_Printf("timer...");
    Timer_Reset();
    Timer_SetFps(60);
    g_game->start_ticks = (int)Timer_GetTicks();
    Log_Printf("OK\n");

#ifdef ANDROID_PORT
    /* A folder switch requested while a storyboard ran: skip the gameflow. */
    int reload_pending = 0;
#endif
    if (g_cmdline->storyboard[0] != '\0') {
        Storyboard_PlayFile(g_cmdline->storyboard);
        Log_Printf("\n");
        /*
         * A storyboard is a fixed run, not a gameflow, so it never reaches the
         * reload check below. Without this a folder switch requested while one
         * was playing would sit on the flag until the next launch.
         */
#ifdef ANDROID_PORT
        if (g_folderReloadRequested) {
            if (!Folder_SwitchPending()) {
                return 0;
            }
            reload_pending = 1;
        }
#endif
#ifdef ANDROID_PORT
    } else if (!reload_pending) {
#else
    } else {
#endif
        /*
         * Port addition (M8, RENDERING_PLAN 12.6): a GL renderer that cannot be
         * rebuilt after Android destroyed its context sets
         * g_rendererFallbackRequested. Re-running System_Init takes the same
         * verified GL -> System recovery the start-up path uses, including the
         * asset reload, instead of leaving the engine in mode=OpenGL with a
         * dead renderer. Bounded so a renderer that keeps failing cannot spin.
         */
        int attempts = 0;

        do {
            g_rendererFallbackRequested = 0;
            if (g_game->quick_vs) {
                Log_Printf("Playing quick VS.\n");
            }
            Log_Printf("\nEntering gameflow loop");
            Log_Printf("\n");
            Gameflow_Run();
            if (g_gameflowState != NULL) {
                free(g_gameflowState);
                g_gameflowState = NULL;
            }
            if (!g_rendererFallbackRequested) {
                break;
            }
            attempts++;
            if (attempts > 1) {
                Log_Printf("\nRenderer fallback requested again; not retrying.");
                Log_Printf("\n");
                break;
            }
            Log_Printf("\nRenderer fallback: restarting the renderer.");
            Log_Printf("\n");
            System_ReinitVideo();
        } while (1);
        Log_Printf("Returned from gameflow loop");
        Log_Printf("\n");
#ifdef ANDROID_PORT
        /*
         * The player picked a different game folder. This is the only place a
         * live switch is acted on: the UI thread recorded the path and raised the
         * flag but did not touch the working directory, so everything up to here
         * still ran in the tree being left.
         */
        if (g_folderReloadRequested) {
            if (!Folder_SwitchPending()) {
                return 0;
            }
            reload_pending = 1;
        }
#endif
    }
#ifdef ANDROID_PORT
    /* Both branches converge here, so the restart has exactly one site. */
    if (reload_pending) {
        goto restart;
    }
#endif

    Log_Printf("Deinitializing...\n");
    if (g_game != NULL && g_game->game_dir[0] != '\0') {
        _chdir(g_game->game_dir);
    }
    System_Deinit();
    free(g_cmdline);
    /* Port addition: write the sampling profile before the process goes away. */
    Prof_Stop(getenv("MUGEN_PROF_OUT"));
    Memprof_Stop(getenv("MUGEN_MEMPROF_OUT"));
    Log_Printf("Successful program termination.\n");
    if (g_log != NULL) {
        Log_Printf("\nLog file successfully closed.\n");
        Log_Close();
        g_log = NULL;
    }
    /*
     * Android_VideoQuit runs inside Video_Deinit (before System_Deinit's
     * SDL_Quit), not here: SDL_Destroy* after SDL_Quit is a use-after-shutdown.
     */
    return 0;
}

/* ------------------------------------------------------------------ helpers */

/* 0x004216F0: the screen being run is one of kinds 6-8 (no round intro, no KO skipping by the players). */
int Screen_IsSpecial(void)
{
    int kind = g_game->screen_kind;

    return kind == 6 || kind == 7 || kind == 8;
}

/* 0x00421720: v in 320-wide units to game units. */
float Units_From320(float v)
{
    float r = (float)((double)Ftol(g_game->game_coord[0]) * v / 320.0);

    return r;
}

/* 0x00421750: the camera x in game units, the whole-unit part included. */
float Camera_GetRawX(void)
{
    GameVars *g = g_game;
    float r = (float)(g_gameCoord[0] / g->stage.unit * g->camera_x + g->camera_offset_x);

    return r;
}

/* 0x00421780: the camera y in game units, the whole-unit part included. */
float Camera_GetRawY(void)
{
    GameVars *g = g_game;
    float r = (float)(g_gameCoord[0] / g->stage.unit * g->camera_y + (double)g->camera_zoom * g->camera_offset_y);

    return r;
}

/* 0x004217B0: the camera x in game units (CameraPos trigger). */
float Camera_GetX(void)
{
    GameVars *g = g_game;
    float r = (float)(((double)g->camera_x - g->camera_x_base) * (g_gameCoord[0] / g->stage.unit) + g->camera_offset_x);

    return r;
}

/* 0x004217E0: the camera y in game units (CameraPos trigger). */
float Camera_GetY(void)
{
    GameVars *g = g_game;
    float r = (float)(((double)g->camera_y - g->camera_y_base) * (g_gameCoord[0] / g->stage.unit) +
                      (double)g->camera_zoom * g->camera_offset_y);

    return r;
}

/* 0x00421820: s is a number of digits and dots (at least one character). */
int Str_IsVersionNumber(const char *s)
{
    int c = *s++;

    if (c == 0) {
        return 0;
    }
    do {
        if (c != '.' && (unsigned int)(c - '0') > 9) {
            return 0;
        }
        c = *s++;
    } while (c != 0);
    return 1;
}

/* A plain version number v ("1.0") as major << 24 | tenths << 16 | hundredths << 8. */
static unsigned int Version_FromNumber(double v)
{
    unsigned int r = (unsigned int)(Ftol(v * 10.0) % 10);

    r |= (unsigned int)Ftol(v) << 8;
    r <<= 8;
    r |= (unsigned int)(Ftol(v * 100.0) % 10);
    return r << 8;
}

/*
 * 0x00421850: [Info] versiondate: "mm,dd,yyyy" (as Version_ParseTriple, less
 * 0x1000000) or a plain number ("1.0"); 0 when missing or invalid.
 */
unsigned int Version_ParseDate(const char *s)
{
    if (s == NULL) {
        return 0;
    }
    if (strchr(s, ',') == NULL) {
        double v = atof(s);
        if (!Str_IsVersionNumber(s)) {
            return 0;
        }
        return Version_FromNumber(v);
    } else {
        unsigned int r = (unsigned int)Version_ParseTriple(s);
        if (r > 0x1000000) {
            return r - 0x1000000;
        }
    }
    return 0;
}

/*
 * 0x004218E0: [Info] mugenversion: "y,m,d" up to 2013 (as
 * Version_ParseTriple, less 0x1000000) or a number below 100 ("1.0"); 0 when
 * missing or invalid.
 */
unsigned int Version_Parse(const char *s)
{
    if (s == NULL) {
        return 0;
    }
    if (strchr(s, ',') == NULL) {
        double v = atof(s);
        if (!Str_IsVersionNumber(s) || !(100.0 > v)) {
            return 0;
        }
        return Version_FromNumber(v);
    } else {
        unsigned int r = (unsigned int)Version_ParseTriple(s);
        if (r - 0x1000001u <= 0x329013u) {
            return r - 0x1000000;
        }
    }
    return 0;
}

/*
 * 0x00421990: seeds the session RNG (Park-Miller, modulus 0x7FFFFFFF,
 * multiplier 16807) with `seed`, or with the time for 0; seeds of 0 and
 * 0x7FFFFFFF or more become 1.
 */
void Rng_Seed(int seed)
{
    GameVars *g;
    unsigned int s = (unsigned int)seed;

    if (s == 0) {
        s = (unsigned int)time(NULL);
    }
    g = g_game;
    if (s == 0 || s >= 0x7FFFFFFF) {
        s = 1;
    }
    g->session_seed_hi = 0;
    g->session_rng = s;
    g->session_seed = s;
    g->session_rng_mul = 16807;
    g->session_rng_mod = 0x7FFFFFFF;
}

/* 0x004219F0: the next value of the session RNG (the low 32 bits of the state). */
int Random_Next(void)
{
    GameVars *g = g_game;

    g->session_rng = g->session_rng * g->session_rng_mul % g->session_rng_mod;
    return (int)(unsigned int)g->session_rng;
}
