/*
 * PS Vita port: GL capability probing, replacing glad's loader.
 *
 * glad (third_party/glad-2.1) is generated for the desktop GL loader -- it
 * dlopens libGL.so / LoadLibraryW(opengl32.dll) and resolves every entry point
 * through glXGetProcAddressARB / wglGetProcAddress. The Vita has neither a
 * libGL nor a loader: vita_video.c initializes vitaGL directly. Entry points
 * are resolved by the linker, and the SDL 2D driver is bypassed for GL.
 *
 * glrender.c calls the two functions declared here in place of glad's
 * gladLoadGL() and the GLAD_GL_* flags. See vita_gl_shim.c for the
 * implementation and port/psvita/README.md for the whole story.
 */
#ifndef MUGEN_VITA_GL_SHIM_H
#define MUGEN_VITA_GL_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Probes the current context and fills the engine's own g_glew* capability
 * flags (src/globals.c), which Gl_Init() tests. Returns 1 when the context is
 * GL >= 2.0 with NPOT textures and framebuffer objects, 0 otherwise. An
 * owned context is required before driver strings or GL objects are read.
 *
 * Replaces: gladLoadGL() plus the GLAD_GL_VERSION_2_0 / GLAD_GL_ARB_* /
 * GLAD_GL_EXT_* reads.
 */
int VitaGl_Probe(void);

/*
 * The three GL entry points vitaGL does not provide, defined in vita_gl_shim.c
 * with the semantics the desktop driver has. src/symbols.h maps the original's
 * g_glew* names onto them under MUGEN_VITA_GL, so glrender.c is unchanged.
 */
void        MugenVita_BlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
GLboolean   MugenVita_IsShader(GLuint shader);
GLuint      MugenVita_TrackShader(GLuint shader);
void        MugenVita_MultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void        MugenVita_LogicOp(GLenum opcode);

/*
 * V4 capability: 1 when the driver can honour glBlendColor /
 * GL_CONSTANT_ALPHA. vitaGL cannot, so this is 0 and MugenVita_BlendColor
 * logs exactly what degrades instead of silently doing nothing.
 */
extern int MugenVita_ConstantAlphaSupported;

/*
 * GL constants vitaGL does not define. Desktop GL values, so the guarded block
 * in Gfx_GlInvertPixel compiles; it is unreachable because the port's
 * EXT_blend_logic_op probe always reports the extension absent.
 */
#ifndef GL_LOGIC_OP
#define GL_LOGIC_OP 0x0BF1
#endif
#ifndef GL_XOR
#define GL_XOR 0x1506
#endif

/*
 * Port fix: the framebuffer-binding query. GL_FRAMEBUFFER_BINDING (0x8CA6) is
 * the core GL 3.0 spelling, which is what vitaGL and the GLES headers use; the
 * desktop glad set this project builds against (third_party/glad-2.1) carries
 * only the EXT_framebuffer_object alias GL_FRAMEBUFFER_BINDING_EXT. Both name
 * the same enum, so the missing alias is supplied here instead of in
 * src/glrender.c, which keeps the original's single spelling (Gl_AttachFramebuffer
 * queries the currently bound framebuffer with it) and still compiles on every
 * target.
 */
#ifndef GL_FRAMEBUFFER_BINDING_EXT
#define GL_FRAMEBUFFER_BINDING_EXT GL_FRAMEBUFFER_BINDING
#endif

/*
 * Port fix: same idea in the other direction. vitaGL.h defines
 * GL_FRAMEBUFFER_COMPLETE and GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT but
 * not GL_FRAMEBUFFER_UNSUPPORTED (0x8CDD, the value every other GL header
 * gives it). vita_gl_shim.c uses it only as the initial value of the FBO
 * probe's status, which glCheckFramebufferStatus overwrites.
 */
#ifndef GL_FRAMEBUFFER_UNSUPPORTED
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#endif

#ifdef __cplusplus
}
#endif

#endif /* MUGEN_VITA_GL_SHIM_H */
