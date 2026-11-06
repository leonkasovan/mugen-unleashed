/*
 * GLES capability probe (M8, RENDERING_PLAN.md section 12).
 *
 * Answers the only question the mode selector may ask before choosing the
 * GLES renderer: is this context a usable OpenGL ES 3.0+ context with the
 * features the engine's GLES backend needs? Everything the desktop GL probe
 * gates on that GLES guarantees by version (NPOT textures, FBO) is accepted
 * by version string, not by extension name -- plan section 12.3: do not
 * require ARB_fragment_program (it does not exist in GLES; the engine's
 * programs are GLSL ES, compiled and linked explicitly below).
 *
 * Returns 1 and fills *caps (GLES_CAP_* below) on success, 0 otherwise.
 * Logs every decision once via MatchLog_Printf (M6 section 10.10 style).
 */
#include "mugen.h"

#ifdef MUGEN_GLES

#include "gles_probe.h"

/* Parse "OpenGL ES X.Y ..." -- plan 12.3: a GLES 3.x string does NOT make
 * GLSL 120 valid; the dialect is selected from these numbers, and only the
 * ES shaders (res/shaders/gles, deployed as data/gles) are ever offered to
 * this context. */
/* Port fix: this backend uses ES3 formats (R8/RED), not generic ES2. */
static int Gles_ParseVersion(const char *s, int *major, int *minor)
{
    return s != NULL && sscanf(s, "OpenGL ES %d.%d", major, minor) == 2;
}

int Gles_Probe(unsigned int *caps)
{
    const char *ver;
    int major = 0, minor = 0;

    if (caps != NULL) {
        *caps = 0;
    }
    ver = (const char *)glGetString(GL_VERSION);
    if (ver == NULL) {
        MatchLog_Printf(3, "GLES: no GL context (glGetString(GL_VERSION) is NULL).\n");
        return 0;
    }
    MatchLog_Printf(7, "GLES version: %s\n", ver);
    if (!Gles_ParseVersion(ver, &major, &minor)) {
        MatchLog_Printf(3, "GLES: unrecognised version string: %s\n", ver);
        return 0;
    }
    if (major < 3) {
        MatchLog_Printf(3, "GLES: version %d.%d below ES 3.0.\n", major, minor);
        return 0;
    }
    /* ES 3.0+ guarantees NPOT textures and framebuffer objects by version;
     * no extension-name check (plan 12.3). */
    if (caps != NULL) {
        *caps = GLES_CAP_NPOT | GLES_CAP_FBO | GLES_CAP_GLSL_ES_100;
    }
    {
        GLint maxtex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
        MatchLog_Printf(7, "GLES: Max texture size %d.\n", (int)maxtex);
    }
    return 1;
}

#endif /* MUGEN_GLES */
