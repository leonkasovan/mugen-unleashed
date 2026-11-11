/* Real SDL2 window + production keyboard and translated input state. */
#include "mugen.h"
#include <assert.h>
#ifdef main
#undef main
#endif
void Log_Printf(const char *fmt, ...) { (void)fmt; }
void MatchLog_Printf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
void Timer_ResetFrame(void) {}
static GameVars game;
static int queue[INPUT_RING_SIZE];
static SDL_Window *window(void)
{
    int i;
    for (i=1;i<100;i++) { SDL_Window *w=SDL_GetWindowFromID((Uint32)i);if(w)return w; }
    return NULL;
}
static void clear_queues(void)
{
    g_keyQueue.read=g_keyQueue.write=0;
    g_keyboardQueueRead=g_keyboardQueueWrite=0;
}
static void enter(int pressed)
{
    Keyboard_HandleKey(SDLK_RETURN,pressed);
}
static void state(int fs)
{
    assert(game.fullscreen==fs && g_videoFullscreen==fs);
    assert(((g_videoFlags&VIDEO_FULLSCREEN)!=0)==fs);
    assert(((g_videoSdlFlags&DISPLAY_FULLSCREEN)!=0)==fs);
    assert(((SDL_GetWindowFlags(window())&SDL_WINDOW_FULLSCREEN)!=0)==fs);
}
int main(void)
{
    int mode,fs,i;
    if(SDL_Init(SDL_INIT_VIDEO)<0) {fprintf(stderr,"SKIP: %s\n",SDL_GetError());return 77;}
    g_game=&game;game.bgm_background_mode=1;
    g_keyQueue.data=queue;g_keyQueue.capacity=INPUT_RING_SIZE;
    Keyboard_Init();g_keyHandler=Input_OnKey;
    for(mode=0;mode<2;mode++)for(fs=0;fs<2;fs++) {
        g_renderMode=mode ? RENDER_OPENGL : RENDER_SYSTEM;
        g_screenSurface=Sdl2_SetVideoMode(640,480,32,(mode?DISPLAY_OPENGL:0)|(fs?DISPLAY_FULLSCREEN:0));
        assert(g_screenSurface && window());
        game.fullscreen=g_videoFullscreen=fs;g_videoFlags=fs?VIDEO_FULLSCREEN:0;
        g_videoSdlFlags=(mode?DISPLAY_OPENGL:0)|(fs?DISPLAY_FULLSCREEN:0);
        Keyboard_HandleKey(ENGINE_KEY_LALT,1);clear_queues();
        enter(1);state(!fs);
        for(i=0;i<8;i++)enter(1);
        state(!fs);
        assert(!g_keyState[SDLK_RETURN] && !g_inputState[SDLK_RETURN]);
        assert(!Input_CodePending() && g_keyboardQueueRead==g_keyboardQueueWrite);
        enter(0);clear_queues();
        g_screenSurface=Sdl2_SetVideoMode(640,480,32,g_videoSdlFlags);
        assert(g_screenSurface);state(!fs); /* resize must keep current mode */
        /* Alt remains held: a second physical Return press is a new chord. */
        enter(1);state(fs);enter(0);
        Keyboard_HandleKey(ENGINE_KEY_LALT,0);clear_queues();
        /* A chord remains swallowed if Alt is released before Return. */
        Keyboard_HandleKey(ENGINE_KEY_RALT,1);clear_queues();enter(1);
        Keyboard_HandleKey(ENGINE_KEY_RALT,0);clear_queues();
        enter(1);enter(0);state(!fs);
        assert(!g_inputState[SDLK_RETURN] && !Input_CodePending());
        /* Explicit requests are idempotent and do not desynchronize SDL. */
        assert(Video_SetFullscreen(!fs));assert(Video_SetFullscreen(!fs));
        assert(((SDL_GetWindowFlags(window())&SDL_WINDOW_FULLSCREEN)!=0)==!fs);
        Keyboard_HandleKey(SDLK_RETURN,1);
        assert(g_keyState[SDLK_RETURN] && g_inputState[SDLK_RETURN] && Input_CodePending());
        Keyboard_HandleKey(ENGINE_KEY_LALT,1);clear_queues();
        enter(1);state(!fs); /* Enter was already held before Alt: no chord */
        enter(0);Keyboard_HandleKey(ENGINE_KEY_LALT,0);clear_queues();
        game.safe_mode=1;
        Keyboard_HandleKey(ENGINE_KEY_LALT,1);clear_queues();enter(1);state(!fs);
        assert(g_inputState[SDLK_RETURN]);
        enter(0);Keyboard_HandleKey(ENGINE_KEY_LALT,0);game.safe_mode=0;clear_queues();
        printf("ok: %s start-fullscreen=%d repeat, re-arm, Alt-first release, queues/state and SDL flags\n",mode?"OpenGL":"System",fs);
        Sdl2_VideoQuit();g_screenSurface=NULL;
    }
    /* A refused switch still consumes the shortcut and preserves engine state. */
    game.fullscreen=g_videoFullscreen=0;g_videoFlags=0;
    Keyboard_HandleKey(ENGINE_KEY_LALT,1);clear_queues();enter(1);
    assert(!game.fullscreen && !g_videoFullscreen && !g_videoFlags);
    assert(!g_keyState[SDLK_RETURN] && !g_inputState[SDLK_RETURN] && !Input_CodePending());
    enter(0);Keyboard_HandleKey(ENGINE_KEY_LALT,0);
    SDL_Quit();puts("ok: failed fullscreen switch cannot confirm menus or taunt");return 0;
}
