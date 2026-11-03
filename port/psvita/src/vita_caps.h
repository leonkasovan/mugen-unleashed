/*
 * PS Vita / GLES capability resolution -- pure logic, no GL calls.
 *
 * RENDERING_PLAN.md section 11 (V2) asks for a probe that accepts capability
 * by what is actually needed and tested, not by a single hardcoded extension
 * name. VitaGL is an OpenGL ES 2.0 implementation, so:
 *
 *  - NPOT textures are core in ES 2.0; the driver advertises GL_OES_texture_npot
 *    rather than GL_ARB_texture_non_power_of_two. Both names mean the same
 *    capability and either is accepted (V2: "Terima nama OES/ARB NPOT").
 *  - ARB_fragment_program does not exist in GLES at all, and the engine never
 *    uses it: the shader files under data/ are plain GLSL. Requiring it
 *    rejected every GLES context (V2: "Jangan mensyaratkan
 *    ARB_fragment_program").
 *  - Framebuffer objects are core in ES 2.0, so a 2.0+ version implies them
 *    even when the driver only advertises GL_EXT_framebuffer_object.
 *
 * Kept free of GL headers and of the engine headers so it can be compiled and
 * unit-tested on the host: tools/test_vita_caps.sh does exactly that. The
 * probe in vita_gl_shim.c feeds it the live strings and turns the result into
 * the engine's g_glew* flags.
 */
#ifndef MUGEN_VITA_CAPS_H
#define MUGEN_VITA_CAPS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MugenVitaCaps {
    int major;              /* parsed GL version */
    int minor;
    int gl2;                /* version >= 2.0 (desktop) or >= 2.0 ES */
    int npot;               /* ARB_texture_non_power_of_two or OES_texture_npot */
    int fbo;                /* EXT_framebuffer_object, or core by ES 2.0 */
    int blend_logic_op;     /* EXT_blend_logic_op */
    int programmable;       /* GLSL ES/GLSL path: always for 2.0+ */
    int constant_alpha;     /* glBlendColor + GL_CONSTANT_ALPHA usable */
} MugenVitaCaps;

/* Parses "<digits>.<digits>" anywhere in `version` (VitaGL reports
 * "OpenGL ES 2.0 VitaGL"). Returns 1 when a pair was found. */
int MugenVita_ParseVersion(const char *version, int *major, int *minor);

/* Exact-token search in a GL_EXTENSIONS list (GLEW's glewGetExtension
 * semantics: "GL_EXT_framebuffer_object" must not match a "_2" variant). */
int MugenVita_HasExtension(const char *list, const char *name);

/* Fills *caps from the two driver strings. `constant_alpha_supported` is the
 * driver's own answer (0 on vitaGL, which has no glBlendColor); pass it in so
 * this stays a pure function. Returns 1 when the context satisfies the
 * engine's GL mode requirements: GL >= 2.0, NPOT and FBO. */
int MugenVita_ResolveCaps(const char *version, const char *extensions, int constant_alpha_supported,
                          MugenVitaCaps *caps);

#ifdef __cplusplus
}
#endif

#endif /* MUGEN_VITA_CAPS_H */
