/*
 * OpenGL loading on the PS Vita.
 *
 * The engine reaches GL in three steps, all of them Windows/driver specific:
 *
 *   1. src/mugen.h includes <glad.h> *before* <SDL.h>, so glad's declarations
 *      are the single source of GL and SDL_opengl.h's <GL/gl.h> self-suppresses.
 *   2. src/glrender.c calls gladLoadGL(), which dlopens libGL.so /
 *      LoadLibraryW(opengl32.dll) and resolves the entry points through
 *      glXGetProcAddressARB / wglGetProcAddress.
 *   3. src/glrender.c reads glad's feature flags (GLAD_GL_VERSION_2_0 and the
 *      per-extension flags) to decide whether the driver can run the renderer.
 *
 * None of that exists on the Vita. There is no libGL and no loader: VitaSDK's
 * SDL 1.2 OpenGL driver sits on **vitaGL**, which implements the GL 1.x/2.1
 * fixed-function pipeline over SceGxm and links its entry points directly into
 * the executable. So the entry points are resolved at link time and there is
 * nothing for glad to load.
 *
 * What this file provides, therefore, is the third step only:
 *
 *   VitaGl_Probe()   fills the capability flags Gl_Init() checks.
 *
 * It lives in port/psvita/src rather than in src/ on purpose: src/ is the
 * reconstruction and must keep matching the original binary. The call site in
 * glrender.c is the one #ifdef MUGEN_VITA branch, marked as a port.
 *
 * The flags are named MugenVita_* rather than glad's GLAD_* names on purpose:
 * src/symbols.h maps the g_glew* macros onto them under MUGEN_VITA_GL, so
 * src/ keeps its original vocabulary and the Windows/Linux builds (which use
 * glad's real globals) are untouched.
 *
 * Quirk kept from the original: the capability flags are derived the same way
 * GLEW's are -- version from glGetString(GL_VERSION), extensions from the
 * GL_EXTENSIONS string -- rather than from the library's own constants, so a
 * vitaGL update that drops an extension is noticed here exactly as GLEW would
 * have noticed it on Windows.
 */
#include <string.h>

/* vitaGL's declarations; SDL.h pulls the same header through SDL_opengl.h. */
#include <SDL.h>
#include <SDL_opengl.h>

#include "mugen.h"
#include "vita_gl_shim.h"

/* The constant colour recorded by MugenVita_BlendColor (RGBA). */
static GLfloat MugenVita_blendColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

/* Handles created by glCreateShader, for MugenVita_IsShader. */
#define MUGEN_VITA_MAX_SHADERS 64
static GLuint MugenVita_shaderHandles[MUGEN_VITA_MAX_SHADERS];

/*
 * True when the space-separated extension list `list` contains exactly `name`
 * -- GLEW's glewGetExtension semantics, including the fact that a substring
 * match is not enough ("GL_EXT_framebuffer_object" must not match
 * "GL_EXT_framebuffer_object_2").
 */
static int VitaGl_HasExtension(const char *list, const char *name)
{
    size_t name_len = strlen(name);
    const char *p = list;

    if (list == NULL) {
        return 0;
    }
    while ((p = strstr(p, name)) != NULL) {
        const char *end = p + name_len;

        if ((p == list || p[-1] == ' ') && (*end == ' ' || *end == '\0')) {
            return 1;
        }
        p = end;
    }
    return 0;
}

/*
 * The capability flags, under the names glad would have defined and that
 * src/symbols.h maps the g_glew* macros onto. Declared here and not in glad.h
 * because glad is not part of this build (see the file comment).
 */
extern int MugenVita_GLVersion20;
extern int MugenVita_GLArbFragmentProgram;
extern int MugenVita_GLArbTextureNpot;
extern int MugenVita_GLExtBlendLogicOp;
extern int MugenVita_GLExtFramebufferObject;

/* Storage for the five flags above. glad.c supplies the equivalents elsewhere. */
int MugenVita_GLVersion20 = 0;
int MugenVita_GLArbFragmentProgram = 0;
int MugenVita_GLArbTextureNpot = 0;
int MugenVita_GLExtBlendLogicOp = 0;
int MugenVita_GLExtFramebufferObject = 0;

/* -------------------------------------------------- entry points vitaGL lacks */

/*
 * vitaGL implements GL 2.1 but not three of the entry points this engine calls.
 * Each is defined below with the semantics the desktop driver has, so the call
 * sites in glrender.c are unchanged. They are prefixed MugenVita_ and mapped
 * from src/symbols.h under MUGEN_VITA_GL only.
 */

/*
 * glBlendColor: sets the constant colour that GL_CONSTANT_ALPHA blending reads.
 *
 * vitaGL has no glBlendColor, so the value is recorded here and blending is
 * performed without it. This is the one real behavioural gap of the port: the
 * constant-alpha blend the engine uses for software-blended sprites
 * (Gl_DrawSprite sets glBlendFunc(GL_ONE, GL_CONSTANT_ALPHA) and then
 * glBlendColor(alpha_src, ..., alpha_dst)) becomes plain additive there.
 * Recorded in PLANS.md and port/psvita/README.md; it affects sprite blending
 * appearance, not game state, and the sprites affected are exactly the ones
 * whose blend mode is BLEND_ADD/BLEND_SUB/BLEND_ALPHA* on a 32-bit surface.
 */
void MugenVita_BlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    /* Remembered so a future vitaGL release that gains the call can use it. */
    MugenVita_blendColor[0] = red;
    MugenVita_blendColor[1] = green;
    MugenVita_blendColor[2] = blue;
    MugenVita_blendColor[3] = alpha;
}

