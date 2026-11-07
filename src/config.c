/*
 * mugen.cfg: configuration and options (original module 0x45DE80-0x45FC7F).
 *
 * Config_Load reads the whole file at start-up; Options_Load/Options_Save
 * handle the part the options screen edits ([Options], volumes, keys).
 */
#include "mugen.h"
#include "render_policy.h"

#ifdef MUGEN_VITA_GL
/* Defined near Options_Load; Config_Load runs first. */
static void Config_ApplyVitaJoystick(void);
#endif
#ifdef MUGEN_GAMEPAD_POLICY
/* The [P<n> Gamepad] groups, loaded with the rest of the options and applied
 * after the pads open (their topology is only known then). */
static int g_cfgGamepadKeys[2][KEY_COUNT];
static int g_cfgGamepadPresent[2];
static int g_cfgGamepadMissing;
static void Config_LoadGamepadGroups(Ini *ini);
static void Config_WriteMissingGamepadGroups(void);
/* Local semantic codes, independent of the runtime player's slot offset. */
static const struct {
    const char *name;
    int key;
    int code;
} gamepad_bindings[] = {
    { "Jump", KEY_JUMP, 1 }, { "Crouch", KEY_CROUCH, 2 },
    { "Left", KEY_LEFT, 3 }, { "Right", KEY_RIGHT, 4 },
    { "A", KEY_A, 5 }, { "B", KEY_B, 6 }, { "C", KEY_C, 7 },
    { "X", KEY_X, 8 }, { "Y", KEY_Y, 9 }, { "Z", KEY_Z, 10 },
    { "Start", KEY_START, 11 },
};
/* Defined near Options_Load; Config_Load runs first. */
/* The configured [P<n> Joystick] codes, kept across the controller policy. */
static int g_cfgRawJoyKeys[2][KEY_COUNT];

void Config_ApplyJoystickBindings(void);
/* Per-player version, used when a controller attaches after start-up. */
void Config_ApplyJoystickSlot(int player);
#endif

#define CONFIG_PATH "data/mugen.cfg"

static const char *const ai_ramp_modes[3] = { "arcade", "team", "survival" };

/* 0x0045FA30: reads a [P<n> Keys] / [P<n> Joystick] group into keys (missing keys keep their value). */
int Config_ReadKeys(Ini *ini, const char *group, int *keys)
{
    static const struct {
        const char *name;
        int index;
    } map[] = {
        { "Jump", KEY_JUMP }, { "Crouch", KEY_CROUCH }, { "Left", KEY_LEFT }, { "Right", KEY_RIGHT },
        { "A", KEY_A }, { "B", KEY_B }, { "C", KEY_C }, { "X", KEY_X }, { "Y", KEY_Y }, { "Z", KEY_Z },
        { "Start", KEY_START },
    };
    int i;

    if (Ini_SelectGroup(ini, group) == -1) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++) {
        const char *value = Ini_GetValue(ini, map[i].name);
        if (value != NULL) {
            keys[map[i].index] = atoi(value);
        }
    }
    return 1;
}

/* 0x0045FB90: writes keys into the selected group (Ini opened editable). */
void Config_WriteKeys(Ini *ini, const int *keys)
{
    Ini_SetValuef(ini, "Jump", "%i", keys[KEY_JUMP]);
    Ini_SetValuef(ini, "Crouch", "%i", keys[KEY_CROUCH]);
    Ini_SetValuef(ini, "Left", "%i", keys[KEY_LEFT]);
    Ini_SetValuef(ini, "Right", "%i", keys[KEY_RIGHT]);
    Ini_SetValuef(ini, "A", "%i", keys[KEY_A]);
    Ini_SetValuef(ini, "B", "%i", keys[KEY_B]);
    Ini_SetValuef(ini, "C", "%i", keys[KEY_C]);
    Ini_SetValuef(ini, "X", "%i", keys[KEY_X]);
    Ini_SetValuef(ini, "Y", "%i", keys[KEY_Y]);
    Ini_SetValuef(ini, "Z", "%i", keys[KEY_Z]);
    Ini_SetValuef(ini, "Start", "%i", keys[KEY_START]);
}

/* Unassigned entries (0) of a key map get the given "none" value. */
static void Config_FillUnassigned(int *keys, int none)
{
    int i;

    for (i = 1; i < KEY_COUNT; i++) {
        if (keys[i] == 0) {
            keys[i] = none;
        }
    }
}

/*
 * Fix (port change): the RenderMode the config asked for, kept apart from
 * g_game->render_mode.
 *
 * Platform policy rewrites g_game->render_mode to the mode that will really
 * run (Android's capability fallback below; the Vita's recovery lives in
 * system.c). By the time System_Init logs "requested=... effective=..." and
 * enforces MUGEN_STRICT_RENDERER, the request was therefore gone: a cfg of
 * OpenGL ran as System, logged requested=system, and strict mode never failed.
 * -1 means "not parsed yet"; RENDER_SYSTEM is 0, so it cannot be the sentinel.
 */
static int g_configuredRenderMode = -1;

int Config_RequestedRenderMode(void)
{
    return g_configuredRenderMode;
}

/*
 * 0x0045DE80: loads data/mugen.cfg into the game variables. Returns 0 (with
 * an error message) on a missing mandatory group or an invalid value; as in
 * the original the Ini is then not freed.
 */
