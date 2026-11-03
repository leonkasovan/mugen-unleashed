/*
 * ES01 structural gate, part 1 (host, no GPU): the GLES shader dialect
 * must contain no desktop-only constructs and must carry the ES 1.00
 * requirements. Full compile+link+readback needs a GLES context (ES01
 * device gate, tracked separately); this catches dialect regressions
 * (e.g. reintroducing sampler1D or gl_TexCoord) at build time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int cond, const char *what, const char *file)
{
    if (!cond) {
        printf("FAIL [%s]: %s\n", file, what);
        failures++;
    }
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int balanced(const char *s)
{
    int depth = 0;
    for (; *s; s++) {
        if (*s == '{') depth++;
        if (*s == '}') { depth--; if (depth < 0) return 0; }
    }
    return depth == 0;
}

/* Strip /* *\/ and // comments: the ES headers document the desktop
 * constructs they replace, so a substring search must not trip on the
 * documentation itself. */
static char *strip_comments(const char *s)
{
    char *out = malloc(strlen(s) + 1);
    char *d = out;
    const char *p = s;

    if (out == NULL) {
        return NULL;
    }
    while (*p) {
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                p++;
            }
            if (*p) {
                p += 2;
            }
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') {
                p++;
            }
        } else {
            *d++ = *p++;
        }
    }
    *d = '\0';
    return out;
}

static void check_shader(const char *path, int is_vert)
{
    char *raw = read_file(path);
    char *s;
    if (raw == NULL) {
        check(0, "cannot read file", path);
        return;
    }
    /* The loader inserts defines at this marker, so it must survive
     * verbatim in the shipped file (checked pre-strip: it lives in a
     * comment by design). */
    check(strstr(raw, "////*EB_DEFINES*////") != NULL, "keeps the EB_DEFINES marker", path);
    s = strip_comments(raw);
    free(raw);
    if (s == NULL) {
        check(0, "out of memory", path);
        return;
    }
    /* Desktop-only constructs that must never appear in the ES dialect. */
    check(strstr(s, "sampler1D") == NULL, "no sampler1D (GLES has no 1D textures)", path);
    check(strstr(s, "gl_TexCoord") == NULL, "no gl_TexCoord built-in", path);
    check(strstr(s, "gl_MultiTexCoord") == NULL, "no gl_MultiTexCoord built-in", path);
    check(strstr(s, "gl_ModelViewProjectionMatrix") == NULL, "no fixed-function MVP", path);
    check(strstr(s, "gl_Vertex") == NULL, "no gl_Vertex built-in", path);
    check(strstr(s, "#version 120") == NULL, "no desktop #version 120", path);
    check(balanced(s), "balanced braces", path);
    if (is_vert) {
        check(strstr(s, "attribute ") != NULL, "vertex shader declares attributes", path);
        check(strstr(s, "uniform mat4 u_mvp") != NULL, "vertex shader takes u_mvp", path);
        check(strstr(s, "varying ") != NULL, "vertex shader writes varyings", path);
    } else {
        check(strstr(s, "precision ") != NULL, "fragment shader declares precision", path);
        check(strstr(s, "varying vec4 v_texcoord") != NULL, "fragment shader reads v_texcoord", path);
        check(strstr(s, "gl_FragColor") != NULL, "fragment shader writes gl_FragColor", path);
    }
    free(s);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "data";
    char path[512];

    snprintf(path, sizeof(path), "%s/indexed_es.vert", dir);
    check_shader(path, 1);
    snprintf(path, sizeof(path), "%s/rgba_es.vert", dir);
    check_shader(path, 1);
    snprintf(path, sizeof(path), "%s/indexed_es.frag", dir);
    check_shader(path, 0);
    snprintf(path, sizeof(path), "%s/rgba_es.frag", dir);
    check_shader(path, 0);

    /* The indexed ES fragment must sample the palette as a 2D texture. */
    {
        char *s;
        snprintf(path, sizeof(path), "%s/indexed_es.frag", dir);
        s = read_file(path);
        check(s != NULL, "cannot read indexed_es.frag", path);
        if (s != NULL) {
            check(strstr(s, "sampler2D texunit1") != NULL, "palette is sampler2D (256x1)", path);
            check(strstr(s, "texture2D(texunit1, vec2(") != NULL, "palette sampled at (index, 0.5)", path);
            free(s);
        }
    }

    if (failures != 0) {
        printf("render-gles-shader: %d FAILURES\n", failures);
        return 1;
    }
    printf("render-gles-shader: ES dialect gates pass\n");
    return 0;
}
