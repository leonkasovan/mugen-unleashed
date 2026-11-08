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
 * The Vita has no desktop loader. vita_video.c initializes vitaGL directly
 * before this probe runs; SDL's separate 2D driver does not create that
 * context. GL entry points are resolved at link time.
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

/* mugen.h supplies vitaGL before SDL, and the owned-context interface. */
#include "mugen.h"
#include "vita_gl_shim.h"
#include "vita_caps.h"

/* The constant colour recorded by MugenVita_BlendColor (RGBA). */
static GLfloat MugenVita_blendColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

/* Handles created by glCreateShader, for MugenVita_IsShader. */
#define MUGEN_VITA_MAX_SHADERS 64
static GLuint MugenVita_shaderHandles[MUGEN_VITA_MAX_SHADERS];

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

/*
 * V4 capability (not a g_glew* flag): 1 when the driver can honour
 * glBlendColor / GL_CONSTANT_ALPHA. vitaGL cannot, so this stays 0 and the
 * constant-alpha blend the engine uses for BLEND_ADD/BLEND_ALPHA sprites is
 * approximated instead of being silently claimed to work. Read by
 * tools/test_vita_caps.sh through MugenVita_ResolveCaps and logged once by
 * VitaGl_Probe.
 */
int MugenVita_ConstantAlphaSupported = 0;

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
    static int logged;

    /*
     * V4 (RENDERING_PLAN.md section 11): an empty function is not accepted as
     * rendering parity. vitaGL has no glBlendColor, so the constant the
     * engine's GL_CONSTANT_ALPHA blend reads cannot be supplied. Rather than
     * pretend, the value is recorded, the capability flag says it is
     * unsupported, and the first use logs exactly what is lost.
     *
     * What degrades: Gl_DrawSprite sets glBlendFunc(GL_ONE, GL_CONSTANT_ALPHA)
     * for BLEND_ALPHA sprites and glBlendColor(alpha_src, ..., alpha_dst).
     * Without the constant, the destination factor stays 1 and the sprite
     * composites additively instead of at the requested alpha. BLEND_ADD, the
     * masked path and every non-constant-alpha blend are unaffected.
     */
    MugenVita_blendColor[0] = red;
    MugenVita_blendColor[1] = green;
    MugenVita_blendColor[2] = blue;
    MugenVita_blendColor[3] = alpha;
    if (!logged) {
        logged = 1;
        Log_Printf("GL: glBlendColor(%.3f,%.3f,%.3f,%.3f) ignored -- vitaGL has no "
                   "constant-alpha blend; BLEND_ALPHA sprites composite additively\n",
                   (double)red, (double)green, (double)blue, (double)alpha);
    }
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
    static int logged;

    /*
     * V4: declared unsupported rather than implemented as a silent no-op.
     * The engine reaches this only from Gfx_GlInvertPixel, which is guarded
     * by the EXT_blend_logic_op capability flag; vitaGL reports the extension
     * absent, so this path is unreachable today. If a future vitaGL gains
     * logic ops the flag flips and this must call glLogicOp for real -- until
     * then, say so once instead of pretending the operation happened.
     */
    if (!logged) {
        logged = 1;
        Log_Printf("GL: glLogicOp(0x%X) unsupported (no EXT_blend_logic_op); "
                   "invert-pixel fallback used\n", (unsigned)opcode);
    }
}

/*
 * Fills the capability flags Gl_Init() reads. Returns 1 when a GL 2.0 context
 * with the required extensions is present, 0 otherwise. Gl_Init() now treats 0
 * as fatal: the flags are published before the checks run, so a caller that
 * only re-checked them would accept a context the probe has just rejected.
 */
