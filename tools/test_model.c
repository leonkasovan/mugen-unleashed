/* Merge regression: real glTF loading, GL pixels and context recreation.
 * Other engine subsystems are excluded; file/PNG/model code is production. */
#include "mugen.h"
#include <assert.h>
#include <stdarg.h>

/* The probe owns a native SDL2 context rather than an engine display. */
#undef SDL_GL_SetAttribute

int g_renderMode, g_glInitialized, g_glBoundFramebuffer;
unsigned int g_glCurrentProgram;
static GameVars game;
GameVars *g_game = &game;

void Log_Printf(const char *format, ...)
{
    va_list args;
    va_start(args, format); vprintf(format, args); va_end(args);
}
void Error_Printf(const char *format, ...)
{
    va_list args;
    va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}
void MatchLog_Printf(int level, const char *format, ...)
{
    va_list args;
    (void)level;
    va_start(args, format); vprintf(format, args); va_end(args);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    printf("abi: %u-bit build\n", (unsigned)(sizeof(void *) * 8));
    g_renderMode = RENDER_SYSTEM;
    assert(Model_Load(argv[1]) == NULL); /* Must not call GL without a context. */
#ifdef MUGEN_MODEL3D
    {
        int pass, w, h;
        unsigned char invalid[8] = { 0 };
        assert(Png_DecodeRGBA(NULL, 8, &w, &h) == NULL);
        assert(Png_DecodeRGBA(invalid, sizeof invalid, &w, &h) == NULL);
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            printf("SKIP: SDL video: %s\n", SDL_GetError()); return 77;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        for (pass = 0; pass < 2; ++pass) {
            SDL_Window *window;
            SDL_GLContext context;
            Model *mdl;
            unsigned char pixel[4], corner[4];
            ModelFx fx = { { 0, 0, 0 }, { 1, 1, 1 }, 1, 0 };
            float ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            window = SDL_CreateWindow("model probe", 0, 0, 64, 64,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            context = window ? SDL_GL_CreateContext(window) : NULL;
            if (!context) {
                printf("SKIP: GL context: %s\n", SDL_GetError());
                if (window) SDL_DestroyWindow(window);
                SDL_Quit(); return 77;
            }
            assert(gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress));
            printf("GL: %s\n", glGetString(GL_VERSION));
            /* SDL's context-version query can leave INVALID_ENUM on GL 2.1.
             * Establish the probe boundary before calling the model loader. */
            while (glGetError() != GL_NO_ERROR) { }
            g_renderMode = RENDER_OPENGL; g_glInitialized = 1;
            mdl = Model_Load(argv[1]);
            assert(mdl && mdl->scene_count == 1 && mdl->node_count == 1);
            assert(mdl->prim_count == 1 && mdl->index_count == 3 && mdl->vbo && mdl->ibo);
            assert(mdl->texture_count == 1 && mdl->textures[0] != 0);
            assert(glGetError() == GL_NO_ERROR);
            Model_Reset(mdl); Model_Step(mdl);
            glViewport(0, 0, 64, 64);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            Model_Draw(mdl, 0, 0, 0, ident, ident, &fx, 0, NULL);
            glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            glReadPixels(0, 63, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
            printf("pass %d pixel=%u,%u,%u,%u\n", pass, pixel[0], pixel[1], pixel[2], pixel[3]);
            assert(glGetError() == GL_NO_ERROR);
            assert(pixel[1] > pixel[0] + 50 && pixel[1] > pixel[2] + 50 && pixel[3] > 200);
            assert(corner[0] == 0 && corner[1] == 0 && corner[2] == 0);
            assert(g_glCurrentProgram == 0);
            Model_Free(mdl);
            Model_RendererQuit(); Model_RendererQuit(); /* Idempotent teardown. */
            assert(glGetError() == GL_NO_ERROR);
            g_glInitialized = 0;
            SDL_GL_DeleteContext(context); SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
    puts("ok: model loading, pixels and recreated GL context");
#else
    Bg3D_SetModel(NULL); Bg3D_EndModel(); Bg3D_FreeMotifModel();
    Bg3D_Attach(NULL, NULL); Bg3D_Tick(NULL); Bg3D_Draw(NULL, 0); Bg3D_Detach(NULL);
    Stage3D_Tick(); Stage3D_Reset(); Stage3D_Draw(0); Stage3D_Free();
    Model_RendererQuit(); Model_Free(NULL);
    assert(g_stage3d.model == NULL && g_glInitialized == 0);
    puts("ok: unsupported model backend keeps the 2D path inert");
#endif
    return 0;
}