int Config_Load(void)
{
    GameVars *g;
    char text[0x104];
    const char *value;
    int width, height;
    int flag;
    Stream *s;
    Ini *ini = NULL;
    int c, i;

    s = Stream_OpenFile(CONFIG_PATH, "rt");
    if (s != NULL) {
        ini = Ini_Parse(s, INI_MODE_READ);
        Stream_Close(s);
    }
    if (ini == NULL) {
        Error_Printf("Can't open config file %s.\n", CONFIG_PATH);
        return 0;
    }

    /* [Rules] */
    if (Ini_SelectGroup(ini, "Rules") == -1) {
        Error_Printf("Can't access [Rules]\n");
        return 0;
    }
    value = Ini_GetValue(ini, "gametype");
    if (value == NULL) {
        Error_Printf("Can't get gametype.\n");
        return 0;
    }
    c = Char_ToLower(*value);
    g = g_game;
    if (c == 'm') {
        g->game_type_m = 1;
    } else if (c == 'v') {
        g->game_type_m = 0;
    } else {
        Error_Printf("Invalid gametype: %s\n", value);
        return 0;
    }
    g->attack_life_to_power_mul = 3.0f;
    g->gethit_life_to_power_mul = 2.0f;
    g->super_target_defence_mul = 1.0f;
    value = Ini_GetValue(ini, "default.attack.lifetopowermul");
    if (value != NULL) {
        g->attack_life_to_power_mul = (float)atof(value);
    }
    value = Ini_GetValue(ini, "default.gethit.lifetopowermul");
    if (value != NULL) {
        g_game->gethit_life_to_power_mul = (float)atof(value);
    }
    value = Ini_GetValue(ini, "super.targetdefencemul");
    if (value != NULL) {
        g_game->super_target_defence_mul = (float)atof(value);
    }

    /* [Config] */
    if (Ini_SelectGroup(ini, "config") == -1) {
        Error_Printf("Can't access [Config]\n");
        return 0;
    }
    g = g_game;
    g->game_speed = 60;
    g->draw_shadows = 1;
    width = 640;
    height = 480;
    text[0] = '\0';
    Ini_ReadString(ini, "language", 0x100, text);
    if (text[0] == '\0') {
        const char *lang;
#ifdef _WIN32
        if (GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, g_systemLanguage, 3) != 0) {
#else
        /* POSIX has no locale-info call of this shape; the LANG/LC_ALL
         * environment variables are the equivalent source of a language code. */
        if (Compat_GetSystemLanguage(g_systemLanguage, 3)) {
#endif
            Log_Printf("Autodetected system language. ");
            lang = g_systemLanguage;
        } else {
            Log_Printf("Unable to detect system language. ");
            lang = "en";
        }
        text[0] = '\0';
        strncat(text, lang, 0xFF);
    }
    Log_Printf("Setting language \"%s\".\n", text);
    Lang_SetCode(text);
    Ini_GetInt(ini, "gamewidth", &width);
    Ini_GetInt(ini, "gameheight", &height);
    g = g_game;
    g->game_width = width;
    g->game_height = height;
    g->game_coord[0] = (double)width;
    g->game_coord[1] = (double)height;
    g->game_coord[2] = (double)((width - 1) / 2);
    g->game_coord[3] = (double)((height - 1) / 2);
    memcpy(g_gameCoord, g->game_coord, sizeof(g_gameCoord));
    Ini_GetInt(ini, "gamespeed", &g->game_speed);
    Ini_GetInt(ini, "drawshadows", &g_game->draw_shadows);
    Ini_GetInt(ini, "afterimagemax", &g_game->afterimage_max);
    if (g_game->afterimage_max < 1) {
        g_game->afterimage_max = 1;
    }
    Ini_GetInt(ini, "layeredspritemax", &g_game->layered_sprite_max);
    if (g_game->layered_sprite_max < 32) {
        g_game->layered_sprite_max = 32;
    }
    Ini_GetInt(ini, "spritedecompressionbuffersize", &g_game->sprite_decompression_buffer_size);
    if (g_game->sprite_decompression_buffer_size < 128) {
        g_game->sprite_decompression_buffer_size = 128;
    }
    Ini_GetInt(ini, "explodmax", &g_game->explod_max);
    if (g_game->explod_max < 8) {
        g_game->explod_max = 8;
    }
    Ini_GetInt(ini, "sysexplodmax", &g_game->sysexplod_max);
    if (g_game->sysexplod_max < 8) {
        g_game->sysexplod_max = 8;
    }
    Ini_GetInt(ini, "helpermax", &g_game->helper_max);
    if (g_game->helper_max < 4) {
        g_game->helper_max = 4;
    }
    if (g_game->helper_max > 56) {
        g_game->helper_max = 56;
    }
    Ini_GetInt(ini, "playerprojectilemax", &g_game->player_projectile_max);
    if (g_game->player_projectile_max < 5) {
        g_game->player_projectile_max = 5;
    }
    Ini_GetInt(ini, "playerpalmax", &g_game->player_pal_max);
    if (g_game->player_pal_max < 12) {
        g_game->player_pal_max = 12;
    }
    Ini_GetInt(ini, "firstrun", &g_game->first_run);

    /* [Debug] */
    if (Ini_SelectGroup(ini, "Debug") != -1) {
        Ini_GetInt(ini, "debug", &g_game->debug);
        Ini_GetInt(ini, "allowdebugmode", &g_game->allow_debug_mode);
        Ini_GetInt(ini, "allowdebugkeys", &g_game->allow_debug_keys);
        if (g_game->debug) {
            g_game->allow_debug_mode = 1;
        }
        Ini_GetInt(ini, "speedup", &g_game->speedup);
        value = Ini_GetValue(ini, "startstage");
        if (value != NULL) {
            strncpy(g_game->start_stage, value, sizeof(g_game->start_stage));
        }
        Ini_GetInt(ini, "HideDevelopmentBuildBanner", &g_game->hide_dev_banner);
        /*
         * [Debug] ShowFPS: the source comment on Game_DrawFps names this
         * option, but it was never read anywhere -- only the console's "fps"
         * command could turn the counter on, and the console needs a backquote
         * key. On the Vita there is no keyboard, so the frame rate was
         * effectively unmeasurable. Reading the option makes it reachable from
         * mugen.cfg, which the Vita build can patch.
         */
        Ini_GetInt(ini, "showfps", &g_game->show_fps);
    }

    /* [Video] */
    if (Ini_SelectGroup(ini, "video") == -1) {
        Error_Printf("Can't access [Video]\n");
        return 0;
    }
    g = g_game;
    g->video_width = width;
    g->video_flags = 0;
    g->video_height = height;
    g->keep_aspect = 1;
    g->filter_indexed = 1;
    Ini_GetInt(ini, "width", &g->video_width);
    Ini_GetInt(ini, "height", &g_game->video_height);
    Ini_GetInt(ini, "depth", &g_game->depth);
    Ini_GetInt(ini, "vretrace", &g_game->vretrace);
    Ini_GetInt(ini, "keepaspect", &g_game->keep_aspect);
    Ini_GetInt(ini, "filterindexed", &g_game->filter_indexed);
    Ini_GetInt(ini, "fullscreen", &g_game->fullscreen);
    g = g_game;
    if (g->fullscreen == 0) {
        g->video_flags &= ~VIDEO_FULLSCREEN;
    } else {
        g->video_flags |= VIDEO_FULLSCREEN;
    }
    g->render_mode = RENDER_SYSTEM;
    value = Ini_GetValue(ini, "rendermode");
    if (value != NULL) {
        if (_stricmp(value, "system") == 0) {
            g_game->render_mode = RENDER_SYSTEM;
        } else if (_stricmp(value, "opengl") == 0) {
            g_game->render_mode = RENDER_OPENGL;
        } else if (_stricmp(value, "openglscreen") == 0) {
            g_game->render_mode = RENDER_OPENGL_SCREEN;
        } else if (_stricmp(value, "directx") == 0) {
            /* M6 (plan section 10.3): DirectX is a System-compatibility alias.
             * The engine draws the same software frames; only the SDL 1.2
             * video driver hint differs (see Video_SetMode). It is not a
             * Direct3D sprite renderer and never selects GPU code paths. */
            g_game->video_flags |= VIDEO_DIRECTX;
            g_game->render_mode = RENDER_SYSTEM;
        } else {
            Error_Printf("Invalid RenderMode: %s\n", value);
            return 0;
        }
    }
    /* The request as written, before any platform fallback rewrites it. */
    g_configuredRenderMode = g_game->render_mode;
    g = g_game;
    if (g->render_mode == RENDER_OPENGL) {
        Ini_GetInt(ini, "safemode", &g->safe_mode);
    }
    value = Ini_GetValue(ini, "blitmode");
    if (value != NULL && (c = Char_ToLower(*value)) != 'n') {
        if (c != 'p') {
            Error_Printf("Invalid BlitMode: %s\n", value);
            return 0;
        }
        g_game->video_flags |= VIDEO_HWBLIT;
    }
    flag = 0;
    Ini_GetInt(ini, "resizable", &flag);
    if (flag) {
        g_game->video_flags |= VIDEO_RESIZABLE;
    }
    Ini_GetInt(ini, "stagefit", &g_game->stage_fit);
    Ini_GetInt(ini, "systemfit", &g_game->system_fit);
    g = g_game;
#if defined(ANDROID_PORT) || defined(MUGEN_GLES)
    /*
     * Renderer availability on the GLES backends (RENDERING_PLAN.md 12.2 and
     * 12.7; Linux ARM64 per PROBLEMS.md P1).
     *
     * The decision goes through RenderPolicy_Resolve so there is one table
     * instead of an ad-hoc #ifdef. The capabilities are what this target
     * actually compiles: glrender.c/gldetect.c are excluded from every GLES
     * build, so the GL sprite tables and the OpenGLScreen presenter come from
     * src/gles_render.c or not at all.
     *
     * Selecting a mode here is a claim about the build only; whether the driver
     * can honour it is decided after the context exists (Gl_Init probes, links
     * the ES programs and requires a complete framebuffer), and system.c falls
     * back to System with a logged reason when any of that fails. Reporting 0
     * here would hide a working GLES path; claiming it without validating it is
     * the "removing the System force alone picks an incomplete path" failure
     * section 12.2 warns about.
     */
    {
        RenderCaps caps;
        RenderDecision decision;

        caps.system = 1;
#ifdef MUGEN_GLES
        /*
         * gl_screen stays 0: OpenGLScreen blits the CPU framebuffer into a
         * placeholder surface and the frame boundary then swaps the GL window
         * without ever uploading that surface, so the mode would present a
         * black window. No GLES target has a CPU-framebuffer upload presenter
         * yet (src/gfx.c Gfx_PresentGlScreen is desktop-only), so it is not
         * advertised anywhere. gl_sprite is the mode that actually works --
         * the screen buffer is a real render target.
         */
        caps.gl_screen = 0;
        caps.gl_sprite = 1;
        /*
         * gl_present: the sprite programs and FBO path exist, which is what the
         * OpenGL mode needs. It is still only a claim about the build -- Gl_Init
         * validates it against the real driver and system.c falls back if it
         * fails. gl_screen above stays 0 because the CPU-framebuffer upload
         * presenter does not exist; those are different questions, and
         * conflating them made every OpenGL request fall back to System.
         */
        caps.gl_present = 1;
#else
        caps.gl_screen = 0;
        caps.gl_sprite = 0;
        caps.gl_present = 0;
#endif
        RenderPolicy_Resolve(g->render_mode, &caps, &decision);
        if (decision.fell_back) {
            Log_Printf("RenderMode %d unavailable (%s); using System.\n", g->render_mode,
                       (decision.reason != NULL) ? decision.reason : "unspecified");
            g->render_mode = decision.effective;
        }
    }
#endif
    i = g->render_mode;
    if (i == RENDER_SYSTEM || i == RENDER_OPENGL_SCREEN) {
        g->depth = 16;
        g->stage_fit = 0;
        g->system_fit = 0;
    }
    if (i == RENDER_OPENGL) {
        g->video_flags |= VIDEO_HWBLIT;
        g->depth = 32;
    }
    if (g->safe_mode) {
        g->video_flags &= ~VIDEO_RESIZABLE;
        g->stage_fit = 0;
        g->system_fit = 0;
        if (i == RENDER_OPENGL) {
            g->safe_mode_opengl = 1;
        }
    }

    /* [Sound] */
    if (Ini_SelectGroup(ini, "sound") == -1) {
        Error_Printf("Can't access [Sound]\n");
        return 0;
    }
    g = g_game;
    g->wav_volume = 900.0;      /* overwritten below: only the default argument counts */
    g->sound_enabled = 1;
    g->master_volume = 1000.0;
    g->stereo_effects = 1;
    g->panning_width = 160;
    g->bgm_volume = 800.0;
    Ini_GetInt(ini, "sound", &g->sound_enabled);
    Ini_GetInt(ini, "stereoeffects", &g_game->stereo_effects);
    Ini_GetInt(ini, "panningwidth", &g_game->panning_width);
    if (Ini_GetInt(ini, "reversestereo", &flag)) {
        g_stereoDirection = flag == 0 ? 1 : -1;
    }
    g_game->sfx_resample_method = 1;
    value = Ini_GetValue(ini, "SFXResampleMethod");
    if (value != NULL) {
        if (_stricmp(value, "libresample") == 0) {
            g_game->sfx_resample_method = 1;
        } else if (_stricmp(value, "SDL") == 0) {
            g_game->sfx_resample_method = 0;
        }
    }
    if (g_game->sfx_resample_method == 1) {
        Ini_GetIntDef(ini, "SFXResampleQuality", &g_game->sfx_resample_quality, 0);
    }
    g_game->bgm_resample_method = 1;
    value = Ini_GetValue(ini, "BGMResampleMethod");
    if (value != NULL) {
        if (_stricmp(value, "libresample") == 0) {
            g_game->bgm_resample_method = 1;
        } else if (_stricmp(value, "SDL") == 0) {
            g_game->bgm_resample_method = 0;
        }
    }
    if (g_game->bgm_resample_method == 1) {
        Ini_GetIntDef(ini, "BGMResampleQuality", &g_game->bgm_resample_quality, 0);
    }
    Ini_GetDoubleDef(ini, "mastervolume", &g_game->master_volume, 100.0);
    Ini_GetDoubleDef(ini, "wavvolume", &g_game->wav_volume, 100.0);
    Ini_GetDoubleDef(ini, "bgmvolume", &g_game->bgm_volume, 100.0);
    Ini_GetIntDef(ini, "samplerate", &g_game->sample_rate, 44100);
    Ini_GetIntDef(ini, "channels", &g_game->channels, 2);
    Ini_GetIntDef(ini, "bitdepth", &g_game->bit_depth, 16);
    Ini_GetIntDef(ini, "buffersize", &g_game->buffer_size, 11);
    g = g_game;
    g->unused_128E8 = -1;
    g->wav_channels = 8;
    Ini_GetInt(ini, "wavchannels", &g->wav_channels);
    g_game->unused_128EC = -1;
    if (Ini_SelectGroup(ini, "music") == -1) {
        g_game->music_plugins = NULL;
    } else {
        g_game->music_plugins = BgmPlugins_Load(ini);
    }

    /* [Misc] */
    if (Ini_SelectGroup(ini, "Misc") == -1) {
        Error_Printf("Can't access [Misc]\n");
        return 0;
    }
    g_game->player_cache = 0;
    Ini_GetInt(ini, "playercache", &g_game->player_cache);
    g_game->player_cache += 2;
    g_game->precache = 0;
    Ini_GetInt(ini, "precache", &g_game->precache);
    if (g_game->render_mode == RENDER_OPENGL) {
        g_game->precache = 0;
    }
    if (Ini_GetInt(ini, "bufferedread", &flag)) {
        g_bufferedRead = flag;
        g_sndBufferedRead = flag;
        g_game->buffered_read = flag;
    }
    Ini_GetInt(ini, "unloadsystem", &g_game->unload_system);
    Ini_GetInt(ini, "pauseondefocus", &g_game->pause_on_defocus);
    g_game->sfx_background_mode = 1;
    value = Ini_GetValue(ini, "sfxbackgroundmode");
    if (value != NULL) {
        if (_stricmp(value, "mute") == 0) {
            g_game->sfx_background_mode = 0;
        } else if (_stricmp(value, "play") == 0) {
            g_game->sfx_background_mode = 1;
        }
    }
    g_game->bgm_background_mode = 2;
    value = Ini_GetValue(ini, "bgmbackgroundmode");
    if (value != NULL) {
        if (_stricmp(value, "pause") == 0) {
            g_game->bgm_background_mode = 0;
        } else if (_stricmp(value, "play") == 0) {
            g_game->bgm_background_mode = 2;
        } else if (_stricmp(value, "mute") == 0) {
            g_game->bgm_background_mode = 1;
        }
    }
    Ini_GetFloat(ini, "forcestagezoomout", &g_game->force_stage_zoomout);

    /* [Arcade] */
    if (Ini_SelectGroup(ini, "Arcade") == -1) {
        Error_Printf("Can't access [Arcade]\n");
        return 0;
    }
    Ini_GetInt(ini, "ai.randomcolor", &g_game->ai_random_color);
    Ini_GetInt(ini, "ai.cheat", &g_game->ai_cheat);
    for (i = 0; i < 3; i++) {
        AiRamp *ramp = &g_game->ai_ramp[i];
        sprintf(text, "%s.AIramp.start", ai_ramp_modes[i]);
        Ini_Scanf(ini, text, "%i,%i", &ramp->start_match, &ramp->start_offset);
        sprintf(text, "%s.AIramp.end", ai_ramp_modes[i]);
        Ini_Scanf(ini, text, "%i,%i", &ramp->end_match, &ramp->end_offset);
    }

    /* [Input] */
    if (Ini_SelectGroup(ini, "Input") == -1) {
        Error_Printf("Can't access [Input]\n");
        return 0;
    }
    Ini_GetInt(ini, "p1.usekeyboard", &g_game->use_keyboard[0]);
    Ini_GetInt(ini, "p2.usekeyboard", &g_game->use_keyboard[1]);
    Ini_GetInt(ini, "p1.joystick.type", &g_game->joystick_type[0]);
    Ini_GetInt(ini, "p2.joystick.type", &g_game->joystick_type[1]);
    g = g_game;
    g->use_joystick[0] = g->joystick_type[0] != 0;
    g->use_joystick[1] = g->joystick_type[1] != 0;
    g->force_feedback = g->use_joystick[0] || g->use_joystick[1];
    Ini_GetInt(ini, "forcefeedback", &g->force_feedback);
    Ini_GetInt(ini, "psxdelay", &g_game->psx_delay);
    Ini_GetInt(ini, "inputdirmode", &g_game->input_dir_mode);
    Ini_GetInt(ini, "anykeyunpauses", &g_game->any_key_unpauses);
    if (g_game->use_keyboard[0] && !Config_ReadKeys(ini, "p1 keys", g_game->keys[0])) {
        Error_Printf("Can't read p1 key config.\n");
        return 0;
    }
    g = g_game;
    Config_FillUnassigned(g->keys[0], KEY_NONE);
    if (g->use_keyboard[1]) {
        int ok = Config_ReadKeys(ini, "p2 keys", g_game->keys[1]);
        g = g_game;
        if (!ok) {
            Error_Printf("Can't read p2 key config.\n");
            return 0;
        }
    }
    Config_FillUnassigned(g->keys[1], KEY_NONE);
    g->joy_keys[0][KEY_JUMP] = 1;
    g->joy_keys[0][KEY_CROUCH] = 2;
    g->joy_keys[0][KEY_LEFT] = 3;
    g->joy_keys[0][KEY_RIGHT] = 4;
    memcpy(g->joy_keys[1], g->joy_keys[0], sizeof(g->joy_keys[1]));
    sprintf(text, "P1 joystick");
    Config_ReadKeys(ini, text, g_game->joy_keys[0]);
    sprintf(text, "P2 joystick");
    Config_ReadKeys(ini, text, g_game->joy_keys[1]);
#ifdef MUGEN_VITA_GL
    /* The platform joystick policy; see Config_ApplyVitaJoystick. Applied here
     * and again after every Options_Load, which rereads the sections below. */
    Config_ApplyVitaJoystick();
#endif
    g = g_game;
#ifdef MUGEN_GAMEPAD_POLICY
    /*
     * Keep the [P<n> Joystick] codes as configured. Config_ApplyJoystickSlot
     * overwrites joy_keys with the semantic controller layout for a slot that
     * has a controller, and a slot that loses its controller (unplugged, or
     * never had one) has to fall back to these raw codes. Without the snapshot
     * the raw bindings were simply gone, so a detach left the player bound to
     * codes no device would ever send.
     */
    memcpy(g_cfgRawJoyKeys[0], g->joy_keys[0], sizeof(g_cfgRawJoyKeys[0]));
    memcpy(g_cfgRawJoyKeys[1], g->joy_keys[1], sizeof(g_cfgRawJoyKeys[1]));
#endif
    memcpy(g->joy_keys_active[0], g->joy_keys[0], sizeof(g->joy_keys_active[0]));
    memcpy(g->joy_keys_active[1], g->joy_keys[1], sizeof(g->joy_keys_active[1]));
    if (!g->use_joystick[0]) {
        memset(g->joy_keys_active[0], 0, sizeof(g->joy_keys_active[0]));
    }
    if (!g->use_joystick[1]) {
        memset(g->joy_keys_active[1], 0, sizeof(g->joy_keys_active[1]));
    }
    Config_FillUnassigned(g->joy_keys_active[0], JOY_NONE);
    Config_FillUnassigned(g->joy_keys_active[1], JOY_NONE);
    Config_FillUnassigned(g->joy_keys[0], JOY_NONE);
    Config_FillUnassigned(g->joy_keys[1], JOY_NONE);
    Ini_Free(ini);

    /* Clear FirstRun once it has been seen. */
    if (g_game->first_run) {
        ini = Ini_OpenEditable(CONFIG_PATH);
        if (ini != NULL) {
            Ini_SelectGroup(ini, "Config");
            Ini_SetValuef(ini, "FirstRun", "0");
            Ini_Save(ini, CONFIG_PATH);
            Ini_Free(ini);
        }
    }
    Ini_SetLanguage(g_language);
    return 1;
}

