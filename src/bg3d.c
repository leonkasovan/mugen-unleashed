/*
 * 3D models in the backgrounds of the screens (the motif's TitleBG,
 * SelectBG...) and of storyboards, as Ikemen GO's BGDef.
 *
 * mugen-unleashed addition, not part of the original. The motif ([Files]
 * model) or a storyboard ([SceneDef] model) has one glTF model; each
 * background group "[<name>Def]" draws a scene of it with scenenumber
 * (none without), through a fixed perspective camera: fov, near, far,
 * modeloffset, modelrotate (degrees) and modelscale, all 0 by default as in
 * Ikemen GO. The scene is drawn under the elements of each layer of the
 * background (nodes without a layerNumber are on layer 0), and its
 * animations run with the background's ticks. No shadows (Ikemen GO draws
 * them for stages only).
 *
 * The original's Bg has no room for this: a table on the side holds what
 * each background with a model draws.
 */
#include "mugen.h"
#ifdef MUGEN_MODEL3D

#include <math.h>

#define BG3D_PI 3.14159265358979323846

/* A background that draws a model. */
typedef struct Bg3D {
    const Bg *bg;
    Model *model;
    int scene;                  /* [<name>Def] scenenumber */
    float fov;                  /* degrees */
    float near_z;
    float far_z;
    float offset[3];            /* modeloffset */
    float rotate[3];            /* modelrotate, radians */
    float scale[3];             /* modelscale */
} Bg3D;

static Bg3D *g_bg3d;
static int g_bg3dCount;
static int g_bg3dCapacity;

/* The model the backgrounds being loaded take (Bg3D_SetModel). */
static Model *g_bg3dModel;

/* The motif's model ([Files] model), kept until the motif is freed. */
static Model *g_motifModel;

static Bg3D *Bg3D_Find(const Bg *bg)
{
    int i;

    for (i = 0; i < g_bg3dCount; i++) {
        if (g_bg3d[i].bg == bg) {
            return &g_bg3d[i];
        }
    }
    return NULL;
}

static int Bg3D_Uses(const Model *mdl)
{
    int i;

    for (i = 0; i < g_bg3dCount; i++) {
        if (g_bg3d[i].model == mdl) {
            return 1;
        }
    }
    return 0;
}

/* Reads up to n comma separated floats of key; returns how many (Ikemen GO takes them only when all n are there). */
static int Bg3D_ReadFloats(Ini *ini, const char *key, float *out, int n)
{
    const char *s = Ini_GetValue(ini, key);
    int i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char *end;
        double v = strtod(s, &end);
        if (end == s) {
            return i;
        }
        out[i] = (float)v;
        s = end;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s != ',') {
            return i + 1;
        }
        s++;
    }
    return i;
}

/*
 * Loads a model for the backgrounds that follow (the motif's or a
 * storyboard's): file is looked up from dir, then the usual places.
 * Returns NULL (logged) when it cannot be loaded.
 */
Model *Bg3D_LoadModel(const char *file, const char *dir)
{
    char path[0x200];
    Model *mdl;

    if (!File_Find(file, path, NULL, dir)) {
        Log_Printf("Can't find model %s\n", file);
        return NULL;
    }
    Log_Printf("Loading model %s...", path);
    mdl = Model_Load(path);
    Log_Printf(mdl != NULL ? "OK\n" : "failed\n");
    return mdl;
}

/* The model the backgrounds loaded next draw (NULL: none). */
void Bg3D_SetModel(Model *mdl)
{
    g_bg3dModel = mdl;
}

/* The end of a storyboard's loading: its model is freed if no background uses it. */
void Bg3D_EndModel(void)
{
    Model *mdl = g_bg3dModel;

    g_bg3dModel = NULL;
    if (mdl != NULL && mdl != g_motifModel && !Bg3D_Uses(mdl)) {
        Model_Free(mdl);
    }
}

/* Loads the motif's [Files] model (System_LoadMotif); the motif backgrounds take it. */
void Bg3D_LoadMotifModel(Ini *ini)
{
    const char *value = Ini_GetValue(ini, "model");

    Bg3D_FreeMotifModel();
    if (value != NULL && value[0] != '\0') {
        g_motifModel = Bg3D_LoadModel(value, NULL);
    }
}

/* The motif's model, for the motif backgrounds being (re)loaded. */
Model *Bg3D_MotifModel(void)
{
    return g_motifModel;
}

/* Frees the motif's model (System_FreeMotif, after its backgrounds). */
void Bg3D_FreeMotifModel(void)
{
    if (g_motifModel != NULL && !Bg3D_Uses(g_motifModel)) {
        Model_Free(g_motifModel);
    }
    g_motifModel = NULL;
}

/*
 * A background being loaded (Bg_Load, its "[<name>Def]" group selected)
 * draws a scene of the current model when it has a scenenumber.
 */
