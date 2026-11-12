/* Merge regression: real glTF loading, GL pixels and context recreation.
 * Other engine subsystems are excluded; file/PNG/model code is production. */
#include "mugen.h"
#include <assert.h>
#include <stdarg.h>
/* ModelGpu_Ensure: the basic-profile case asserts the upload refuses to rebuild. */
#include "model_backend.h"

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

#ifdef MUGEN_MODEL3D
static GLuint caller_program(void)
{
#ifdef MUGEN_GLES
    const char *vertex = "#version 300 es\nin vec3 position; void main(){gl_Position=vec4(position,1);}";
    const char *fragment = "#version 300 es\nprecision mediump float; out vec4 color; void main(){color=vec4(1);}";
#else
    const char *vertex = "#version 120\nattribute vec3 position; void main(){gl_Position=vec4(position,1);}";
    const char *fragment = "#version 120\nvoid main(){gl_FragColor=vec4(1);}";
#endif
    GLuint shaders[2], program = glCreateProgram();
    const char *sources[2] = {vertex, fragment};
    GLenum types[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    GLint ok;
    int i;
    for (i = 0; i < 2; ++i) {
        shaders[i] = glCreateShader(types[i]);
        glShaderSource(shaders[i], 1, &sources[i], NULL); glCompileShader(shaders[i]);
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &ok); assert(ok);
        glAttachShader(program, shaders[i]); glDeleteShader(shaders[i]);
    }
    glLinkProgram(program); glGetProgramiv(program, GL_LINK_STATUS, &ok); assert(ok);
    glUseProgram(program); g_glCurrentProgram = program;
    return program;
}

/* A caller-owned sprite-like buffer/program state must survive upload and draw. */
static void check_state(GLuint buffer, GLuint element, GLuint texture, GLuint program)
{
    GLint value, box[4];
    GLfloat constant[4], clear_depth;
    GLboolean mask[4], depth_mask;
    glGetIntegerv(GL_CURRENT_PROGRAM, &value); assert((GLuint)value == program);
    assert(g_glCurrentProgram == program);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask); assert(!depth_mask);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask); assert(!mask[0] && !mask[1] && !mask[2] && !mask[3]);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear_depth); assert(clear_depth == 0);
#ifndef MUGEN_GLES
    assert(glIsEnabled(GL_ALPHA_TEST));
#endif
    assert(glIsEnabled(GL_SCISSOR_TEST));
    assert(!glIsEnabled(GL_BLEND));
    glGetIntegerv(GL_SCISSOR_BOX, box);
    assert(box[0] == 4 && box[1] == 5 && box[2] == 40 && box[3] == 41);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &value); assert((GLuint)value == buffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &value); assert((GLuint)value == element);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &value); assert(value == GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &value); assert((GLuint)value == texture);
    glGetIntegerv(GL_DEPTH_FUNC, &value); assert(value == GL_GREATER);
    glGetIntegerv(GL_FRONT_FACE, &value); assert(value == GL_CW);
    glGetIntegerv(GL_BLEND_SRC_RGB, &value); assert(value == GL_SRC_ALPHA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &value); assert(value == GL_ONE);
    glGetVertexAttribfv(3, GL_CURRENT_VERTEX_ATTRIB, constant); assert(constant[0] == .25f);
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &value); assert(value);
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &value); assert((GLuint)value == buffer);
}

static void caller_state(GLuint *buffer, GLuint *element, GLuint *texture)
{
    static const float vertices[12] = { 0 };
    glGenBuffers(1, buffer); glBindBuffer(GL_ARRAY_BUFFER, *buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glVertexAttrib4f(3, .25f, .5f, .75f, 1);
    glGenBuffers(1, element); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *element);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4, vertices, GL_STATIC_DRAW);
    glEnable(GL_SCISSOR_TEST); glScissor(4, 5, 40, 41);
    glDisable(GL_BLEND); glDepthFunc(GL_GREATER); glFrontFace(GL_CW);
    glDepthMask(GL_FALSE); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
