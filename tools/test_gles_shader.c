/*
 * ES01 structural gate, part 1 (host, no GPU): the shader dialects the ports
 * ship must contain no desktop-only constructs and must carry the rules of
 * the dialect they target. Full compile+link+readback needs a GL context (the
 * device gates, tracked separately); this catches dialect regressions (e.g.
 * reintroducing sampler1D or gl_TexCoord) at build time.
 *
 * Usage: test_gles_shader <dir> [gles|vita]
 *   gles (default): res/shaders/gles, the GLSL ES 1.00 dialect the Android
 *                   renderer loads from data/gles/.
 *   vita:           res/shaders/vita, the vitaGL dialect. Same four file
 *                   names; it supplies a uMvp uniform instead of u_mvp
 *                   and uses its own attribute/varying
 *                   spelling, and vitaGL does not require a precision
 *                   statement in the fragment shader.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_abi.h"

typedef struct {
    const char *name;
    const char *position;   /* vertex attribute declaration */
    const char *texcoord;   /* vertex attribute declaration */
    const char *varying;    /* fragment-side varying declaration */
    const char *matrix;     /* vertex MVP expression */
    int needs_precision;    /* fragment shader declares precision */
    int forbids_ffp_matrix;
} Dialect;

static const Dialect gles_dialect = {
    "gles",
    "attribute vec4 a_position",
    "attribute vec4 a_texcoord",
    "varying vec4 v_texcoord",
    "uniform mat4 u_mvp",
    1,
    1
};

static const Dialect vita_dialect = {
    .name = "vita",
    .position = "attribute vec4 aPosition",
    .texcoord = "attribute vec4 aTexCoord",
    .varying = "varying vec4 vTexCoord",
    .matrix = "uniform mat4 uMvp",
    .needs_precision = 0,
    .forbids_ffp_matrix = 1
};

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

/* Strip block and line comments: the dialect headers document the desktop
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

/* The loader replaces the *first* marker it finds, so a second occurrence in
 * a header comment would swallow the #define block before the real marker.
 * That is exactly what the shipped dialects did before the files moved here. */
static int count_occurrences(const char *s, const char *needle)
{
    size_t len = strlen(needle);
    int n = 0;

    while ((s = strstr(s, needle)) != NULL) {
        n++;
        s += len;
    }
    return n;
}

static void check_shader(const char *path, int is_vert, const Dialect *d)
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
    check(count_occurrences(raw, "////*EB_DEFINES*////") == 1,
          "EB_DEFINES marker appears exactly once", path);
    s = strip_comments(raw);
    free(raw);
    if (s == NULL) {
        check(0, "out of memory", path);
        return;
    }
    /* Desktop-only constructs that must never appear in an ES-shaped
     * dialect. */
    check(strstr(s, "sampler1D") == NULL, "no sampler1D (no 1D textures)", path);
    check(strstr(s, "gl_TexCoord") == NULL, "no gl_TexCoord built-in", path);
    check(strstr(s, "gl_MultiTexCoord") == NULL, "no gl_MultiTexCoord built-in", path);
    check(strstr(s, "gl_Vertex") == NULL, "no gl_Vertex built-in", path);
    check(strstr(s, "#version 120") == NULL, "no desktop #version 120", path);
    if (d->forbids_ffp_matrix) {
        check(strstr(s, "gl_ModelViewProjectionMatrix") == NULL,
              "no fixed-function MVP (dialect supplies its own)", path);
    }
    check(balanced(s), "balanced braces", path);
    if (is_vert) {
        check(strstr(s, d->position) != NULL, "vertex shader declares a_position", path);
        check(strstr(s, d->texcoord) != NULL, "vertex shader declares a_texcoord", path);
        check(strstr(s, d->matrix) != NULL, "vertex shader takes the dialect MVP", path);
        check(strstr(s, "varying ") != NULL, "vertex shader writes varyings", path);
        check(strstr(s, "gl_Position") != NULL, "vertex shader writes gl_Position", path);
    } else {
        check(strstr(s, d->varying) != NULL, "fragment shader reads the dialect varying", path);
        check(strstr(s, "gl_FragColor") != NULL, "fragment shader writes gl_FragColor", path);
        if (d->needs_precision) {
            check(strstr(s, "precision ") != NULL, "fragment shader declares precision", path);
        }
    }
    free(s);
}

int main(int argc, char **argv)
{
    const char *dir;
    const char *which;
    const Dialect *d;
    const char *files[4];
    char path[512];
    char *s;
    int i;

    TEST_REPORT_ABI();
    dir = argc > 1 ? argv[1] : "res/shaders/gles";
    which = argc > 2 ? argv[2] : "gles";
    if (strcmp(which, "gles") == 0) {
        d = &gles_dialect;
    } else if (strcmp(which, "vita") == 0) {
        d = &vita_dialect;
    } else {
        printf("unknown dialect '%s' (expected gles or vita)\n", which);
        return 2;
    }

    files[0] = "indexed.vert";
    files[1] = "rgba.vert";
    files[2] = "indexed.frag";
    files[3] = "rgba.frag";
    for (i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
        check_shader(path, i < 2, d);
    }

    /* Every dialect samples the palette as a 256x1 2D texture: GLES has no 1D
     * samplers, and vitaGL's shim provides texture2D only. */
    snprintf(path, sizeof(path), "%s/indexed.frag", dir);
    s = read_file(path);
    check(s != NULL, "cannot read indexed.frag", path);
    if (s != NULL) {
        check(strstr(s, "sampler2D texunit1") != NULL, "palette is sampler2D (256x1)", path);
        check(strstr(s, "texture2D(texunit1, vec2(") != NULL,
              "palette sampled at (index, 0.5)", path);
        free(s);
    }

    if (failures != 0) {
        printf("%s-shader: %d FAILURES\n", d->name, failures);
        return 1;
    }
    printf("%s-shader: dialect gates pass\n", d->name);
    return 0;
}
