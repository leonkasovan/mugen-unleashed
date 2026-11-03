/*
 * PS Vita / GLES capability resolution -- implementation. See vita_caps.h for
 * why this is a separate, GL-free unit (host-testable via
 * tools/test_vita_caps.sh).
 */
#include <string.h>

#include "vita_caps.h"

/* Exact-token search in a space-separated extension list. */
int MugenVita_HasExtension(const char *list, const char *name)
{
    size_t name_len;
    const char *p;

    if (list == NULL || name == NULL) {
        return 0;
    }
    name_len = strlen(name);
    p = list;
    while ((p = strstr(p, name)) != NULL) {
        const char *end = p + name_len;

        if ((p == list || p[-1] == ' ') && (*end == ' ' || *end == '\0')) {
            return 1;
        }
        p = end;
    }
    return 0;
}

/* First "<digits>.<digits>" pair anywhere in the string. */
int MugenVita_ParseVersion(const char *version, int *major, int *minor)
{
    const char *p;

    if (major != NULL) {
        *major = 0;
    }
    if (minor != NULL) {
        *minor = 0;
    }
    if (version == NULL) {
        return 0;
    }
    p = version;
    while (*p != '\0') {
        if (*p >= '0' && *p <= '9') {
            int maj = 0;
            int min = 0;
            const char *q = p;

            while (*q >= '0' && *q <= '9') {
                maj = maj * 10 + (*q - '0');
                q++;
            }
            if (*q == '.') {
                q++;
                if (*q >= '0' && *q <= '9') {
                    while (*q >= '0' && *q <= '9') {
                        min = min * 10 + (*q - '0');
                        q++;
                    }
                    if (major != NULL) {
                        *major = maj;
                    }
                    if (minor != NULL) {
                        *minor = min;
                    }
                    return 1;
                }
            }
            p = q;
            continue;
        }
        p++;
    }
    return 0;
}

int MugenVita_ResolveCaps(const char *version, const char *extensions, int constant_alpha_supported,
                          MugenVitaCaps *caps)
{
    MugenVitaCaps out;
    int found;

    memset(&out, 0, sizeof(out));
    found = MugenVita_ParseVersion(version, &out.major, &out.minor);
    if (!found) {
        if (caps != NULL) {
            *caps = out;
        }
        return 0;
    }
    /* ES 2.0 and desktop 2.0 both satisfy the engine's 2.0 requirement. */
    out.gl2 = (out.major > 2 || (out.major == 2 && out.minor >= 0)) ? 1 : 0;

    /* NPOT: either spelling is the same capability (V2). */
    out.npot = MugenVita_HasExtension(extensions, "GL_ARB_texture_non_power_of_two") ||
               MugenVita_HasExtension(extensions, "GL_OES_texture_npot");

    /* FBO is core in ES 2.0; the explicit extension covers drivers that only
     * advertise EXT for a desktop context. */
    out.fbo = MugenVita_HasExtension(extensions, "GL_EXT_framebuffer_object") ||
              MugenVita_HasExtension(extensions, "GL_OES_framebuffer_object") || out.gl2;

    out.blend_logic_op = MugenVita_HasExtension(extensions, "GL_EXT_blend_logic_op");

    /* GLSL is the only programmable path here; ARB_fragment_program is not
     * required (and does not exist in GLES). Actual shader compile/link is
     * validated by the loader, not by a flag. */
    out.programmable = out.gl2;

    out.constant_alpha = constant_alpha_supported ? 1 : 0;

    if (caps != NULL) {
        *caps = out;
    }
    return out.gl2 && out.npot && out.fbo;
}
