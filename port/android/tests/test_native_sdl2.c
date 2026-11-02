/* Real Android engine paths on host SDL2; no fake SDL entry points or GL stubs.
 * Run from a writable build directory. Optional args: font.ttf music.wav music.ogg music.mp3. */
#include "mugen.h"
#include "sdl2keys.h"
#include "sdl2surface.h"
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <assert.h>
#include <limits.h>

extern int AndroidTest_Uploads;
extern int AndroidTest_Presents;

static int text_count;
static unsigned int last_text;
static GameVars game;

static void Text(unsigned int unicode, int key, int pressed)
{
    if (unicode != 0) {
        text_count++;
        last_text = unicode;
    }
}

static void Key(SDL_Keycode key, int down, int repeat)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.sym = key;
    event.key.repeat = (Uint8)repeat;
    assert(SDL_PushEvent(&event) == 1);
    Events_Poll();
}

static void Input(void)
{
    SDL_Event event;
    int i;
    g_keyboardKeepRepeat = 0;
    Keyboard_Init();
    g_keyboardNoReleases = 0;
    for (i = 1; i < ENGINE_KEY_LIMIT; i++) {
        SDL_Keycode key = Sdl2_SdlKeyForEngineKey(i);
        if (key != SDLK_UNKNOWN) {
            assert(Sdl2_EngineKey(key) == i);
        }
    }
    Key(SDLK_UP, 1, 0);
    assert(g_keyState[ENGINE_KEY_UP] == 1);
    Key(SDLK_UP, 0, 0);
    assert(g_keyState[ENGINE_KEY_UP] == 0);
    Key(SDLK_F12, 1, 0);
    assert(g_keyState[ENGINE_KEY_F12] == 1);
    Key(SDLK_LCTRL, 1, 0);
    assert(g_keyState[ENGINE_KEY_LCTRL] == 1);
    Key(SDLK_KP_7, 1, 0);
    assert(g_keyState[263] == 1);
    assert(strcmp(SDL_GetKeyName(Sdl2_SdlKeyForEngineKey(ENGINE_KEY_UP)), "Up") == 0);
    Keyboard_HandleKey(INT_MAX, 1);
    Keyboard_HandleKey(-1, 1);
    Key(SDLK_AUDIOPLAY, 1, 0); /* unsupported keys cannot index the state array */
    Key(SDLK_a, 1, 1);
    assert(!g_keyState['a']);
    assert(Keyboard_SetEventHandler(Text) == 1);
    SDL_zero(event);
    event.type = SDL_TEXTINPUT;
    SDL_strlcpy(event.text.text, "ab\xC3\xA9", sizeof(event.text.text));
    assert(SDL_PushEvent(&event) == 1);
    Events_Poll();
    assert(text_count == 3 && last_text == 0xE9);
    SDL_zero(event);
    event.type = SDL_APP_WILLENTERBACKGROUND;
    assert(SDL_PushEvent(&event) == 1);
    Events_Poll();
    assert(!g_keyState[ENGINE_KEY_F12] && !g_keyState[ENGINE_KEY_LCTRL]);
    event.type = SDL_APP_DIDENTERFOREGROUND;
    assert(SDL_PushEvent(&event) == 1);
    Events_Poll();
    Keyboard_SyncState();
    assert(Keyboard_SetEventHandler(NULL) == 1);
    puts("ok: native SDL2 key IDs, event queue, text, repeat and focus");
}

static void Joysticks(void)
{
    SDL_Joystick *joy;
    SDL_JoystickID id;
    SDL_Event event;
    int device, code;
    device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 4, 1);
    assert(device >= 0);
    Events_Poll();
    assert(g_padCount > 0 && g_pads[0].joystick != NULL);
    joy = g_pads[0].joystick;
    id = SDL_JoystickInstanceID(joy);
    code = g_pads[0].button_base + 1;
    SDL_zero(event);
    event.type = SDL_JOYBUTTONDOWN;
    event.jbutton.which = id;
    event.jbutton.button = 0;
    assert(SDL_PushEvent(&event) == 1);
    Events_Poll();
    assert(g_padButtonState[code] == 1);
    event.type = SDL_JOYAXISMOTION;
    event.jaxis.which = id;
    event.jaxis.axis = 255; /* malformed/high-control-count devices stay in bounds */
    event.jaxis.value = 32767;
    assert(SDL_PushEvent(&event) == 1);
    Events_Poll();
    assert(SDL_JoystickDetachVirtual(device) == 0);
    Events_Poll();
    assert(g_pads[0].joystick == NULL && g_padButtonState[code] == 0);
    Input_DeinitPads();
    puts("ok: native SDL2 joystick hotplug, instance IDs and release");
}

