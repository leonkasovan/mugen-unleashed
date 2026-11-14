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
#include <string.h>

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


#ifdef MUGEN_DEBUG
/* Is `name` in the space-separated GL_EXTENSIONS string? */
static int gles_has_ext(const char *extensions, const char *name)
{
    size_t n = strlen(name);
    const char *p = extensions;

    while (p != NULL && *p != '\0') {
        const char *end = strchr(p, ' ');

        if ((end != NULL ? (size_t)(end - p) : strlen(p)) == n && strncmp(p, name, n) == 0) {
            return 1;
        }
        p = (end != NULL) ? end + 1 : NULL;
    }
    return 0;
}

void Gles_LogCapabilities(void)
{
    static const struct {
        const char *name;
        const char *why;
    } interesting[] = {
        { "GL_EXT_disjoint_timer_query", "GPU-side timing (measure GPU cost, not guesses)" },
        { "GL_EXT_disjoint_timer_query_webgl2", "GPU-side timing" },
        { "GL_KHR_debug", "driver error callbacks instead of glGetError polling" },
        /* Mali advertises the KHR spelling; checking only EXT_ was a false
         * negative that put "no texture compression" in the docs. */
        { "GL_KHR_texture_compression_astc_ldr", "ASTC textures (stage/character sheets at 1/6 memory)" },
        { "GL_EXT_texture_compression_astc_ldr", "ASTC textures (EXT spelling)" },
        { "GL_OES_texture_compression_astc", "ASTC textures (OES spelling)" },
        { "GL_EXT_discard_framebuffer", "skip tile writeback for unused attachments" },
        { "GL_OES_vertex_array_object", "vertex array objects" },
        { "GL_EXT_texture_compression_s3tc", "S3TC textures" },
        { "GL_EXT_texture_format_BGRA8888", "upload BGRA without swizzling" },
        { "GL_EXT_texture_storage", "immutable textures: no reallocation per upload" },
        { "GL_EXT_buffer_storage", "persistent/immutable buffers" },
        { "GL_EXT_map_buffer_range", "partial buffer updates without orphaning" },
        { "GL_EXT_multisampled_render_to_texture", "MSAA without a separate resolve" },
        { "GL_KHR_blend_equation_advanced", "advanced blends in hardware" },
        { "GL_EXT_shader_framebuffer_fetch", "read the framebuffer in-shader (Mali: cheap blending)" },
        { "GL_ARM_shader_framebuffer_fetch", "read the framebuffer in-shader (Mali native)" },
        { "GL_EXT_instanced_arrays", "instancing" },
        { "GL_EXT_sRGB", "sRGB framebuffer/textures" },
        { "GL_EXT_texture_filter_anisotropic", "anisotropic filtering" },
        { "GL_KHR_partial_update", "tile-based present: update only dirty tiles" },
        { "GL_EXT_EGL_image_array", "EGL image arrays" },
        { "GL_OES_EGL_image", "zero-copy EGL images" },
        { NULL, NULL }
    };
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *sl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    GLint v[2] = { 0, 0 };
    GLint i = 0;
    const char *prefix = "\ncaps: ";

    Log_Printf("%svendor=%s renderer=%s\n", prefix, vendor ? vendor : "?", renderer ? renderer : "?");
    Log_Printf("caps: version=%s sl=%s\n", version ? version : "?", sl ? sl : "?");

#define CAP1(label, enumval) do { GLint t = 0; glGetIntegerv(enumval, &t); Log_Printf("caps: %-28s %d\n", label, (int)t); } while (0)
    CAP1("MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE);
    CAP1("MAX_TEXTURE_IMAGE_UNITS", GL_MAX_TEXTURE_IMAGE_UNITS);
    CAP1("MAX_VERTEX_ATTRIBS", GL_MAX_VERTEX_ATTRIBS);
    CAP1("MAX_VERTEX_UNIFORM_VECTORS", GL_MAX_VERTEX_UNIFORM_VECTORS);
    CAP1("MAX_FRAGMENT_UNIFORM_VECTORS", GL_MAX_FRAGMENT_UNIFORM_VECTORS);
    CAP1("MAX_RENDERBUFFER_SIZE", GL_MAX_RENDERBUFFER_SIZE);
    CAP1("MAX_SAMPLES", 0x8D57 /* GL_MAX_SAMPLES */);
    CAP1("MAX_UNIFORM_BUFFER_BINDINGS", 0x8A2F);
    CAP1("MAX_UNIFORM_BLOCK_SIZE", 0x8A30);
    CAP1("MAX_ELEMENT_INDEX", 0x8D6B);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, v);
    Log_Printf("caps: %-28s %dx%d\n", "MAX_VIEWPORT_DIMS", (int)v[0], (int)v[1]);
#undef CAP1

    if (ext == NULL) {
        /*
         * MUGEN_GL_EXT=1 prints every extension the driver lists, not just the
         * curated set above: used to write docs/GLES_EXTENSIONS.md, and worth
         * keeping for the next device.
         */
        Log_Printf("caps: no GL_EXTENSIONS string (ES 3.0+ exposes glGetStringi instead)\n");
        glGetIntegerv(0x821D /* GL_NUM_EXTENSIONS */, &i);
        Log_Printf("caps: %-28s %d\n", "NUM_EXTENSIONS", (int)i);
        if (i > 0 && i < 4096) {
            GLint k;
            int shown = 0;

            for (k = 0; k < i; k++) {
                const char *name = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)k);
                int m;

                if (name == NULL) {
                    continue;
                }
                for (m = 0; interesting[m].name != NULL; m++) {
                    if (strcmp(name, interesting[m].name) == 0) {
                        Log_Printf("caps: + %-40s %s\n", name, interesting[m].why);
                        shown++;
                        break;
                    }
                }
            }
            Log_Printf("caps: %d extension(s) of interest\n", shown);
        }
        return;
    }
    {
        int m;
        int shown = 0;

        for (m = 0; interesting[m].name != NULL; m++) {
            if (gles_has_ext(ext, interesting[m].name)) {
                Log_Printf("caps: + %-40s %s\n", interesting[m].name, interesting[m].why);
                shown++;
            }
        }
        Log_Printf("caps: %d extension(s) of interest\n", shown);
    }

    /*
     * MUGEN_GL_EXT=1 prints every extension the driver lists, not just the
     * curated set above. Written to document a device (docs/GLES_EXTENSIONS.md);
     * kept for the next one.
     */
    {
        const char *want = getenv("MUGEN_GL_EXT");

        if (want != NULL && want[0] == '1') {
            GLint total = 0;
            GLint k;
            int printed = 0;

            glGetIntegerv(0x821D /* GL_NUM_EXTENSIONS */, &total);
            for (k = 0; k < total; k++) {
                const char *name = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)k);

                if (name != NULL) {
                    Log_Printf("ext: %s\n", name);
                    printed++;
                }
            }
            if (printed == 0) {
                /* Fall back to the legacy string. */
                const char *p = ext;

                while (p != NULL && *p != '\0') {
                    const char *end = strchr(p, ' ');
                    size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);

                    Log_Printf("ext: %.*s\n", (int)len, p);
                    printed++;
                    p = (end != NULL) ? end + 1 : NULL;
                }
            }
            Log_Printf("caps: %d extension(s) listed in total\n", printed);
        }
    }
}
#else
void Gles_LogCapabilities(void)
{
}
#endif

#endif /* MUGEN_GLES */