int VitaGl_Probe(void)
{
    const char *version;
    const char *extensions;
    MugenVitaCaps caps;
    int ok;

    if (!VitaGl_HasContext()) {
        Log_Printf("GL: no owned vitaGL context; probe rejected\n");
        return 0;
    }
    version = (const char *)glGetString(GL_VERSION);
    extensions = (const char *)glGetString(GL_EXTENSIONS);

    /*
     * Version parsing, extension matching and the capability rules live in
     * vita_caps.c: pure logic with no GL calls, so it is unit-tested on the
     * host (tools/test_vita_caps.sh) instead of only on the console. This
     * function's job is to read the driver strings, resolve, publish the
     * flags Gl_Init() reads, and log the verdict.
     *
     * VitaGL reports "OpenGL ES 2.0 VitaGL" -- the version is not at the
     * start of the string -- which the resolver handles by scanning for the
     * first "<digits>.<digits>" pair.
     *
     * constant_alpha_supported is 0: vitaGL has no glBlendColor (V4), so
     * GL_CONSTANT_ALPHA blending cannot be honoured. Recording it as a
     * capability (instead of implying it works) is what makes the gap
     * visible in the log and testable.
     */
    ok = MugenVita_ResolveCaps(version, extensions, 0, &caps);

    MugenVita_GLVersion20 = caps.gl2;
    MugenVita_GLArbTextureNpot = caps.npot;
    MugenVita_GLExtFramebufferObject = caps.fbo;
    /* Not required by the engine's shaders (plain GLSL); reported for the
     * log only, so a driver that happens to advertise it changes nothing. */
    MugenVita_GLArbFragmentProgram = 0;
    MugenVita_GLExtBlendLogicOp = caps.blend_logic_op;
    MugenVita_ConstantAlphaSupported = caps.constant_alpha;

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
    Log_Printf("GL: flags 2.0=%d npot=%d fbo=%d blendlogic=%d constantalpha=%d\n",
               caps.gl2, caps.npot, caps.fbo, caps.blend_logic_op, caps.constant_alpha);
    if (!caps.programmable) {
        Log_Printf("GL: no GLSL path (below 2.0)\n");
    }
    if (!caps.constant_alpha) {
        /* Honest capability statement (V4): constant-alpha blending degrades. */
        Log_Printf("GL: constant-alpha blending unavailable (no glBlendColor); "
                   "BLEND_ADD/ALPHA sprites approximate\n");
    }

    /*
     * FBO completeness (V2: "Periksa format attachment/depth dan FBO
     * completeness"). A context that creates but cannot render to a
     * framebuffer fails the mode just as a missing extension does.
     */
    if (ok && caps.fbo) {
        GLuint probe_fbo = 0;
        GLuint probe_tex = 0;
        GLenum status = GL_FRAMEBUFFER_UNSUPPORTED;

        /*
         * Fix (port change): the probe used to check a framebuffer with no
         * attachment. That is incomplete by definition -- GLES reports
         * GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT (0x8CD7) -- so the
         * check rejected every driver that could actually render, including
         * the one it was meant to qualify. Attach a colour texture of the kind
         * the renderer itself draws into, then check. 16x16 keeps the render
         * target inside every GLES2 limit.
         */
        glGenFramebuffers(1, &probe_fbo);
        glGenTextures(1, &probe_tex);
        if (probe_fbo != 0 && probe_tex != 0) {
            GLint prev = 0;
            GLint prev_tex = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
            glBindTexture(GL_TEXTURE_2D, probe_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, probe_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probe_tex, 0);
            status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
            glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
            glDeleteFramebuffers(1, &probe_fbo);
            glDeleteTextures(1, &probe_tex);
            Log_Printf("GL: FBO probe status 0x%X (complete=0x%X)\n", (unsigned)status,
                       (unsigned)GL_FRAMEBUFFER_COMPLETE);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                Log_Printf("GL: framebuffer incomplete; GL mode unavailable\n");
                ok = 0;
            }
        } else {
            if (probe_fbo != 0) {
                glDeleteFramebuffers(1, &probe_fbo);
            }
            if (probe_tex != 0) {
                glDeleteTextures(1, &probe_tex);
            }
            Log_Printf("GL: could not create a probe framebuffer\n");
            ok = 0;
        }
    }

    return ok;
}