/* 0x0045F100: option defaults; the volume percentages come from the configured volumes. */
void Options_SetDefaults(void)
{
    GameVars *g = g_game;

    g->difficulty = 4;
    g->life = 100;
    g->time = 99;
    g->option_game_speed = 0;
    g->master_volume_pct = (int)(g->master_volume + 0.5);
    g->wav_volume_pct = (int)(g->wav_volume + 0.5);
    g->bgm_volume_pct = (int)(g->bgm_volume + 0.5);
}

/* Options menu encoding of a volume: 101 for 100%, -1 for 0%. */
static int Options_VolumeToPct(double volume)
{
    if (volume == 100.0) {
        return 101;
    }
    if (volume == 0.0) {
        return -1;
    }
    return (int)(volume + 0.5);
}

/* Inverse of Options_VolumeToPct, keeping the fractional part of the current volume. */
static double Options_PctToVolume(int pct, double volume)
{
    if (pct == -1) {
        return 0.0;
    }
    if (pct == 101) {
        return 100.0;
    }
    return (volume - floor(volume + 0.5)) + pct;
}

#ifdef MUGEN_GAMEPAD_POLICY
/*
 * Fix (port change): a pad is bound through SDL's game-controller semantics on
 * every SDL2 target, not by translating raw device indices.
 *
 * [P<n> Joystick] in the shipped mugen.cfg is a PC DirectInput pad's numbering:
 * Jump=9, Crou=10, Left=11, Right=12, A=13, B=14, X=15, Y=16, Z=18, C=20,
 * Start=24. Those are not button indices -- the engine derives an engine code
 * from the device's own topology:
 *
 *     axis n   -> n*2 + 1 (negative) / + 2 (positive)
 *     hat      -> axes*2 + 1..4 (up, down, left, right)
 *     button b -> axes*2 + hats*4 + 1 + b
 *
 * so those values only mean what they say on a pad with 4 axes and 1 hat. A
 * real controller does not match: the tested Xbox Wireless Controller reports
 * axes=8 hats=0, so its face buttons start at engine code 17 while the config's
 * A/B/X/Y (13..16) are axis codes there -- and its d-pad arrives as buttons at
 * code 32+, past the 32 codes a pad slot holds, so those were dropped. The pad
 * opened, the left stick steered, and every attack button was dead.
 *
 * Rather than try to map one topology onto another, the pad is bound as a game
 * controller (Pads_HandleControllerEvent): A/B/X/Y, the d-pad, the shoulders
 * and Start mean the same thing on every device, and the compact 1..11 layout
 * fits a slot with room to spare.
 *
 * The group that configures it is [P<n> Gamepad], read here:
 *
 *   [P1 Gamepad]
 *   Jump  = 1      ; up
 *   Crouch= 2      ; down
 *   Left  = 3
 *   Right = 4
 *   A     = 5
 *   B     = 6
 *   C     = 7      ; RB
 *   X     = 8
 *   Y     = 9
 *   Z     = 10     ; LB
 *   Start = 11
 *
 * A missing group falls back to exactly that layout, so an untouched mugen.cfg
 * still works. Only codes the controller path can deliver (1..11) are honoured;
 * anything else is reported and ignored, because a larger number here no longer
 * means "the next button" -- it means nothing on this path.
 */
