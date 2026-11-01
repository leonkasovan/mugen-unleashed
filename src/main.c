/*
 * Program entry (original SDL_main at 0x421500, called by SDLmain's WinMain)
 * and the helpers that follow it in the module (0x4216F0-0x421A3F): the
 * special screens, the camera position, version numbers and the session
 * RNG.
 */
#include "mugen.h"

/* 0x00421500 (SDL.h renames main to SDL_main) */
#ifdef ANDROID_PORT
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
     * PS Vita port (port/psvita): give stdout and stderr real files.
     *
     * The console has no standard output. Whatever descriptor newlib hands out
     * for stdout/stderr is not a valid SceIo fd, and writing to it faults inside
     * sceIoWrite -- core dump 1793497610 (delta 0x4e000) died exactly there,
     * reached from _write_r.
     *
     * The engine writes to these streams on its error paths: Error_Printf falls
     * back to vfprintf(stderr, ...) when the error clipboard does not exist yet,
     * and several allocations printf before the log is up. So the failure mode
     * was: something goes wrong, the engine tries to report it, and reporting it
     * takes the process down -- hiding the original fault.
     *
     * Redirecting them costs nothing, removes a whole class of crash, and leaves
     * the text on the memory card where it can actually be read. Windows has
     * SDLmain doing the same thing to stdout.txt/stderr.txt.
     */
    if (freopen("ux0:data/mugen-stdout.txt", "w", stdout) == NULL) {
        (void)0;    /* nothing to report to: carry on with the original stream */
    }
    if (freopen("ux0:data/mugen-stderr.txt", "w", stderr) == NULL) {
        (void)0;
    }
#endif
    g_cmdline = malloc(sizeof(CmdLineArgs));
    if (g_cmdline == NULL) {
        return 0;
    }
    Log_Init();
    Log_Printf("Parsing command line...\n");
    CmdLine_Parse(argc, argv);
    Log_Printf("Parse command line OK\n");
    Log_Printf("Initializing...\n");
    System_Init();
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

    if (g_cmdline->storyboard[0] != '\0') {
        Storyboard_PlayFile(g_cmdline->storyboard);
    } else {
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
        Log_Printf("Returned from gameflow loop");
        Log_Printf("\n");
    }

    Log_Printf("Deinitializing...\n");
    if (g_game != NULL && g_game->game_dir[0] != '\0') {
        _chdir(g_game->game_dir);
    }
    System_Deinit();
    free(g_cmdline);
    Log_Printf("Successful program termination.\n");
    if (g_log != NULL) {
        Log_Printf("\nLog file successfully closed.\n");
        Log_Close();
        g_log = NULL;
    }
#ifdef ANDROID_PORT
    /*
     * Android port: release the window, renderer and texture. The process is
     * about to exit and the OS would reclaim them anyway, but leaving an
     * acquired window would matter if this path ever became reachable from an
     * Android lifecycle pause instead of process death. Android_VideoQuit is
     * declared in port/android/.../android_main.h, which is force-included.
     */
    Android_VideoQuit();
#endif
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
