/* Exercise actual backend code, injecting only the GL driver and unrelated
 * sprite/SDL boundaries. Assertions observe storage, ownership and callers,
 * rather than just searching source text. All files live in the runner OUT. */
#include "mugen.h"
#ifdef main
#undef main
#endif
#include <assert.h>
#include <stdarg.h>

#ifdef TEST_VITA
/* Select the actual Vita shader-loader branches while retaining the host's
 * GL declarations/driver injection. GXM and runtime compilation are mocked. */
#define MUGEN_VITA_GL 1
enum { VGL_MODE_SHADER_PAIR, VGL_MODE_GLOBAL, VGL_MODE_POSTPONED };
void vglSetSemanticBindingMode(GLenum mode);
int VitaGl_Probe(void);
int VitaGl_InitShaderCompiler(void);
int VitaGl_OwnsDisplay(void);
void VitaDiag_Log(const char *format, ...);
void SDL_VITA_ShowMessageBox(const char *text);
void VitaGl_DrawQuad(GLuint program, const float *v, int special,
                     float top, float bottom, const DrawParams *dp);
#endif

#ifdef TEST_GLES
/* The host uses the vendored desktop GL declarations for common GL calls.
 * Core FBO calls in GLES have the same signatures as GL2.1's EXT entry points.
 * Engine types were included above, before selecting the Android source. */
#ifndef TEST_LINUX_GLES
#define ANDROID_PORT 1
#endif
#define MUGEN_GLES 1
#define GL_R8 0x8229
#define GL_FRAMEBUFFER GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_BINDING GL_FRAMEBUFFER_BINDING_EXT
#define GL_RENDERBUFFER GL_RENDERBUFFER_EXT
#define GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0_EXT
#define GL_DEPTH_ATTACHMENT GL_DEPTH_ATTACHMENT_EXT
#define GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE_EXT
#define glBindFramebuffer glBindFramebufferEXT
#define glGenFramebuffers glGenFramebuffersEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glFramebufferRenderbuffer glFramebufferRenderbufferEXT
#define glGenRenderbuffers glGenRenderbuffersEXT
#define glDeleteRenderbuffers glDeleteRenderbuffersEXT
#define glBindRenderbuffer glBindRenderbufferEXT
#define glRenderbufferStorage glRenderbufferStorageEXT
void Android_GLDrawableSize(int *w, int *h);
void Sdl2_GLDrawableSize(int *w, int *h);
void Android_MarkDisplayDirty(SDL_Surface *s);
static void overlay(SpriteSet *set,int index);
#include "../src/gles_render.c"
#include "../src/gles_draw.c"
#include "../src/gles_probe.c"
#else
int Test_LoadGL(void);
#define gladLoadGL Test_LoadGL
#include "../src/glrender.c"
#undef gladLoadGL
void Test_DrawGL(SpriteSet *set,int s,const DrawParams *dp);
#define Sprite_DrawGL Test_DrawGL
#include "../src/gldetect.c"
#undef Sprite_DrawGL
#endif
#ifdef TEST_VITA
/* Types were loaded for the host above; select the real Vita error UI branch
 * even when this mock harness itself is cross-built for Windows. */
#ifdef _WIN32
#define TEST_VITA_HOST_WIN32 1
#undef _WIN32
#endif
#include "../src/error.c"
#ifdef TEST_VITA_HOST_WIN32
#define _WIN32 1
#endif
#undef MUGEN_VITA_GL /* gfx.c's System scaler is outside the shader test. */
#endif
#include "../src/gfx.c"

static GLenum pending;
static unsigned next_id = 10, binding, color_attachment, depth_attachment;
static int image_calls, sub_calls, fail_image, fail_read, fail_depth, fail_fbo;
static int gen_calls, fail_gen_at;
static int deleted_textures, deleted_depth, deleted_fbos, deleted_shaders, deleted_programs;
static int shader_ok = 1, link_ok = 1, fail_compile_at, compile_calls, color_location = 3;
#ifdef TEST_VITA
static GLenum semantic_mode = VGL_MODE_POSTPONED;
static int compiler_missing, compile_error_only, links, compiler_hints;
static int preflight_failure, missing_vita_mul;
static int native_display_owned, sdl_dialogs, attachments, program_creations;
void vglSetSemanticBindingMode(GLenum mode) { semantic_mode = mode; }
void VitaDiag_Log(const char *format, ...) { (void)format; }
int VitaGl_Probe(void) { return 1; }
int VitaGl_InitShaderCompiler(void) { return !preflight_failure; }
int VitaGl_OwnsDisplay(void) { return native_display_owned; }
void SDL_VITA_ShowMessageBox(const char *text) { assert(text); sdl_dialogs++; }
void VitaGl_DrawQuad(GLuint program, const float *v, int special,
                     float top, float bottom, const DrawParams *dp) {}