#ifdef MUGEN_GLES
    glClearDepthf(0);
#else
    glClearDepth(0); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_NEVER, 0);
#endif
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glActiveTexture(GL_TEXTURE3);
    glGenTextures(1, texture); glBindTexture(GL_TEXTURE_2D, *texture);
}
#endif

int main(int argc, char **argv)
{
    assert(argc == 2);
    printf("abi: %u-bit build\n", (unsigned)(sizeof(void *) * 8));
    g_renderMode = RENDER_SYSTEM;
    assert(Model_Load(argv[1]) == NULL); /* Must not call GL without a context. */
#ifdef MUGEN_MODEL3D
    {
        int pass, w, h;
        /* The basic profile cannot rebuild after a context loss, so its frame is
         * captured before the loss; the reference build compares after it. */
#ifdef MUGEN_MODEL_BASIC
#define MODEL_TEST_CAPTURE_PASS 0
#else
#define MODEL_TEST_CAPTURE_PASS 1
#endif
        Model *live = NULL;
        unsigned char reference[64 * 64 * 4];
        float animation_time = 0;
        Model *asset = Model_LoadAsset(argv[1]);
        if (getenv("MODEL_TEST_BAD_ASSET") != NULL) {
            assert(asset == NULL); puts("ok: invalid asset rejected before GL"); return 0;
        }
        assert(asset && asset->vbo == 0 && asset->ibo == 0);
        assert(asset->vertex_data && asset->index_data && asset->images[0].pixels);
        Model_Free(asset); /* GL-free load/free before SDL has even initialized. */
        unsigned char invalid[8] = { 0 };
        assert(Png_DecodeRGBA(NULL, 8, &w, &h) == NULL);
        assert(Png_DecodeRGBA(invalid, sizeof invalid, &w, &h) == NULL);
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            printf("SKIP: SDL video: %s\n", SDL_GetError()); return 77;
        }
#ifdef MUGEN_GLES
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        for (pass = 0; pass < 2; ++pass) {
            SDL_Window *window;
            SDL_GLContext context;
            Model *mdl;
            unsigned char pixel[4], corner[4], rgba[64 * 64 * 4];
            GLuint caller_buffer, caller_element, caller_texture, sprite_program;
            float camera[3] = { 0, 0, 2 };
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
#ifdef MUGEN_GLES
            assert(gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress));
#else
            assert(gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress));
#endif
            printf("GL: %s\n", glGetString(GL_VERSION));
            /* SDL's context-version query can leave INVALID_ENUM on GL 2.1.
             * Establish the probe boundary before calling the model loader. */
            while (glGetError() != GL_NO_ERROR) { }
            g_renderMode = RENDER_OPENGL; g_glInitialized = 1;
            if (pass == 0) live = Model_Load(argv[1]);
            if (getenv("MODEL_TEST_REJECT_SKIN") != NULL) {
                assert(live == NULL);
                Model_RendererQuit(); SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
                puts("ok: basic profile rejects skinning explicitly"); return 0;
            }
            mdl = live;
            if (pass == 0 && getenv("MODEL_TEST_ENV") != NULL) {
                assert(mdl && Model_LoadEnvironment(mdl, getenv("MODEL_TEST_ENV"), .5f));
            }
            assert(mdl && mdl->scene_count == 1 && mdl->node_count >= 1);
            assert(mdl->prim_count == 1 && mdl->index_count == 3);
            assert(mdl->texture_count >= 1);
#ifdef MUGEN_MODEL_BASIC
            /*
             * The basic profile frees the decoded RGBA once it is on the GPU
             * (ModelGpu_Ensure), so it is gone by the time the model is live and
             * a context loss cannot rebuild it. Both are asserted here rather
             * than the desktop contract below.
             */
            assert(mdl->vertex_data && mdl->index_data);
            assert(mdl->images[0].pixels == NULL && mdl->cpu_textures_released);