/* Reads [P1 Gamepad] / [P2 Gamepad] into the sidecar tables. A group that is
 * not there leaves its slot absent, which means "use the built-in layout". */
static void Config_LoadGamepadGroups(Ini *ini)
{
    int player;

    g_cfgGamepadMissing = 0;
    for (player = 0; player < 2; player++) {
        char group[16];
        int i;

        for (i = 0; i < KEY_COUNT; i++) {
            g_cfgGamepadKeys[player][i] = JOY_NONE;
        }
        snprintf(group, sizeof(group), "P%d Gamepad", player + 1);
        g_cfgGamepadPresent[player] = Config_ReadKeys(ini, group, g_cfgGamepadKeys[player]);
        if (!g_cfgGamepadPresent[player]) {
            g_cfgGamepadMissing = 1;
            for (i = 0; i < (int)(sizeof(gamepad_bindings) / sizeof(gamepad_bindings[0])); i++) {
                g_cfgGamepadKeys[player][gamepad_bindings[i].key] = gamepad_bindings[i].code;
            }
            Log_Printf("config: no [%s] group; the built-in gamepad layout applies\n", group);
        }
        Config_FillUnassigned(g_cfgGamepadKeys[player], JOY_NONE);
    }
}

/*
 * Port fix: save canonical gamepad codes into their own sections. Existing
 * comments and unknown keys stay in place; missing sections and keys are added.
 */