/*
 * glIsShader: GL_TRUE when `shader` names a shader object.
 *
 * vitaGL provides neither glIsShader nor glIsProgram, and the engine uses
 * glIsShader only in Gl_LogInfo to decide whether a handle is a shader or a
 * program before asking for its info log. Shader and program names come from
 * the same namespace in GL, so without a query the caller cannot tell them
 * apart; MugenVita_TrackShader below records the handles the engine creates.
 */
GLboolean MugenVita_IsShader(GLuint shader)
{
    int i;

    if (shader == 0) {
        return GL_FALSE;
    }
    for (i = 0; i < MUGEN_VITA_MAX_SHADERS; i++) {
        if (MugenVita_shaderHandles[i] == shader) {
            return GL_TRUE;
        }
    }
    return GL_FALSE;
}

/* Records a shader handle for MugenVita_IsShader. Called by glCreateShader. */
GLuint MugenVita_TrackShader(GLuint shader)
{
    int i;

    if (shader == 0) {
        return shader;
    }
    for (i = 0; i < MUGEN_VITA_MAX_SHADERS; i++) {
        if (MugenVita_shaderHandles[i] == 0) {
            MugenVita_shaderHandles[i] = shader;
            return shader;
        }
    }
    /* Table full: the handle is still usable, only its classification is lost. */
    return shader;
}

/*
 * glMultiTexCoord4f: the four-component multitexture coordinate vitaGL does not
 * declare (it has glMultiTexCoord2f/-2fv only).
 *
 * The engine uses it for Q coordinates on streamed palettes. vitaGL's texture
 * coordinate handling goes through its own texture matrix, so setting (s, t) is
 * what has an effect; r and q are accepted and ignored, which is what a driver
 * without projective texturing does. Same limitation as GL_CONSTANT_ALPHA
 * above: appearance, not game state.
 */
void MugenVita_MultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    (void)r;
    (void)q;
    glMultiTexCoord2f(target, s, t);
}

/*
 * glLogicOp / GL_LOGIC_OP / GL_XOR.
 *
 * vitaGL implements no logic-op stage and therefore advertises no
 * EXT_blend_logic_op, which is why the port's flag probe reports it absent.
 * Gfx_GlInvertPixel (src/prim.c) guards on that flag and returns immediately on
 * the Vita -- but the guarded block must still compile, and vitaGL declares
 * neither the entry point nor the constants. They are provided here with
 * desktop GL's values; the function is unreachable while the flag is 0, which
 * the probe guarantees, so it does nothing.
 *
 * If vitagl ever gains logic ops, the flag probe picks it up from GL_EXTENSIONS
 * and this stub must be replaced by the real call.
 */
void MugenVita_LogicOp(GLenum opcode)
{
    (void)opcode;
}

/*
 * Fills the capability flags Gl_Init() reads. Returns 1 when a GL 2.0 context
 * with the required extensions is present, 0 otherwise -- the caller ignores
 * the value and re-checks the flags, exactly like the original ignored
 * glewInit()'s status.
 */
int VitaGl_Probe(void)
{
    const char *version;
    const char *extensions;
    int major = 0;
    int minor = 0;

    version = (const char *)glGetString(GL_VERSION);
    extensions = (const char *)glGetString(GL_EXTENSIONS);

    /*
     * GL_VERSION starts with "<major>.<minor>", optionally followed by vendor
     * text. Hand-parsed rather than with sscanf: the engine's CRT shim renames
     * the _snprintf family around, and this file stays free of <stdio.h>.
     */
    if (version != NULL) {
        const char *p = version;

        while (*p >= '0' && *p <= '9') {
            major = major * 10 + (*p - '0');
            p++;
        }
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9') {
                minor = minor * 10 + (*p - '0');
                p++;
            }
        }
    }

    MugenVita_GLVersion20 = (major > 2 || (major == 2 && minor >= 0)) ? 1 : 0;
    MugenVita_GLArbTextureNpot =
        VitaGl_HasExtension(extensions, "GL_ARB_texture_non_power_of_two");
    MugenVita_GLExtFramebufferObject =
        VitaGl_HasExtension(extensions, "GL_EXT_framebuffer_object");
    MugenVita_GLArbFragmentProgram =
        VitaGl_HasExtension(extensions, "GL_ARB_fragment_program");
    MugenVita_GLExtBlendLogicOp =
        VitaGl_HasExtension(extensions, "GL_EXT_blend_logic_op");

    /*
     * Report to mugen.log, not just the match log.
     *
     * Gl_Init sends its own verdict through MatchLog_Printf, which writes only
     * when -log was passed -- on the Vita there is no console to read and the
     * bubble takes no arguments, so a GL failure was completely silent and
     * looked like a bare "Can't change video mode". This is the one place that
     * knows what the driver actually reported, so it goes to the status log.
     */
    Log_Printf("GL: \"%s\"\n", (version != NULL) ? version : "(no context)");
    if (extensions != NULL) {
        Log_Printf("GL: extensions present\n");
    } else {
        Log_Printf("GL: no extension list (no GL context)\n");
    }
    Log_Printf("GL: flags 2.0=%d npot=%d fbo=%d fragprog=%d blendlogic=%d\n",
               MugenVita_GLVersion20, MugenVita_GLArbTextureNpot, MugenVita_GLExtFramebufferObject,
               MugenVita_GLArbFragmentProgram, MugenVita_GLExtBlendLogicOp);

    if (!MugenVita_GLVersion20) {
        return 0;
    }
    return MugenVita_GLArbTextureNpot && MugenVita_GLExtFramebufferObject &&
           MugenVita_GLArbFragmentProgram;
}