#else
            assert(mdl->vertex_data && mdl->index_data && mdl->images[0].pixels);
#endif
            if (pass == 0) assert(mdl->textures[0] != 0);
            else assert(mdl->vbo == 0 && mdl->ibo == 0 && mdl->textures[0] == 0);
#ifdef MUGEN_MODEL_BASIC
            if (pass == 1) {
                /* The upload must refuse instead of binding freed textures. */
                assert(!ModelGpu_Ensure(mdl));
                assert(mdl->gpu_failed);
                Model_Free(mdl);
                g_glInitialized = 0;
                SDL_GL_DeleteContext(context); SDL_DestroyWindow(window);
                SDL_Quit();
                puts("ok: basic profile releases CPU textures and refuses a context rebuild");
                return 0;
            }
#endif
            assert(glGetError() == GL_NO_ERROR);
            if (pass == 0) {
                Model_Reset(mdl); Model_Step(mdl);
                mdl->nodes[0].id = 1234;
                if (mdl->anim_count) animation_time = mdl->anims[0].time;
            } else {
                assert(mdl->nodes[0].id == 1234);
                if (mdl->anim_count) assert(mdl->anims[0].time == animation_time);
            }
            glViewport(0, 0, 64, 64);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            sprite_program = caller_program();
            caller_state(&caller_buffer, &caller_element, &caller_texture);
            Model_DrawShadows(mdl);
            check_state(caller_buffer, caller_element, caller_texture, sprite_program);
            Model_Draw(mdl, 0, 0, 0, ident, ident, &fx, 0, camera);
            check_state(caller_buffer, caller_element, caller_texture, sprite_program);
            assert(mdl->vbo && mdl->ibo && mdl->textures[0]);
            glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            if (pass == 0) memcpy(reference, rgba, sizeof rgba);
            else assert(memcmp(reference, rgba, sizeof rgba) == 0);
            glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            glReadPixels(0, 63, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
            printf("pass %d pixel=%u,%u,%u,%u\n", pass, pixel[0], pixel[1], pixel[2], pixel[3]);
            assert(glGetError() == GL_NO_ERROR);
            assert(pixel[1] > pixel[0] + 50 && pixel[1] > pixel[2] + 50 && pixel[3] > 200);
            assert(corner[0] == 0 && corner[1] == 0 && corner[2] == 0);
            assert(g_glCurrentProgram == sprite_program);
            glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glDeleteBuffers(1, &caller_buffer); glDeleteBuffers(1, &caller_element);
            glDeleteTextures(1, &caller_texture);
            glUseProgram(0); g_glCurrentProgram = 0; glDeleteProgram(sprite_program);
            if (pass == MODEL_TEST_CAPTURE_PASS) {
                const char *capture = getenv("MODEL_TEST_CAPTURE");
                if (capture != NULL) {
                    FILE *file = fopen(capture, "wb");
                    assert(file && fwrite(rgba, 1, sizeof rgba, file) == sizeof rgba);
                    fclose(file);
                }
            }
            if (pass == 1) {
                /* Reached by the reference build only: the basic profile returns
                 * above once it has asserted that a rebuild is refused. */
                Model_RendererQuit(); Model_RendererQuit(); /* Orderly teardown of a live asset. */
                assert(mdl->vbo == 0 && mdl->textures[0] == 0);
                Model_Free(mdl);
                assert(glGetError() == GL_NO_ERROR);
            }
            g_glInitialized = 0;
            SDL_GL_DeleteContext(context); SDL_DestroyWindow(window);
            /* Real unplanned context loss: invalidate only after destruction.
             * CPU model/animation state stays alive into the replacement. */
            Model_OnContextLost();

        }
        SDL_Quit();
    }
    puts("ok: asset/GPU lifetimes, caller state, pixels and live-model context recovery");
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