static int Config_SaveGamepadGroup(Ini *ini, int player)
{
    char group[32];
    char line[64];
    int present, i;

    snprintf(group, sizeof(group), "P%d Gamepad", player + 1);
    present = Ini_SelectGroup(ini, group) != -1;
    if (!present) {
        if (!Ini_AppendLine(ini, "")) {
            return 0;
        }
        snprintf(line, sizeof(line), "[%s]", group);
        if (!Ini_AppendLine(ini, line)) {
            return 0;
        }
    }
    for (i = 0; i < (int)(sizeof(gamepad_bindings) / sizeof(gamepad_bindings[0])); i++) {
        const char *name = gamepad_bindings[i].name;
        int value = g_cfgGamepadKeys[player][gamepad_bindings[i].key];

        if (value == JOY_NONE) {
            value = 0;
        }
        if (present && Ini_GetValue(ini, name) != NULL) {
            if (Ini_SetValuef(ini, name, "%i", value) != 1) {
                return 0;
            }
        } else {
            snprintf(line, sizeof(line), "%-6s = %d", name, value);
            if (present ? !Ini_AppendGroupLine(ini, line) : !Ini_AppendLine(ini, line)) {
                return 0;
            }
        }
    }
    return 1;
}

/* Make missing groups visible at startup, using the loaded/default mappings. */
static void Config_WriteMissingGamepadGroups(void)
{
    Ini *ini;
    int player;

    if (!g_cfgGamepadMissing) {
        return;
    }
    ini = Ini_OpenEditable(CONFIG_PATH);
    if (ini == NULL) {
        Log_Printf("config: could not reopen %s to add the gamepad groups\n", CONFIG_PATH);
        return;
    }
    for (player = 0; player < 2; player++) {
        if (!g_cfgGamepadPresent[player] && !Config_SaveGamepadGroup(ini, player)) {
            Log_Printf("config: could not add the gamepad groups\n");
            Ini_Free(ini);
            return;
        }
    }
    if (!Ini_Save(ini, CONFIG_PATH)) {
        Log_Printf("config: could not write the gamepad groups to %s\n", CONFIG_PATH);
    }
    Ini_Free(ini);
}

/*
 * Port fix: commit the key menu to the appropriate mapping before publishing
 * runtime bindings. A semantic code is local in the cfg (P2's runtime 37 is 5).
 */