static void Surfaces(const char *font_path)
{
    SDL_Surface *src, *scaled;
    SDL_BlendMode blend;
    SDL_Color colors[2] = {{20, 40, 60, 255}, {200, 100, 50, 255}};
    Uint8 alpha;
    Uint32 key;
    int x, y;
    src = SDL_CreateRGBSurfaceWithFormat(0, 13, 7, 8, SDL_PIXELFORMAT_INDEX8);
    assert(src != NULL);
    assert(SDL_SetPaletteColors(src->format->palette, colors, 0, 2) == 0);
    assert(SDL_SetColorKey(src, SDL_TRUE, 0) == 0);
    for (y = 0; y < src->h; y++) {
        for (x = 0; x < src->w; x++) {
            ((Uint8 *)src->pixels)[y * src->pitch + x] = (Uint8)((x + y) & 1);
        }
    }
    scaled = Sdl2_ScaleSurface(src, 1.5, 2.5, 0);
    assert(scaled != NULL && scaled->w == 20 && scaled->h == 18);
    assert(SDL_GetColorKey(scaled, &key) == 0 && key == 0);
    assert(scaled->format->palette->colors[1].r == 200);
    for (y = 0; y < scaled->h; y++) {
        for (x = 0; x < scaled->w; x++) {
            assert(((Uint8 *)scaled->pixels)[y * scaled->pitch + x] ==
                   ((Uint8 *)src->pixels)[(y * 7 / 18) * src->pitch + x * 13 / 20]);
        }
    }
    SDL_FreeSurface(scaled);
    assert(Sdl2_ScaleSurface(src, NAN, 1, 0) == NULL);
    assert(Sdl2_ScaleSurface(src, INFINITY, 1, 0) == NULL);
    SDL_FreeSurface(src);
    src = SDL_CreateRGBSurfaceWithFormat(0, 16, 2, 32, SDL_PIXELFORMAT_RGBA32);
    assert(src != NULL);
    for (x = 0; x < 16; x++) {
        ((Uint32 *)src->pixels)[x] = SDL_MapRGBA(src->format, (Uint8)(x * 10), 20, 30, 100);
    }
    SDL_SetSurfaceAlphaMod(src, 123);
    SDL_SetSurfaceColorMod(src, 90, 100, 110);
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
    scaled = Sdl2_ScaleSurface(src, 0.75, 1, 0);
    assert(scaled != NULL && scaled->w == 12);
    assert(SDL_GetSurfaceAlphaMod(scaled, &alpha) == 0 && alpha == 123);
    assert(SDL_GetSurfaceBlendMode(scaled, &blend) == 0 && blend == SDL_BLENDMODE_BLEND);
    assert(((Uint32 *)scaled->pixels)[3] == ((Uint32 *)src->pixels)[3]); /* 16.16, not floor(3*16/12) */
    SDL_FreeSurface(scaled);
    SDL_FreeSurface(src);
    if (font_path != NULL) {
        TTF_Font *font;
        SDL_Color color = {240, 210, 180, 255};
        assert(TTF_Init() == 0);
        font = TTF_OpenFont(font_path, 16);
        assert(font != NULL);
        src = TTF_RenderUTF8_Solid(font, "SDL2", color);
        assert(src != NULL && src->format->BitsPerPixel == 8);
        scaled = Sdl2_ScaleSurface(src, 1.5, 1.5, 0);
        assert(scaled != NULL && SDL_HasColorKey(scaled));
        SDL_FreeSurface(scaled);
        SDL_FreeSurface(src);
        src = TTF_RenderUTF8_Blended(font, "SDL2", color);
        assert(src != NULL);
        scaled = Sdl2_ScaleSurface(src, 1.5, 1.5, 0);
        assert(scaled != NULL && SDL_GetSurfaceBlendMode(scaled, &blend) == 0 && blend == SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(scaled);
        SDL_FreeSurface(src);
        TTF_CloseFont(font);
        TTF_Quit();
    }
    puts("ok: native SDL2 indexed/RGBA font scale and transparency");
}

static void Video(void)
{
    SDL_Surface *display;
    g_renderMode = RENDER_SYSTEM;
    game.keep_aspect = 1;
    assert(Video_PreInit() == 1);
    assert(Video_SetMode(640, 480, 640, 480, 16, 0) == 1);
    assert(g_videoWidth == 640 && g_videoHeight == 480); /* no Vita panel override */
    display = g_videoSurface;
    assert(display->w == 640 && display->h == 480);
    AndroidTest_Uploads = 0;
    AndroidTest_Presents = 0;
    assert(Android_PresentFrame() == 0);
    SDL_FillRect(display, NULL, SDL_MapRGB(display->format, 200, 50, 20));
    Android_MarkDisplayDirty(display);
    Android_MarkDisplayDirty(display);
    Android_MarkDisplayDirty(display);
    assert(AndroidTest_Uploads == 0 && AndroidTest_Presents == 0);
    assert(Android_PresentFrame() == 1);
    assert(AndroidTest_Uploads == 1 && AndroidTest_Presents == 1);
    assert(Android_PresentFrame() == 0); /* no duplicate/skipped-frame upload */
    assert(AndroidTest_Uploads == 1 && AndroidTest_Presents == 1);
    Android_MarkDisplayDirty(display);
    Android_SetBackground(1);
    assert(Android_PresentFrame() == 0);
    Android_SetBackground(0);
    assert(Android_PresentFrame() == 1);
    Android_ResetRenderer();
    assert(Android_PresentFrame() == 1);
    assert(Video_SetMode(320, 240, 320, 240, 16, 0) == 1); /* same format, different size */
    Android_MarkDisplayDirty(g_videoSurface);
    assert(Android_PresentFrame() == 1);
    assert(Android_CreateDisplay(0, 10, 16, 1) == NULL);
    assert(Android_CreateDisplay(640, 480, 24, 1) == NULL);
    assert(Video_SetMode(640, 480, 640, 480, 32, 0) == 1);
    assert(g_videoSurface->format->BytesPerPixel == 4);
    Android_MarkDisplayDirty(g_videoSurface);
    assert(Android_PresentFrame() == 1);
    Video_Deinit();
    Video_Deinit();
    assert(g_videoSurface == NULL && g_screenSurface == NULL);
    assert(Android_PresentFrame() == 0);
    puts("ok: native SDL2 video, presentation batching, resize, reset and teardown");
}

static void Audio(const char *path)
{
    BgmSettings *settings;
    BgmPluginConfig config;
    BgmHostCallbacks host = {Bgm_GetExtension, Bgm_GetLoopInfo, NULL};
    BgmPluginIo io;
    BgmInfo info;
    BgmOpenResult result;
    Stream *stream;
    void *instance;
    Mix_Chunk *effect;
    int channel, frequency, channels;
    Uint16 format;
    assert(Sound_Init(10, 2, 16, 8, 44100, SND_RESAMPLE_SDL, 0) == 1);
    assert(Mix_QuerySpec(&frequency, &format, &channels) > 0);
    assert(frequency == 44100 && format == AUDIO_S16LSB && channels == 2);
    settings = BgmSettings_New();
    assert(settings != NULL);
    config.format = g_sndFormat;
    config.settings = settings;
    assert(Sdlmix_Init(&config, &host, BgmSettings_Get) == 0);
    /* Release settings through the engine's normal plugin list owner. */
    {
        DynArray plugins;
        BgmPluginEntry *entry = calloc(1, sizeof(*entry));
        BgmPluginEntry **entries = malloc(sizeof(*entries));
        DynArray *owned = malloc(sizeof(*owned));
        entry->settings = settings;
        entries[0] = entry;
        plugins.data = entries; plugins.count = 1; plugins.capacity = 1; plugins.elem_size = sizeof(*entries);
        *owned = plugins;
        BgmPlugins_Free(owned);
    }
    if (path != NULL) {
        memset(&info, 0, sizeof(info));
        SDL_strlcpy(info.path, path, sizeof(info.path));
        info.loop = 0;
        info.loop_start = -1;
        info.loop_end = -1;
        stream = Stream_OpenFile(path, "rb");
        assert(stream != NULL);
        io.handle = stream;
        instance = Sdlmix_Create();
        assert(instance != NULL);
        assert(Sdlmix_Open(instance, &io, &info, &result) == 0);
        assert(Sdlmix_Start(instance) == 0 && Mix_PlayingMusic());
        SDL_Delay(30);
        assert(Sdlmix_Pause(instance) == 0 && Mix_PausedMusic());
        assert(Sdlmix_Resume(instance) == 0);
        assert(Sdlmix_SetMute(instance, 1) == 0 && Mix_VolumeMusic(-1) == 0);
        assert(Sdlmix_SetMute(instance, 0) == 0);
        Sdlmix_Stop(instance);
        Sdlmix_Close(instance);
        Sdlmix_Destroy(instance);
        Stream_Close(stream);
        assert(Bgm_Init(NULL, SND_RESAMPLE_LIBRESAMPLE, 1, 100) == 1);
        assert(g_bgmInitialized && g_bgmThreadRunning);
        assert(Bgm_FindSubsystem(path) == &g_bgmSdlmixSubsystem);
        assert(Bgm_FindSubsystem("unsupported.mid") == NULL);
        assert(Bgm_Play(&info) == 1);
        {
            int tries;
            for (tries = 0; tries < 100 && !Mix_PlayingMusic(); tries++) {
                SDL_Delay(5);
            }
            assert(Mix_PlayingMusic());
        }
        Bgm_Deinit();
        assert(g_bgmMutex == NULL && !g_bgmThreadRunning);
        effect = Mix_LoadWAV(path);
        assert(effect != NULL);
        channel = Mix_PlayChannel(-1, effect, 0);
        assert(channel >= 0 && Mix_Playing(channel));
        Mix_HaltChannel(channel);
        Mix_FreeChunk(effect);
    }
    Sound_Deinit();
    puts("ok: native SDL2_mixer audio device, music and effects");
}

static void Write32(FILE *file, unsigned int value)
{
    int i;
    for (i = 0; i < 4; i++) {
        assert(fputc((int)((value >> (i * 8)) & 255), file) != EOF);
    }
}

static void MakeWave(void)
{
    FILE *file;
    int i;
    file = fopen("native-tone.wav", "wb");
    assert(file != NULL);
    assert(fwrite("RIFF", 1, 4, file) == 4); Write32(file, 36 + 44100 * 4);
    assert(fwrite("WAVEfmt ", 1, 8, file) == 8); Write32(file, 16);
    assert(fwrite("\x01\x00\x02\x00", 1, 4, file) == 4); Write32(file, 44100); Write32(file, 44100 * 4);
    assert(fwrite("\x04\x00\x10\x00" "data", 1, 8, file) == 8); Write32(file, 44100 * 4);
    for (i = 0; i < 44100; i++) {
        unsigned int sample = (i / 50) & 1 ? 6000 : (unsigned short)-6000;
        Write32(file, sample | (sample << 16));
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    int i;
    SDL_SetMainReady();
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0);
    g_game = &game;
    Input();
    Joysticks();
    Surfaces(argc > 1 ? argv[1] : NULL);
    Video();
    if (argc > 2) {
        for (i = 2; i < argc; i++) {
            Audio(argv[i]);
        }
    } else {
        MakeWave();
        Audio("native-tone.wav");
        assert(remove("native-tone.wav") == 0);
    }
    g_game = NULL;
    SDL_Quit();
    puts("ok: Android native SDL2 tests passed");
    return 0;
}