#endif
static int last_color_location, read_mode;
#ifdef TEST_GLES
static int overlays, swaps;
#endif
static GLint viewport[4] = {0,0,640,480}, scissor[4];
static const char *version = "OpenGL ES 3.0 mock";

void Log_Printf(const char *fmt, ...)
{
#ifdef TEST_VITA
    if (strstr(fmt, "libshacccg.suprx") != NULL) compiler_hints++;
#endif
    (void)fmt;
}
void MatchLog_Printf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
static GLenum APIENTRY error_get(void) { GLenum e = pending; pending = 0; return e; }
static void APIENTRY gen(GLsizei n, GLuint *p)
{ while (n--) { gen_calls++; *p++ = gen_calls == fail_gen_at ? 0 : next_id++; } }
static void APIENTRY del_tex(GLsizei n, const GLuint *p) { deleted_textures += n; }
static void APIENTRY del_depth(GLsizei n, const GLuint *p) { deleted_depth += n; }
static void APIENTRY del_fbo(GLsizei n, const GLuint *p) { deleted_fbos += n; }
static void APIENTRY bind_tex(GLenum t, GLuint id) {}
static void APIENTRY parameter(GLenum t, GLenum p, GLint v) {}
static void APIENTRY pixel_store(GLenum t, GLint v) {}
static void APIENTRY image(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p)
{ image_calls++; if (fail_image) { fail_image = 0; pending = GL_OUT_OF_MEMORY; } }
static void APIENTRY subimage(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *p)
{ sub_calls++; if (fail_image) { fail_image = 0; pending = GL_OUT_OF_MEMORY; } }
static void APIENTRY get_int(GLenum p, GLint *v)
{
    if (p == GL_FRAMEBUFFER_BINDING_EXT) *v = binding;
    else if (p == GL_VIEWPORT) memcpy(v, viewport, sizeof(viewport));
    else if (p == GL_MAX_TEXTURE_SIZE) *v = 16384;
    else *v = 0;
}
static const GLubyte *APIENTRY get_string(GLenum p) { return (const GLubyte *)version; }
static void APIENTRY bind_fbo(GLenum t, GLuint id) { binding = id; }
static void APIENTRY attach_tex(GLenum a, GLenum b, GLenum c, GLuint id, GLint l) { color_attachment = id; }
static void APIENTRY attach_depth(GLenum a, GLenum b, GLenum c, GLuint id) { depth_attachment = id; }
static GLenum APIENTRY fbo_status(GLenum t)
{ return fail_fbo ? GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT : GL_FRAMEBUFFER_COMPLETE_EXT; }
static void APIENTRY depth_store(GLenum t, GLenum f, GLsizei w, GLsizei h)
{ if (fail_depth) pending = GL_OUT_OF_MEMORY; }
static void APIENTRY read_pixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p)
{
    size_t i;
    unsigned int *out = p;
    if (fail_read) { pending = GL_INVALID_OPERATION; return; }
    for (i = 0; i < (size_t)w*h; i++) out[i] = read_mode == 1 ? 0xFF000000u :
        (read_mode == 2 ? 0xFFFFFFFFu : ((i & 1) ? 0xFF000000u : 0xFFFFFFFFu));
}
static GLuint APIENTRY create_shader(GLenum t) { return next_id++; }
static GLuint APIENTRY create_program(void)
{
#ifdef TEST_VITA
    program_creations++;
#endif
    return next_id++;
}
static void APIENTRY source(GLuint s, GLsizei n, const GLchar *const *text, const GLint *len) {}
static void APIENTRY compile(GLuint s)
{
    compile_calls++;
#ifdef TEST_VITA
    if (compiler_missing || compile_error_only) pending = GL_INVALID_OPERATION;
#endif
}
static void APIENTRY shader_iv(GLuint s, GLenum p, GLint *v)
{
#ifdef TEST_VITA
    /* Actual vitaGL contract: deferred status is true without a GXM program. */
    if (p == GL_COMPILE_STATUS && semantic_mode == VGL_MODE_POSTPONED) { *v = GL_TRUE; return; }
    if (compiler_missing) { *v = 0; return; }
#endif
    *v = p == GL_COMPILE_STATUS ? shader_ok && compile_calls != fail_compile_at : 0;
}
static void APIENTRY program_iv(GLuint s, GLenum p, GLint *v) { *v = p == GL_LINK_STATUS ? link_ok : 0; }
static GLboolean APIENTRY is_shader(GLuint s) { return GL_TRUE; }
static void APIENTRY shader_log(GLuint s, GLsizei n, GLsizei *len, GLchar *p) { if (len) *len = 0; if (n) *p = 0; }
static void APIENTRY attach_shader(GLuint p, GLuint s)
{
#ifdef TEST_VITA
    attachments++;
#endif
}
static void APIENTRY object_noop(GLuint p) {}
#ifdef TEST_VITA
static void APIENTRY link_program(GLuint p)
{
    /* Real deferred vitaGL can dereference a NULL program in this situation. */
    assert(shader_ok && !compiler_missing && !compile_error_only);
    assert(compile_calls != fail_compile_at);
    links++;
}
#endif
static void APIENTRY del_shader(GLuint p) { deleted_shaders++; }
static void APIENTRY del_program(GLuint p) { deleted_programs++; }
static GLint APIENTRY uniform_location(GLuint p, const GLchar *name)
{
#ifdef TEST_VITA
    /* A mock must distinguish the Cg-safe uniform from the desktop spelling. */
    if (!strcmp(name, "mul")) return -1;
    if (!strcmp(name, "uPalMul")) return missing_vita_mul ? -1 : 73;
#else
    if (!strcmp(name, "mul")) return 74;
    if (!strcmp(name, "uPalMul")) return -1;
#endif
    return !strcmp(name,"u_color") ? color_location : 1;
}
static void APIENTRY attrib_location(GLuint p, GLuint a, const GLchar *name) {}
static void APIENTRY enum_noop(GLenum p) {}
static void APIENTRY two_enums(GLenum p, GLenum q) {}
static void APIENTRY view(GLint x, GLint y, GLsizei w, GLsizei h) { viewport[0]=x; viewport[1]=y; viewport[2]=w; viewport[3]=h; }
static void APIENTRY clip(GLint x, GLint y, GLsizei w, GLsizei h) { scissor[0]=x;scissor[1]=y;scissor[2]=w;scissor[3]=h; }
static void APIENTRY four_floats(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {}
static void APIENTRY uniform4(GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { last_color_location = loc; }
static void APIENTRY uniform1i(GLint loc, GLint a) {}
static void APIENTRY uniform1f(GLint loc, GLfloat a) {}
static void APIENTRY uniform2(GLint loc, GLfloat a, GLfloat b) {}
static void APIENTRY uniform3(GLint loc, GLfloat a, GLfloat b, GLfloat c) {}
static void APIENTRY matrix(GLint loc, GLsizei n, GLboolean t, const GLfloat *p) {}
static void APIENTRY buffer_data(GLenum t, GLsizeiptr n, const void *p, GLenum usage) {}
static void APIENTRY buffer_sub(GLenum t, GLintptr off, GLsizeiptr n, const void *p) {}
static void APIENTRY attrib_ptr(GLuint a, GLint n, GLenum t, GLboolean norm, GLsizei stride, const void *p) {}
static void APIENTRY draw_arrays(GLenum m, GLint first, GLsizei n) {}
static void APIENTRY clear(GLbitfield m) {}
static void APIENTRY no_args(void) {}
static void APIENTRY ortho(GLdouble a,GLdouble b,GLdouble c,GLdouble d,GLdouble e,GLdouble f) {}
static void APIENTRY three_doubles(GLdouble a,GLdouble b,GLdouble c) {}
static void APIENTRY one_double(GLdouble a) {}
static void APIENTRY two_floats(GLfloat a,GLfloat b) {}
static void APIENTRY multi_coord(GLenum t,GLfloat a,GLfloat b) {}
static void APIENTRY three_floats(GLfloat a,GLfloat b,GLfloat c) {}
static void APIENTRY four_float_rotate(GLfloat a,GLfloat b,GLfloat c,GLfloat d) {}

/* CPU-only boundaries of Gfx_SetRenderTarget/Gfx_SetClipRect. */
SDL_bool SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *r) { return SDL_TRUE; }

static void install(void)
{
#define SET(name, fn) glad_gl##name = fn
    SET(GetError,error_get); SET(GenTextures,gen); SET(DeleteTextures,del_tex);
    SET(BindTexture,bind_tex); SET(TexParameteri,parameter); SET(PixelStorei,pixel_store);
    SET(TexImage2D,image); SET(TexSubImage2D,subimage); SET(GetIntegerv,get_int); SET(GetString,get_string);
    SET(BindFramebufferEXT,bind_fbo); SET(GenFramebuffersEXT,gen); SET(DeleteFramebuffersEXT,del_fbo);
    SET(FramebufferTexture2DEXT,attach_tex); SET(FramebufferRenderbufferEXT,attach_depth);
    SET(CheckFramebufferStatusEXT,fbo_status); SET(GenRenderbuffersEXT,gen); SET(DeleteRenderbuffersEXT,del_depth);
    SET(BindRenderbufferEXT,bind_tex); SET(RenderbufferStorageEXT,depth_store); SET(ReadPixels,read_pixels);
    SET(CreateShader,create_shader); SET(CreateProgram,create_program); SET(ShaderSource,source); SET(CompileShader,compile);
    SET(GetShaderiv,shader_iv); SET(GetProgramiv,program_iv); SET(IsShader,is_shader);
    SET(GetShaderInfoLog,shader_log); SET(GetProgramInfoLog,shader_log); SET(AttachShader,attach_shader);
    SET(LinkProgram,object_noop); SET(DeleteShader,del_shader); SET(DeleteProgram,del_program);
#ifdef TEST_VITA
    SET(LinkProgram,link_program); SET(GetAttribLocation,uniform_location);
#endif
    SET(GetUniformLocation,uniform_location); SET(UseProgram,object_noop); SET(BindAttribLocation,attrib_location);
    SET(Enable,enum_noop); SET(Disable,enum_noop); SET(BlendEquation,enum_noop); SET(BlendFunc,two_enums);
    SET(Viewport,view); SET(Scissor,clip); SET(Uniform4f,uniform4); SET(Uniform1i,uniform1i); SET(Uniform1f,uniform1f);
    SET(Uniform2f,uniform2); SET(Uniform3f,uniform3); SET(UniformMatrix4fv,matrix);
    SET(GenBuffers,gen); SET(DeleteBuffers,del_depth); SET(BindBuffer,bind_tex); SET(BufferData,buffer_data);
    SET(BufferSubData,buffer_sub); SET(VertexAttribPointer,attrib_ptr); SET(EnableVertexAttribArray,object_noop);
    SET(DisableVertexAttribArray,object_noop); SET(DrawArrays,draw_arrays);
    SET(ReadBuffer,enum_noop); SET(ActiveTexture,enum_noop); SET(ClearColor,four_floats); SET(Clear,clear); SET(BlendColor,four_floats);
    SET(MatrixMode,enum_noop); SET(LoadIdentity,no_args); SET(Ortho,ortho); SET(Frustum,ortho);
    SET(Translated,three_doubles); SET(Scaled,three_doubles); SET(ClearDepth,one_double);
    SET(MultiTexCoord2f,multi_coord); SET(Begin,enum_noop); SET(End,no_args); SET(Vertex2f,two_floats); SET(Translatef,three_floats);
    SET(Scalef,three_floats); SET(Rotatef,four_float_rotate);
#undef SET
    g_glMaxTextureSize = 16384;
    g_videoWidth = g_targetWidth = 640; g_videoHeight = g_targetHeight = 480;
    g_videoDepth = g_targetBpp = 32; g_renderMode = RENDER_MODE_GL;
    g_videoFlags = VIDEO_HWBLIT; g_videoSdlFlags = SDL_DOUBLEBUF;
#ifdef TEST_GLES
    g_glesReady = 1;
#endif
}

static void test_sizes_uploads(void)
{
    unsigned char px[16] = {255,0,0,255};
    GlTex *tex;
    size_t row = 0, bytes = 0;
    assert(GlImage_Size(2,2,4,&row,&bytes) && row == 8 && bytes == 16);
    assert(!GlImage_Size(0,2,4,NULL,NULL)); assert(!GlImage_Size(-1,2,4,NULL,NULL));
    assert(!GlImage_Size(32768,32768,4,NULL,NULL)); assert(!GlImage_Size(INT_MAX,INT_MAX,4,NULL,NULL));
    assert(!GlImage_Size(2,2,SIZE_MAX,NULL,NULL));
    tex = GlTex_Create(); assert(tex);
    image_calls = sub_calls = 0;
    fail_image = 1; assert(!GlTex_UploadRGBA(2,2,tex,px));
    assert(tex->w == 0 && tex->h == 0 && tex->size == 0);
    assert(GlTex_UploadRGBA(2,2,tex,px));
    assert(image_calls == 2 && sub_calls == 0 && tex->w == 2 && tex->size == 16);
    assert(GlTex_UploadRGBA(2,2,tex,px) && sub_calls == 1);
    assert(GlTex_UploadRGBA(2,2,tex,NULL) && image_calls == 3);
    fail_image = 1; assert(!GlTex_UploadRGBA(2,2,tex,px));
    assert(GlTex_UploadRGBA(2,2,tex,px) && image_calls == 4);
    assert(!GlTex_UploadRGBA(32768,32768,tex,px));
    fail_depth = 1; { int n = deleted_depth; assert(!GlTex_AllocDepth(tex)); assert(tex->depth == 0 && deleted_depth == n+1); }
    fail_depth = 0; assert(GlTex_AllocDepth(tex) && tex->depth != 0);
    assert(GlTex_Free(tex));
    puts("ok: checked sizes, failed upload/retry, depth allocation");
}

static void test_targets_capture(void)
{
    GlTex a = {0}, b = {0}; Sprite sa = {0}, sb = {0};
    SpriteSet set_a = {0}, set_b = {0}; KeyedArray ka = {0}, kb = {0};
    void *p;
    a.id = 81; a.w = 4; a.h = 2; a.depth = 91;
    b.id = 82; b.w = 8; b.h = 4; b.depth = 92;
    sa.format = sb.format = SPRITE_FORMAT_TEXTURE; sa.pixels = &a; sb.pixels = &b;
    sa.w=4;sa.h=2;sa.bpp=32;sb.w=8;sb.h=4;sb.bpp=32;
    ka.data=(void *)&sa;kb.data=(void *)&sb;ka.elem_size=kb.elem_size=sizeof(Sprite);ka.capacity=kb.capacity=1;
    set_a.sprites=&ka;set_b.sprites=&kb;
    assert(Gfx_SetRenderTarget(&set_a,0));
    assert(g_renderTarget == &set_a && color_attachment == 81);
    fail_fbo=1;
    assert(!Gfx_SetRenderTarget(&set_b,0));
    assert(g_renderTarget == &set_a && g_targetWidth == 4 && g_targetHeight == 2);
    assert(color_attachment == 81 && depth_attachment == 91 && binding != 0 && g_glBoundFramebuffer != 0);
    assert(viewport[2] == 4 && viewport[3] == 2);
    fail_fbo=0;
#ifdef TEST_GLES
    g_gfxPrePresent=overlay;
    Gfx_SetClipRect(1,0,3,2);
    Gl_Present(&set_a,0,0,0,4,2);
#ifdef TEST_LINUX_GLES
    assert(swaps == 1);
#else
    assert(swaps == 0); /* Screen_EndFrame owns the Android swap. */
#endif
    assert(overlays == 1 && g_renderTarget == &set_a && g_targetWidth == 4);
    assert(binding != 0 && color_attachment == 81 && viewport[2] == 4);
    assert(g_clipX1 == 1 && g_clipX2 == 3);
    g_gfxPrePresent=NULL;
#endif
    fail_read=1; assert(Gl_Capture() == NULL);
    fail_read=0; p=Gl_Capture(); assert(p); free(p);
    g_targetWidth = INT_MAX; assert(Gl_Capture() == NULL); g_targetWidth=4;
    assert(Gfx_SetRenderTarget(NULL,0)); assert(binding == 0);
    puts("ok: failed FBO preserves target/attachments/viewport; readback rejects failure");
}

#ifdef TEST_GLES
void Android_GLDrawableSize(int *w,int *h) { *w=1280;*h=960; }
void Sdl2_GLDrawableSize(int *w,int *h) { *w=1280;*h=960; }
void SDL_GL_SwapBuffers(void) { assert(binding == 0);swaps++; }
static void overlay(SpriteSet *set,int index)
{
    overlays++;
    assert(set == NULL && index == 0 && g_renderTarget == NULL && binding == 0);
    assert(viewport[2] == 1280 && viewport[3] == 960);
    Gfx_SetClipRect(10,20,30,40);
    assert(scissor[0] == 20 && scissor[2] == 40 && scissor[3] == 40);
}
static void test_programs_lifetime(void)
{
    GlTex *tex;
    int n;
    gles_free_programs();
    compile_calls=0; fail_compile_at=3;
    n=deleted_programs;
    assert(!gles_build_programs()); assert(deleted_programs == n+1);
    assert(g_glesProgramIndexed.program == 0 && g_glesProgramRgba.program == 0);
    fail_compile_at=0; assert(gles_build_programs());
    assert(g_glesProgramSolid.u_color == 3);
    gles_solid_quad(0,0,1,1,1,1,1,1); assert(last_color_location == 3);
    Gl_Deinit(); color_location=7; assert(Gl_Init(1));
    gles_solid_quad(0,0,1,1,1,1,1,1); assert(last_color_location == 7);
    g_gfxPrePresent=overlay; g_glProjection=1; Gfx_SetClipRect(1,2,30,40);
    Gl_Present(NULL,0,0,0,0,0); assert(overlays == 2 && g_glProjection == 1);
    assert(g_clipX1 == 1 && g_clipY1 == 2 && g_clipX2 == 30 && g_clipY2 == 40);
    g_gfxPrePresent=NULL;
    tex=GlTex_Create(); assert(tex && GlTex_Alloc(tex,2,2));
    assert(GlTex_SetBacking(tex,calloc(1,16),16,2,2,32));
    tex->refs=2; n=g_glesTextureCount;
    assert(!GlTex_Free(tex)); assert(tex->refs == 2 && g_glesTextureCount == n && g_glesBackingCount == 1);
    tex->refs=0; assert(GlTex_Free(tex)); assert(g_glesBackingCount == 0);
    tex=GlTex_Create(); assert(tex && GlTex_Alloc(tex,2,2) && GlTex_AllocDepth(tex));
    assert(Gl_AttachFramebuffer(tex->id,tex->depth));
    g_glesReady=0; n=deleted_textures; Gl_Deinit(); assert(deleted_textures == n+1 && tex->id == 0 && tex->depth == 0 && g_glesFbo == 0);
    n=deleted_textures; Gles_OnContextLost(); Gl_Deinit(); assert(deleted_textures == n);
    assert(GlTex_Free(tex));
    /* Loss has no current context: forget names without deleting them. */
    g_glesReady=1; tex=GlTex_Create(); assert(tex && GlTex_Alloc(tex,2,2));
    n=deleted_textures; Gles_OnContextLost(); assert(deleted_textures == n && tex->id == 0);
    assert(GlTex_Free(tex));
    version="OpenGL ES 2.0 mock"; assert(!Gles_Probe(NULL)); version="OpenGL ES 3.0 mock";assert(Gles_Probe(NULL));
    free(g_glesTextures); g_glesTextures=NULL;g_glesTextureCount=g_glesTextureCapacity=0;
    free(g_glesBacking);g_glesBacking=NULL;g_glesBackingCount=g_glesBackingCapacity=0;
    puts("ok: program rebuild locations, overlays, partial cleanup, retained ownership, context loss/deinit");
}
#else
int Test_LoadGL(void) { return 1; }
void Gfx_GlDrawLine(float a,float b,float c,float d,unsigned int color) {}
/* Calibration fixtures: isolate readback and shader lifecycle from the sprite
 * engine; the returned checkerboard still passes through real validation. */
int Sprite_Init(unsigned int flags,Sprite *s,int fmt,void *p,size_t size,int w,int h,int bpp,int x,int y)
{
    memset(s,0,sizeof(*s));s->pixels=p;s->w=w;s->h=h;s->bpp=bpp;
    if(bpp==8){ GlTex *t=calloc(1,sizeof(*t));assert(t);t->id=77;free(p);s->pixels=t;s->format=SPRITE_FORMAT_TEXTURE; }
    return 1;
}
void Sprite_Free(Sprite *s) { free(s->pixels);s->pixels=NULL; }
int SpriteSet_AddSprite(SpriteSet *set,const Sprite *s,int g,int n)
{
    set->sprites=calloc(1,sizeof(KeyedArray));if (!set->sprites) return 0;
    set->data=malloc(sizeof(*s)); if (!set->data) return 0;memcpy(set->data,s,sizeof(*s));
    set->sprites->data=set->data;set->sprites->elem_size=sizeof(*s);set->sprites->capacity=1;return 1;
}
int SpriteSet_AddPalette(SpriteSet *set,const Palette *p,int g,int n) { return 1; }
int SpriteSet_LinkPalette(SpriteSet *set,Sprite *s,int g,int n) { return 1; }
int SpriteSet_Free(SpriteSet *set) { if(set){ if(set->data){Sprite_Free(set->data);free(set->data);}free(set->sprites);free(set);}return 1; }
void Sprite_DrawSoft(SpriteSet *set,int s,const DrawParams *dp) {}
void Test_DrawGL(SpriteSet *set,int s,const DrawParams *dp) {}
/* Sprite_DrawGL is supplied by desktop glrender.c and not invoked here:
 * select ALT to isolate calibration's CPU image acceptance. */
int Pal_Init(Palette *p,int count,void *colors,int flags) { memset(p,0,sizeof(*p));return 1; }
void Pal_Free(Palette *p) {}
Palette *SpriteSet_GetPalette(SpriteSet *s,int i) { static GlTex t;static Palette p;p.is_texture=1;p.data=&t;return &p; }
void Pal_Convert(Palette *p) {}
Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b) { return r | (g<<8) | (b<<16); }
static void test_programs_lifetime(void)
{
    GlProgram p = {0};const char *defs[]={NULL};int n;
#ifdef TEST_VITA
    /* Missing runtime module, shader syntax failure and an error despite a
     * positive status must all stop before program creation/attachment/link. */
    { int before_links=links, before_hints=compiler_hints;
      int before_programs=program_creations, before_attachments=attachments;
      preflight_failure=1;n=compile_calls;
      assert(!Gl_LoadShaders() && compile_calls == n && links == before_links);
      assert(program_creations == before_programs && attachments == before_attachments);
      preflight_failure=0;
      semantic_mode=VGL_MODE_POSTPONED;compiler_missing=1;n=deleted_shaders;
      assert(!Gl_LoadShader(defs,&p,"rgba"));
      assert(links == before_links);
      assert(deleted_shaders == n+1 && compiler_hints == before_hints+1);
      compiler_missing=0;compile_error_only=1;
      assert(!Gl_LoadShader(defs,&p,"rgba") && links == before_links);
      compile_error_only=0;
      fail_compile_at=compile_calls+2;n=deleted_shaders;
      assert(!Gl_LoadShader(defs,&p,"rgba") && links == before_links);
      assert(deleted_shaders == n+2);fail_compile_at=0;
      assert(program_creations == before_programs && attachments == before_attachments);
      assert(Gl_LoadShaders() && links == before_links+2*GL_PROGRAM_COUNT);
      for (int j=0; j<GL_PROGRAM_COUNT; j++) {
          assert(g_glProgramsIndexed[j].mul == 73);
          assert(g_glProgramsRgba[j].mul == 73);
      }
      { unsigned old_program=g_glProgramsIndexed[0].program; int old_compiles=compile_calls;
        preflight_failure=1;
        assert(!Gl_LoadShaders() && compile_calls == old_compiles);
        assert(g_glProgramsIndexed[0].program == old_program);
        preflight_failure=0;
        missing_vita_mul=1;
        assert(!Gl_LoadShaders());
        assert(g_glProgramsIndexed[0].program == old_program);
        { const char *disabled[]={"PALFX_DISABLE",NULL}; GlProgram optimized={0};
          assert(Gl_LoadShader(disabled,&optimized,"rgba") && optimized.mul == -1);
          Glr_FreeProgram(&optimized);
        }
        missing_vita_mul=0;
      }
      Gl_FreeShaders();
      native_display_owned=1;
      assert(Error_ShowDialog("compiler failed","Error") == 0 && sdl_dialogs == 0);
      assert(Error_ShowDialog(NULL,NULL) == 0 && sdl_dialogs == 0);
      native_display_owned=0;
      assert(Error_ShowDialog("System error","Error") == 0 && sdl_dialogs == 1);
      assert(Error_ShowDialog(NULL,"Error") == 0 && sdl_dialogs == 2);
    }
    puts("ok: Vita eager compile, missing compiler, GL error, fragment failure and all variants");
#endif
    shader_ok=0; n=deleted_shaders;assert(!Gl_LoadShader(defs,&p,"rgba"));assert(deleted_shaders == n+1 && p.program == 0);
    shader_ok=1;link_ok=0;n=deleted_programs;assert(!Gl_LoadShader(defs,&p,"rgba"));assert(deleted_programs == n+1);
    link_ok=1;assert(Gl_LoadShader(defs,&p,"rgba"));
#ifdef TEST_VITA
    assert(p.mul == 73);
#else
    assert(p.mul == 74);
#endif
    { unsigned id=p.program; shader_ok=0;assert(!Gl_LoadShader(defs,&p,"rgba") && p.program == id);shader_ok=1; }
    Glr_FreeProgram(&p);
    assert(remove("data/rgba.frag") == 0);assert(!Gl_LoadShaders());
    assert(g_glProgramsIndexed[0].program == 0); Gl_Deinit();
    /* A partially owned resource must be cleaned even without ready=true. */
    g_glSysTex[0]=GlTex_Create(); assert(g_glSysTex[0]);g_glInitialized=0;n=deleted_textures;
    Gl_Deinit();assert(deleted_textures == n+1 && g_glSysTex[0] == NULL);
    fail_gen_at=gen_calls+2;n=deleted_textures;
    assert(!Gl_Init(0));assert(deleted_textures == n+1 && g_glSysTex[0] == NULL && g_glSysTex[1] == NULL);
    fail_gen_at=0;
    { FILE *f=fopen("data/indexed.vert","rb");char *text;assert(f && fseek(f,1,SEEK_SET)==0);
      text=Gl_ReadFile(f);assert(text && !strcmp(text,"oid main(){}\n"));free(text);assert(Gl_ReadFile(NULL)==NULL); }
    g_renderMode=RENDER_MODE_GL_ALT;g_videoWidth=g_targetWidth=4;g_videoHeight=g_targetHeight=4;
    read_mode=1;assert(!GlDetect_TestSprite(4,4,0,0,0));
    read_mode=2;assert(!GlDetect_TestSprite(4,4,0,0,0));
    read_mode=0;assert(GlDetect_TestSprite(4,4,0,0,0));
    fail_read=1;assert(!GlDetect_TestSprite(4,4,0,0,0));fail_read=0;
    shader_ok=0;n=deleted_shaders;assert(!GlDetect_TestIndexShader("bad"));assert(deleted_shaders == n+1);shader_ok=1;
    g_renderMode=RENDER_MODE_GL;g_videoWidth=g_targetWidth=16;g_videoHeight=g_targetHeight=16;
    fail_read=1;n=deleted_programs;assert(!GlDetect_TestIndexShader("valid"));
    assert(deleted_programs == n+1);fail_read=0;
    Glr_FreePremultiply();
    puts("ok: compile/link/missing-source failures, transactional replacement, partial cleanup, blank calibration");
}
#endif

int main(void)
{
#ifdef TEST_EXPECT_BITS
    assert(sizeof(void *)*8 == TEST_EXPECT_BITS);
#endif
    printf("abi: %u-bit build\n",(unsigned)(sizeof(void *)*8));
    install();test_sizes_uploads();test_targets_capture();test_programs_lifetime();
    puts("ok: GL error regression tests passed");
    return 0;
}