int Config_SetJoystickKeys(int player, int gamepad, const int *keys)
{
    int local[KEY_COUNT];
    int i;

    if (player < 0 || player >= 2) {
        return 0;
    }
    if (gamepad) {
        for (i = 0; i < KEY_COUNT; i++) {
            int value = keys[i];

            local[i] = JOY_NONE;
            if (value == JOY_NONE || value == 0) {
                continue;
            }
            if (value < player * PAD_BUTTONS + 1 ||
                value > player * PAD_BUTTONS + PAD_GAMEPAD_LAST) {
                return 0;
            }
            local[i] = value - player * PAD_BUTTONS;
        }
        memcpy(g_cfgGamepadKeys[player], local, sizeof(local));
        g_cfgGamepadPresent[player] = 1;
    } else {
        memcpy(g_cfgRawJoyKeys[player], keys, sizeof(g_cfgRawJoyKeys[player]));
    }
    Config_ApplyJoystickSlot(player);
    return 1;
}

void Config_ApplyJoystickBindings(void)
{
    int player;

    if (Pads_ControllerCount() == 0) {
        /* No pad SDL can map: [P<n> Joystick] stays in force, and its raw codes
         * are the only ones that can work. */
        Log_Printf("config: no game controller; [P<n> Joystick] left unchanged\n");
        return;
    }
    for (player = 0; player < 2; player++) {
        Config_ApplyJoystickSlot(player);
    }
}

/*
 * Applies the binding policy for one player.
 *
 * Split out of Config_ApplyJoystickBindings so a controller that is plugged in
 * after start-up can be bound then and there (Pads_AttachController). Doing
 * all slots on a hotplug was wrong: Pads_ControllerBindings returns JOY_NONE
 * for a slot with no controller, so plugging in player 2's pad would have
 * blanked player 1's bindings.
 *
 * A slot with no controller is left completely alone -- neither its joy_keys
 * nor its active map is touched, so the raw [P<n> Joystick] codes survive.
 */
void Config_ApplyJoystickSlot(int player)
{
    int *jk;
    const int *cfg;
    int used_default = 0;
    int i;

    if (player < 0 || player >= 2) {
        return;
    }
    jk = g_game->joy_keys[player];

    if (!Pads_SlotUsesController(player)) {
        /*
         * No controller in this slot: the raw [P<n> Joystick] codes are the
         * only ones that can work. Restore the snapshot taken at load time,
         * because an earlier attach may have replaced them with the semantic
         * layout.
         */
        memcpy(jk, g_cfgRawJoyKeys[player], sizeof(g_cfgRawJoyKeys[player]));
        if (g_game->use_joystick[player]) {
            memcpy(g_game->joy_keys_active[player], jk, sizeof(g_game->joy_keys_active[player]));
        }
        Log_Printf("config: P%d has no game controller; raw [P%d Joystick] bindings in force\n",
                   player + 1, player + 1);
        return;
    }

    /*
     * use_joystick is runtime state, not the preference: Input_InitPads clears
     * it when no pad can be opened at start-up. The preference is
     * joystick_type, read from the cfg (and written by the options screen). A
     * controller plugged in later must be usable without restarting, so the
     * slot is re-enabled here -- otherwise the semantic bindings were
     * configured but never published: a pad-less start-up reported
     * enabled=0, configured=5, active=13 for button A and pressed nothing.
     */
    if (g_game->joystick_type[player] != 0 && !g_game->nojoy) {
        g_game->use_joystick[player] = 1;
        g_game->force_feedback = g_game->use_joystick[0] || g_game->use_joystick[1];
    }

    cfg = g_cfgGamepadKeys[player];
    used_default = !g_cfgGamepadPresent[player];
    for (i = 0; i < KEY_COUNT; i++) {
        int value = cfg[i];

        jk[i] = JOY_NONE;
        if (value == JOY_NONE) {
            continue;
        }
        if (value < 1 || value > PAD_GAMEPAD_LAST) {
            Log_Printf("config: [P%d Gamepad] key %d = %d is outside the gamepad range "
                       "1..%d; ignored\n", player + 1, i, value, PAD_GAMEPAD_LAST);
            continue;
        }
        jk[i] = player * PAD_BUTTONS + value;
    }
    /*
     * Mirror into the active map. Input_InitPads copies joy_keys ->
     * joy_keys_active at its start, i.e. before the pads were opened, and this
     * policy runs after that (the pad's topology is only known once it is
     * open).
     */
    if (g_game->use_joystick[player]) {
        memcpy(g_game->joy_keys_active[player], jk, sizeof(g_game->joy_keys_active[player]));
    }
    Log_Printf("config: P%d bound through the game controller (%s)\n", player + 1,
               used_default ? "built-in layout, no [P<n> Gamepad] group" : "[P<n> Gamepad]");
}

/*
 * Applies the policy for one player and publishes it to the input engine.
 *
 * Separate from Config_ApplyJoystickSlot because that one also runs during
 * start-up, before the key map exists. An attach or detach happens at runtime,
 * so g_keyMap has to be rebuilt from the new active bindings.
 */
void Config_RepublishJoystick(int player)
{
    Config_ApplyJoystickSlot(player);
    /* Port fix: hotplug must not restart Events_Init and drop other players'
     * pending input. Recompose held state against the updated table only. */
    if (!Input_UpdateMap()) {
        Error_Fatal("Cannot update joystick input map\n");
    }
    Input_ResetState();
}

#endif /* MUGEN_GAMEPAD_POLICY */

#ifdef MUGEN_VITA_GL
/*
 * Port addition: the Vita joystick mapping, as a function so every place that
 * reloads the bindings can reapply it.
 *
 * It used to live inline in Config_Load only. Options_Load rereads both
 * [P1 Joystick] and [P2 Joystick] from mugen.cfg afterwards, and on the Vita
 * that silently replaced the mapping with the shipped 24-button PC pad values:
 * Jump 17 came back as 9, A as 13, Start as 24 -- and 24 does not exist on the
 * Vita driver at all. Applying the policy after every reload, including the
 * one at start-up, keeps the documented layout in force.
 */
