/*
 * Host gate for the Vita/GLES capability resolver (RENDERING_PLAN.md V2).
 *
 * Exercises port/psvita/src/vita_caps.c on the host: version parsing,
 * exact-token extension matching, the OES/ARB NPOT equivalence, FBO-by-version,
 * and the absence of an ARB_fragment_program requirement. Pure logic -- no GL
 * context, no Vita hardware.
 *
 * Usage: tools/test_vita_caps.sh
 */
#include <stdio.h>
#include <string.h>

#include "vita_caps.h"

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* VitaGL's real strings (from port/psvita/README.md and the probe log). */
static const char *VITA_VERSION = "OpenGL ES 2.0 VitaGL";
static const char *VITA_EXTS =
    "GL_EXT_texture_compression_s3tc GL_OES_texture_npot GL_EXT_framebuffer_object "
    "GL_OES_depth_texture GL_OES_element_index_uint";

static void test_version_parse(void)
{
    int maj = 0, min = 0;

    check(MugenVita_ParseVersion(VITA_VERSION, &maj, &min) && maj == 2 && min == 0,
          "parses 'OpenGL ES 2.0 VitaGL' as 2.0");
    check(MugenVita_ParseVersion("OpenGL ES 3.2", &maj, &min) && maj == 3 && min == 2,
          "parses ES 3.2");
    check(MugenVita_ParseVersion("2.1", &maj, &min) && maj == 2 && min == 1,
          "parses a bare desktop 2.1");
    check(!MugenVita_ParseVersion("no version here", &maj, &min), "rejects a string with no version");
    check(!MugenVita_ParseVersion(NULL, &maj, &min), "rejects NULL");
}

static void test_extension_tokens(void)
{
    check(MugenVita_HasExtension(VITA_EXTS, "GL_OES_texture_npot"), "finds an exact token");
    check(!MugenVita_HasExtension(VITA_EXTS, "GL_OES_texture_npotX"), "rejects a longer token");
    check(!MugenVita_HasExtension(VITA_EXTS, "GL_EXT_framebuffer_object_2"),
          "does not match a _2 variant of an exact token");
    check(MugenVita_HasExtension(VITA_EXTS, "GL_EXT_framebuffer_object"), "finds FBO");
    check(!MugenVita_HasExtension(NULL, "GL_ANYTHING"), "handles a NULL list");
}

static void test_vita_caps(void)
{
    MugenVitaCaps c;

    memset(&c, 0, sizeof(c));
    /* vitaGL: ES 2.0, OES NPOT, EXT FBO, no glBlendColor, no ARB_fragment_program. */
    check(MugenVita_ResolveCaps(VITA_VERSION, VITA_EXTS, 0, &c) == 1,
          "vitaGL strings satisfy the GL mode requirements");
    check(c.gl2 == 1, "vitaGL is reported as 2.0");
    check(c.npot == 1, "GL_OES_texture_npot counts as NPOT");
    check(c.fbo == 1, "FBO present");
    check(c.programmable == 1, "GLSL path available");
    check(c.constant_alpha == 0, "constant alpha absent when the driver says so");
    check(c.blend_logic_op == 0, "blend logic op absent here");
}

static void test_npot_aliases(void)
{
    MugenVitaCaps c;

    /* Desktop-style ARB spelling must work too. */
    check(MugenVita_ResolveCaps("2.1", "GL_ARB_texture_non_power_of_two GL_EXT_framebuffer_object", 1, &c) == 1,
          "ARB NPOT spelling accepted");
    check(c.npot == 1, "ARB NPOT sets the flag");
    check(c.constant_alpha == 1, "constant alpha reported when supported");

    /* FBO by version alone (ES 2.0 core) with no explicit extension. */
    check(MugenVita_ResolveCaps("OpenGL ES 2.0", "GL_OES_texture_npot", 0, &c) == 1,
          "ES 2.0 implies FBO without the EXT token");
    check(c.fbo == 1, "FBO by version");

    /* No NPOT at all: the mode must be refused. */
    check(MugenVita_ResolveCaps("2.0", "GL_EXT_framebuffer_object", 0, &c) == 0,
          "1.x/2.0 without NPOT is refused");

    /* Below 2.0: refused even with extensions. */
    check(MugenVita_ResolveCaps("1.5", "GL_ARB_texture_non_power_of_two GL_EXT_framebuffer_object", 0, &c) == 0,
          "1.5 refused");

    /* A version-less string: refused. */
    check(MugenVita_ResolveCaps("VitaGL", VITA_EXTS, 0, &c) == 0, "no version string is refused");
}

static void test_no_fragment_program_requirement(void)
{
    MugenVitaCaps c;

    /* The whole point of V2: a context with NPOT+FBO but WITHOUT
     * ARB_fragment_program must pass -- the engine's shaders are GLSL. */
    check(MugenVita_ResolveCaps(VITA_VERSION, VITA_EXTS, 0, &c) == 1,
          "ARB_fragment_program is not required");
    /* And adding it changes nothing either way. */
    {
        char with_frag[512];
        snprintf(with_frag, sizeof(with_frag), "%s GL_ARB_fragment_program", VITA_EXTS);
        check(MugenVita_ResolveCaps(VITA_VERSION, with_frag, 0, &c) == 1,
              "presence of ARB_fragment_program does not change the verdict");
    }
}

int main(void)
{
    test_version_parse();
    test_extension_tokens();
    test_vita_caps();
    test_npot_aliases();
    test_no_fragment_program_requirement();

    if (failures != 0) {
        printf("vita-caps: %d FAILURES\n", failures);
        return 1;
    }
    printf("vita-caps: V2 capability gates pass\n");
    return 0;
}
