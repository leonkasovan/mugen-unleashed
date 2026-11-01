/*
 * PS Vita port: GL capability probing, replacing glad's loader.
 *
 * glad (third_party/glad-2.1) is generated for the desktop GL loader -- it
 * dlopens libGL.so / LoadLibraryW(opengl32.dll) and resolves every entry point
 * through glXGetProcAddressARB / wglGetProcAddress. The Vita has neither a
 * libGL nor a loader: VitaSDK's SDL 1.2 OpenGL driver runs on vitaGL, which
 * implements GL 1.x/2.1 over SceGxm and links its entry points directly, so
 * they are resolved by the linker and glad has nothing to do.
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
 * GL >= 2.0 with ARB_texture_non_power_of_two, EXT_framebuffer_object and
 * ARB_fragment_program, 0 otherwise.
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

#ifdef __cplusplus
}
#endif

#endif /* MUGEN_VITA_GL_SHIM_H */