static void Config_ApplyVitaJoystick(void)
{
    GameVars *g = g_game;

/*
 * PS Vita port (port/psvita): replace the joystick bindings with the real
 * *engine codes*, not the SDL driver indices.
 *
 * The shipped mugen.cfg binds [P1 Joystick] to indices 9..24, which is a
 * 24-button PC pad. SDL's Vita joystick driver reports only 12 buttons, and
 * in a different order, so those bindings make Right jump, Left crouch,
 * Start walk left, and leave every attack button pointing at a button that
 * does not exist.
 *
 * Indices from SDL_sysjoystick.o in VitaSDK's libSDL.a, confirmed on
 * hardware with port/psvita/input-mapper (12 buttons, 4 axes, 0 hats -- the
 * d-pad is buttons, not a hat):
 *
 *   0 Triangle  1 Circle  2 Cross   3 Square  4 L    5 R
 *   6 Down      7 Left    8 Up      9 Right  10 Select  11 Start
 *
 * But Pads_HandleEvent never delivers a raw index. It computes
 *     code = slot * PAD_BUTTONS + axes*2 + hats*4 + 1 + raw_index,
 * so with the Vita driver's 4 axes and 0 hats, Triangle (raw 0) arrives as
 * code 9. Storing the raw index therefore never matches: Triangle would be
 * B=0, which translates to engine code 0 instead of Triangle's 9. (Zero is
 * not JOY_NONE=0x80; it is simply a code nothing delivers.)
 *
 * The table below is RAW_INDEX + 9 for this topology: 9 Triangle, 10 Circle,
 * 11 Cross, 12 Square, 13 L, 14 R, 15 Down, 16 Left, 17 Up, 18 Right,
 * 19 Select, 20 Start. Do NOT hardcode the +9 anywhere else: it is 1 +
 * axes*2 + hats*4 for the enumerated device, and a different controller
 * topology produces different codes.
 *
 * Layout is the conventional PS one: Square/Triangle/Circle/Cross = A/B/C/X.
 * Overriding here rather than rewriting the config keeps the patch
 * deterministic and leaves the shipped file's [P1 Joystick] text intact.
 */
{
    /*
     * Written by name, not positionally: the KEY_* enum is
     * JUMP, RIGHT, CROUCH, LEFT, A..Z, START -- note RIGHT before CROUCH,
     * so an initializer written in reading order would silently swap the
     * vertical and horizontal directions.
     *
     * Values are engine button codes for the 4-axis/0-hat driver (raw index
     * + 9), not the SDL indices. See the comment above.
     */
    /*
     * Fix (port change): the loop used to give both players the same joystick
     * codes. g_keyMap is one global table, so the second player's pass simply
     * overwrote the first: whatever P2 bound to a code became the action for
     * everyone. That is invisible only while both players bind the same action
     * to the same button, which is a coincidence and not a property.
     *
     * The Vita has a single internal pad -- SDL's Vita joystick driver reports
     * one device -- so the pad belongs to player 1 and player 2 gets no
     * joystick at all. Binding P2 to slot 1 instead would leave it matching
     * nothing, since no second pad exists to deliver those codes.
     */
    int player;

    for (player = 0; player < 2; player++) {
        int *jk = g_game->joy_keys[player];
        int i;

        /*
         * Fix (port change): this was memset(jk, JOY_NONE, sizeof(...)), but
         * jk is an array of int and JOY_NONE is 0x80, so every element came
         * out as 0x80808080 (-2139062144) instead of 128. Player 2 -- which
         * the loop leaves at the cleared values -- then displayed a label of
         * "A--2139062144" and eleven more negative bindings in the options
         * screen, and Options_Save wrote them out again. The fill has to be
         * per element because the elements are wider than a byte.
         */
        for (i = 0; i < KEY_COUNT; i++) {
            jk[i] = JOY_NONE;
        }
        if (player != 0) {
            /* One pad: it drives P1 only. */
            continue;
        }
        jk[KEY_JUMP] = 17;     /* d-pad up: raw 8 */
        jk[KEY_RIGHT] = 18;    /* d-pad right: raw 9 */
        jk[KEY_CROUCH] = 15;   /* d-pad down: raw 6 */
        jk[KEY_LEFT] = 16;     /* d-pad left: raw 7 */
        /*
         * Full six-button fight layout. The character select's own
         * overrides (Cross/Square = back, L/R = nothing) are applied per
         * screen in src/events.c, so nothing is lost in a fight.
         */
        jk[KEY_A] = 12;        /* Square: raw 3 */
        jk[KEY_B] = 9;         /* Triangle: raw 0 */
        /*
         * Circle is C. The earlier revision left this unbound because
         * Circle emitted Return, which is also the Start/pause action, so
         * C on Circle would have paused the match mid-punch. Circle now
         * emits the C key itself (see VitaInput_KeyForButton), giving the
         * conventional Square/Triangle/Circle/Cross = A/B/C/X layout.
         */
        jk[KEY_C] = 10;        /* Circle: raw 1 */
        jk[KEY_X] = 11;        /* Cross: raw 2 */
        jk[KEY_Y] = 13;        /* L: raw 4 */
        jk[KEY_Z] = 14;        /* R: raw 5 */
        jk[KEY_START] = 20;    /* Start: raw 11 */
    }
}
}
#endif