void Bg3D_Attach(const Bg *bg, Ini *ini)
{
    Bg3D *b;
    float v[3];
    int scene = -1;
    int i;

    if (g_bg3dModel == NULL || !Ini_GetInt(ini, "scenenumber", &scene) || scene < 0) {
        return;
    }
    if (g_bg3dCount == g_bg3dCapacity) {
        int cap = g_bg3dCapacity != 0 ? g_bg3dCapacity * 2 : 8;
        Bg3D *p = realloc(g_bg3d, cap * sizeof(Bg3D));
        if (p == NULL) {
            return;
        }
        g_bg3d = p;
        g_bg3dCapacity = cap;
    }
    b = &g_bg3d[g_bg3dCount++];
    memset(b, 0, sizeof(*b));
    b->bg = bg;
    b->model = g_bg3dModel;
    b->scene = scene;
    Ini_GetFloat(ini, "fov", &b->fov);
    Ini_GetFloat(ini, "near", &b->near_z);
    Ini_GetFloat(ini, "far", &b->far_z);
    if (Bg3D_ReadFloats(ini, "modeloffset", v, 3) == 3) {
        memcpy(b->offset, v, sizeof(v));
    }
    if (Bg3D_ReadFloats(ini, "modelrotate", v, 3) == 3) {
        for (i = 0; i < 3; i++) {
            b->rotate[i] = (float)(v[i] * (BG3D_PI / 180.0));
        }
    }
    if (Bg3D_ReadFloats(ini, "modelscale", v, 3) == 3) {
        memcpy(b->scale, v, sizeof(v));
    }
}

/* A background is freed (Bg_Free): a storyboard's model goes with its last background. */
void Bg3D_Detach(const Bg *bg)
{
    Bg3D *b = Bg3D_Find(bg);
    Model *mdl;

    if (b == NULL) {
        return;
    }
    mdl = b->model;
    *b = g_bg3d[--g_bg3dCount];
    if (mdl != g_motifModel && mdl != g_bg3dModel && !Bg3D_Uses(mdl)) {
        Model_Free(mdl);
    }
}

/* One tick of the model of a background (Bg_Tick). */
void Bg3D_Tick(const Bg *bg)
{
    Bg3D *b = Bg3D_Find(bg);

    if (b != NULL) {
        Model_Step(b->model);
    }
}

/* m = m * rotation about axis 0 (x), 1 (y) or 2 (z) (mathgl's HomogRotate3DX/Y/Z) */
static void Bg3D_Rotate(float *m, int axis, float angle)
{
    float c = (float)cos(angle), s = (float)sin(angle);
    int i = (axis + 1) % 3, j = (axis + 2) % 3;
    int k;

    for (k = 0; k < 4; k++) {
        float a = m[i * 4 + k], b = m[j * 4 + k];
        m[i * 4 + k] = a * c + b * s;
        m[j * 4 + k] = b * c - a * s;
    }
}

/* Draws the model's part of layer `layer` of a background (Bg_Draw), as Ikemen GO's BGDef.draw. */
void Bg3D_Draw(const Bg *bg, int layer)
{
    Bg3D *b = Bg3D_Find(bg);
    float proj[16], view[16], camera[3];
    float fov, f;
    ModelFx fx;
    int i, k;

    if (b == NULL || g_renderMode != RENDER_OPENGL || g_targetHeight <= 0) {
        return;
    }
    fov = (float)(b->fov * BG3D_PI / 180.0);
    f = (float)(1.0 / tan(fov / 2.0));
    memset(proj, 0, sizeof(proj));
    proj[0] = f / ((float)g_targetWidth / (float)g_targetHeight);
    proj[5] = f;
    proj[10] = (b->near_z + b->far_z) / (b->near_z - b->far_z);
    proj[11] = -1.0f;
    proj[14] = 2.0f * b->far_z * b->near_z / (b->near_z - b->far_z);

    memset(view, 0, sizeof(view));
    view[0] = view[5] = view[10] = view[15] = 1.0f;
    view[12] = b->offset[0];
    view[13] = b->offset[1];
    view[14] = b->offset[2];
    for (i = 0; i < 3; i++) {
        Bg3D_Rotate(view, i, b->rotate[i]);
    }
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 4; k++) {
            view[i * 4 + k] *= b->scale[i];
        }
    }

    if (g_palFxCur != NULL) {
        const PalFx *p = g_palFxCur;
        fx.colorbal = (float)(p->color * (1.0 / 256));
        for (i = 0; i < 3; i++) {
            fx.add[i] = (float)(p->add[i] / 255.0);
            fx.mul[i] = (float)(p->mul[i] * (1.0 / 256));
        }
        fx.invertall = p->invertall;
    } else {
        fx.colorbal = 1.0f;
        fx.add[0] = fx.add[1] = fx.add[2] = 0.0f;
        fx.mul[0] = fx.mul[1] = fx.mul[2] = 1.0f;
        fx.invertall = 0;
    }
    /* Ikemen GO puts the eye at -modeloffset for the lighting and outlines */
    camera[0] = -b->offset[0];
    camera[1] = -b->offset[1];
    camera[2] = -b->offset[2];
    Model_Draw(b->model, b->scene, layer, 0, proj, view, &fx, (float)(0.003 * tan(fov)), camera);
}

#endif /* MUGEN_MODEL3D */