/* 0x0045F170: reloads the options screen settings from mugen.cfg and reinitialises input. */
void Options_Load(void)
{
    GameVars *g;
    const char *value;
    Stream *s;
    Ini *ini = NULL;

    s = Stream_OpenFile(CONFIG_PATH, "rt");
    if (s != NULL) {
        ini = Ini_Parse(s, INI_MODE_READ);
        Stream_Close(s);
    }
    if (ini == NULL) {
        Error_Fatal("Error reading %s\n", CONFIG_PATH);
    }
    if (Ini_SelectGroup(ini, "Options") != -1) {
        if ((value = Ini_GetValue(ini, "Difficulty")) != NULL) {
            g_game->difficulty = atoi(value);
        }
        if ((value = Ini_GetValue(ini, "Life")) != NULL) {
            g_game->life = atoi(value);
        }
        if ((value = Ini_GetValue(ini, "Time")) != NULL) {
            g_game->time = atoi(value);
        }
        if ((value = Ini_GetValue(ini, "GameSpeed")) != NULL) {
            g_game->option_game_speed = atoi(value);
        }
        Ini_ReadString(ini, "motif", sizeof(g_game->motif), g_game->motif);
        g = g_game;
        if (g->motif[0] == '\0') {
            strcpy(g->motif, "data/system.def");
        }
        Path_Split(g->motif, g->motif_dir, g->motif_file);
        if ((value = Ini_GetValue(ini, "Team.1VS2Life")) != NULL) {
            g_game->team_1vs2_life = atoi(value);
        }
        if ((value = Ini_GetValue(ini, "Team.LoseOnKO")) != NULL) {
            g_game->team_lose_on_ko = atoi(value);
        }
    }
    g = g_game;
    if (Ini_SelectGroup(ini, "Sound") != -1) {
        g->master_volume_pct = Options_VolumeToPct(g->master_volume);
        g->wav_volume_pct = Options_VolumeToPct(g->wav_volume);
        g->bgm_volume_pct = Options_VolumeToPct(g->bgm_volume);
    }
    g->guard_ko = 1;
    /*
     * Fix (port change): Config_Load honours p<n>.usekeyboard when it reads
     * these sections, but this reload did not, so a player whose keyboard was
     * disabled had its bindings loaded anyway and Input_Remap then used them --
     * the keyboard stayed live. Read a player's keys only when its flag is set,
     * and clear them when it is not, so a reload that disables the keyboard
     * actually takes effect.
     */
    if (g->use_keyboard[0]) {
        Config_ReadKeys(ini, "p1 keys", g->keys[0]);
    } else {
        memset(g->keys[0], 0, sizeof(g->keys[0]));
    }
    if (g->use_keyboard[1]) {
        Config_ReadKeys(ini, "p2 keys", g_game->keys[1]);
    } else {
        memset(g->keys[1], 0, sizeof(g->keys[1]));
    }
    g = g_game;
    Config_FillUnassigned(g->keys[0], KEY_NONE);
    Config_FillUnassigned(g->keys[1], KEY_NONE);
    if (Ini_SelectGroup(ini, "Input") != -1) {
        if ((value = Ini_GetValue(ini, "p1.joystick.type")) != NULL) {
            g_game->option_input[0][1] = atoi(value);
        }
        if ((value = Ini_GetValue(ini, "p2.joystick.type")) != NULL) {
            g_game->option_input[1][1] = atoi(value);
        }
    }
    g = g_game;
    g->joystick_type[0] = g->option_input[0][1];
    g->joystick_type[1] = g->option_input[1][1];
    g->use_joystick[0] = g->option_input[0][1] != 0;
    g->use_joystick[1] = g->option_input[1][1] != 0;
#ifdef MUGEN_GAMEPAD_POLICY
    memcpy(g->joy_keys, g_cfgRawJoyKeys, sizeof(g_cfgRawJoyKeys));
#endif
    Config_ReadKeys(ini, "P1 joystick", g->joy_keys[0]);
    Config_ReadKeys(ini, "P2 joystick", g_game->joy_keys[1]);
#ifdef MUGEN_GAMEPAD_POLICY
    /* [P<n> Gamepad] describes the compact game-controller layout; see
     * Config_ApplyJoystickBindings for why every SDL2 target uses that. */
    Config_LoadGamepadGroups(ini);
    {
        /* Startup only: write the defaults into the file once, so the group is
         * visible and editable instead of being implied. Guarded so the options
         * screen's reloads do not rewrite the config. */
        static int wrote_defaults;

        if (!wrote_defaults) {
            wrote_defaults = 1;
            Config_WriteMissingGamepadGroups();
        }
    }
#endif
    g = g_game;
    Config_FillUnassigned(g->joy_keys[0], JOY_NONE);
    Config_FillUnassigned(g->joy_keys[1], JOY_NONE);
#ifdef MUGEN_GAMEPAD_POLICY
    memcpy(g_cfgRawJoyKeys, g->joy_keys, sizeof(g_cfgRawJoyKeys));
#endif
#ifdef MUGEN_VITA_GL
    /*
     * Fix (port change): this reload rereads both [P1 Joystick] and
     * [P2 Joystick] straight from mugen.cfg, which replaced the Vita mapping
     * Config_Load had installed. Jump came back as 9 instead of 17, A as 13
     * instead of 12, Start as 24 -- a code the Vita driver does not have. The
     * platform policy has to be applied after every configuration reload,
     * including the one at start-up.
     */
    Config_ApplyVitaJoystick();
    g = g_game;
#endif
    Ini_Free(ini);
    Input_InitPads();
#ifdef MUGEN_GAMEPAD_POLICY
    /* After the pads are open (their topology is only known then) and before
     * Input_Remap builds g_keyMap from these codes. */
    Config_ApplyJoystickBindings();
#endif
    if (!Input_Remap()) {
        Error_Fatal("Cannot init input\n");
    }
}

/* Writes a key map, with the "none" value written as 0. */
static void Options_SaveKeys(Ini *ini, const char *group, const int *keys, int none)
{
    int copy[KEY_COUNT];
    int i;

    memcpy(copy, keys, sizeof(copy));
    for (i = 1; i < KEY_COUNT; i++) {
        if (copy[i] == none) {
            copy[i] = 0;
        }
    }
    if (Ini_SelectGroup(ini, group) != -1) {
        Config_WriteKeys(ini, copy);
    }
}

/* 0x0045F5B0: writes the options screen settings back to mugen.cfg. */
void Options_Save(void)
{
    GameVars *g;
    Stream *s;
    Ini *ini = NULL;

    s = Stream_OpenFile(CONFIG_PATH, "rt");
    if (s != NULL) {
        ini = Ini_Parse(s, INI_MODE_EDIT);
        Stream_Close(s);
    }
    if (ini == NULL) {
        Error_Fatal("Error saving %s\n", CONFIG_PATH);
    }
    if (Ini_SelectGroup(ini, "Options") != -1) {
        Ini_SetValuef(ini, "Difficulty", "%i", g_game->difficulty);
        Ini_SetValuef(ini, "Life", "%i", g_game->life);
        Ini_SetValuef(ini, "Time", "%i", g_game->time);
        Ini_SetValuef(ini, "GameSpeed", "%i", g_game->option_game_speed);
        Ini_SetValuef(ini, "Team.1VS2Life", "%i", g_game->team_1vs2_life);
        Ini_SetValuef(ini, "Team.LoseOnKO", "%i", g_game->team_lose_on_ko);
    }
    if (Ini_SelectGroup(ini, "Sound") != -1) {
        double old_bgm, new_bgm;
        g = g_game;
        old_bgm = g->bgm_volume * g->master_volume / 100.0;
        g->master_volume = Options_PctToVolume(g->master_volume_pct, g->master_volume);
        Ini_SetValuef(ini, "MasterVolume", "%g", g->master_volume);
        g = g_game;
        g->wav_volume = Options_PctToVolume(g->wav_volume_pct, g->wav_volume);
        Ini_SetValuef(ini, "WavVolume", "%g", g->wav_volume);
        g = g_game;
        g->bgm_volume = Options_PctToVolume(g->bgm_volume_pct, g->bgm_volume);
        new_bgm = g->master_volume * g->bgm_volume / 100.0;
        if (new_bgm != old_bgm) {
            Bgm_SetVolume(new_bgm);
            g = g_game;
        }
        Ini_SetValuef(ini, "BGMVolume", "%g", g->bgm_volume);
    }
    if (Ini_SelectGroup(ini, "Input") != -1) {
        Ini_SetValuef(ini, "p1.joystick.type", "%i", g_game->option_input[0][1]);
        Ini_SetValuef(ini, "p2.joystick.type", "%i", g_game->option_input[1][1]);
    }
    Options_SaveKeys(ini, "p1 keys", g_game->keys[0], KEY_NONE);
    Options_SaveKeys(ini, "p2 keys", g_game->keys[1], KEY_NONE);
#ifdef MUGEN_GAMEPAD_POLICY
    Options_SaveKeys(ini, "p1 joystick", g_cfgRawJoyKeys[0], JOY_NONE);
    Options_SaveKeys(ini, "p2 joystick", g_cfgRawJoyKeys[1], JOY_NONE);
    if (!Config_SaveGamepadGroup(ini, 0) || !Config_SaveGamepadGroup(ini, 1)) {
        Ini_Free(ini);
        Error_Fatal("Cannot save gamepad bindings\n");
    }
#else
    Options_SaveKeys(ini, "p1 joystick", g_game->joy_keys[0], JOY_NONE);
    Options_SaveKeys(ini, "p2 joystick", g_game->joy_keys[1], JOY_NONE);
#endif
    if (!Ini_Save(ini, CONFIG_PATH)) {
        Error_Fatal("Error saving %s\n", CONFIG_PATH);
    }
    Ini_Free(ini);
}
