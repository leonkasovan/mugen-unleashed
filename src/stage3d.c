/*
 * 3D stages: the [BGdef] model of a stage, placed and projected like Ikemen
 * GO does it (stage.go: the [Model] group, Stage.drawModel).
 *
 * mugen-unleashed addition, not part of the original. A stage gets a model
 * with [BGdef] model = file.glb (relative to the stage, then the usual
 * places). [Model] offset places it in the view (x, y, z, z being the depth
 * of the stage's ground), [Model] scale scales it, [Camera] fov / near / far
 * / yshift set the perspective (40 degrees, 0.1, 10000, 0 by default). As in
 * Ikemen GO, the [Model] offset y moves the stage's zoffset onto the ground
 * of the model, and a stage with a model needs no [BG] element.
 *
 * The camera of Ikemen GO is mapped onto the original's: its position is the
 * one the players are drawn with (Player_UpdateDrawParams), in stage units;
 * its vertical shift (EnvShake, the crop of a stage taller than the screen)
 * is GameVars.camera_y; the zoom is the camera zoom times the Zoom
 * controller's stage scale.
 *
 * Layers: the stage's back layer (Bg_Draw layer 0) draws the nodes of Ikemen
 * GO's layers -1 and 0 of the first scene (nodes without a layerNumber are
 * on 0), each over a cleared depth buffer, under the [BG] elements of the
 * layer; the front layer (1) draws layer 1 of the first scene and all of the
 * second scene. The model needs the OpenGL renderer.
 */
#include "mugen.h"

#include <math.h>

#define S3D_PI 3.14159265358979323846

Stage3D g_stage3d;

/* Reads up to n comma separated floats of key; stops at the first that is no number (as Ikemen GO). */
static void Stage3D_ReadFloats(Ini *ini, const char *key, float *out, int n)
{
    const char *s = Ini_GetValue(ini, key);
    int i;

    if (s == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        char *end;
        double v;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        v = strtod(s, &end);
        if (end == s) {
            return;
        }
        out[i] = (float)v;
        s = end;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s != ',') {
            return;
        }
        s++;
    }
}

/*
 * Reads the 3D part of a stage being loaded (Stage_Load, after [StageInfo]
 * zoffset): the [Camera] perspective, the [BGdef] model and the [Model]
 * placement, which moves st->zoffset onto the model's ground. path is the
 * stage's .def. The selected ini group is kept.
 */
void Stage3D_Load(Stage *st, Ini *ini, const char *path)
{
    Stage3D *s3 = &g_stage3d;
    int group = ini->group;
    const char *v;

    Stage3D_Free();
    s3->offset[0] = s3->offset[1] = s3->offset[2] = 0.0f;
    s3->scale[0] = s3->scale[1] = s3->scale[2] = 1.0f;
    s3->fov = 40.0f;
    s3->near_z = 0.1f;
    s3->far_z = 10000.0f;
    s3->yshift = 0.0f;
    s3->localcoord_h = Ftol(st->height);
    if (Ini_SelectGroup(ini, "Camera") != -1) {
        Ini_GetFloat(ini, "fov", &s3->fov);
        Ini_GetFloat(ini, "yshift", &s3->yshift);
        Ini_GetFloat(ini, "near", &s3->near_z);
        Ini_GetFloat(ini, "far", &s3->far_z);
    }
    if (Ini_SelectGroup(ini, "BGdef") != -1 && (v = Ini_GetValue(ini, "model")) != NULL) {
        char dir[0x200];
        char file[0x200];
        Path_Split(path, dir, NULL);
        if (File_Find(v, file, NULL, dir)) {
            Log_Printf("  Loading model %s...", file);
            s3->model = Model_Load(file);
            Log_Printf(s3->model != NULL ? "OK\n" : "failed\n");
        } else {
            MatchLog_Printf(3, "Can't find model %s\n", v);
        }
    }
    if (s3->model != NULL && Ini_SelectGroup(ini, "Model") != -1) {
        float lc_h = (float)s3->localcoord_h;
        float pos_mul;
        Stage3D_ReadFloats(ini, "offset", s3->offset, 3);
        pos_mul = (float)tan(s3->fov * S3D_PI / 180.0 / 2.0) * -s3->offset[2] / (lc_h / 2.0f);
        st->zoffset = (int)(lc_h / 2.0f - s3->offset[1] / pos_mul - s3->yshift * lc_h / 2.0f);
        Stage3D_ReadFloats(ini, "scale", s3->scale, 3);
    }
    Ini_SelectGroupIndex(ini, group);
}

/* Frees the stage's model (Stage_Unload). */
void Stage3D_Free(void)
{
    Model_Free(g_stage3d.model);
    g_stage3d.model = NULL;
}

/* One tick of the model's animations, when the stage backgrounds tick (Stage_UpdateBgCamera). */
void Stage3D_Tick(void)
{
    Model_Step(g_stage3d.model);
}

/* The model back at its start, with the stage backgrounds (Bg_Reset). */
void Stage3D_Reset(void)
{
    Model_Reset(g_stage3d.model);
}

/* Perspective projection (mathgl's Perspective), fovy in radians. */
static void Stage3D_Perspective(float *m, float fovy, float aspect, float near_z, float far_z)
{
    float f = (float)(1.0 / tan(fovy / 2.0));
    float nmf = near_z - far_z;

    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (near_z + far_z) / nmf;
    m[11] = -1.0f;
    m[14] = 2.0f * far_z * near_z / nmf;
}

/* m = m * (translation x, y, z) */
static void Stage3D_Translate(float *m, float x, float y, float z)
{
    int i;

    for (i = 0; i < 4; i++) {
        m[12 + i] += m[i] * x + m[4 + i] * y + m[8 + i] * z;
    }
}

/* m = m * (scale x, y, z) */
static void Stage3D_Scale(float *m, float x, float y, float z)
{
    int i;

    for (i = 0; i < 4; i++) {
        m[i] *= x;
        m[4 + i] *= y;
        m[8 + i] *= z;
    }
}

/* out = a * b */
static void Stage3D_Mul(float *out, const float *a, const float *b)
{
    float r[16];
    int c, k;

    for (c = 0; c < 4; c++) {
        for (k = 0; k < 4; k++) {
            r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] + a[12 + k] * b[c * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

/* Draws the model's part of stage background layer `layer` (0 back, 1 front), from Bg_Draw. */
void Stage3D_Draw(int layer)
{
    Stage3D *s3 = &g_stage3d;
    GameVars *g = g_game;
    float proj[16], persp[16], view[16];
    float k, gh, lc_h, zoff, scl, cam_x, cam_y, pos_x, pos_y;
    float fov, pos_mul, scale_corr, syo, syo2, aspect_corr, ty;
    ModelFx fx;

    if (s3->model == NULL || g_renderMode != RENDER_OPENGL || g_targetHeight <= 0) {
        return;
    }
    k = (float)(g_gameCoord[0] / g->stage.unit);
    gh = (float)g->game_coord[1];
    lc_h = (float)s3->localcoord_h;
    zoff = (float)g->draw_offset_y / k;
    scl = (float)((double)g->stage_scale * g->camera_zoom);

    /* The camera as the players are drawn with it, in stage units. */
    cam_x = (float)(k * g->camera_x + g->camera_offset_x);
    cam_y = (float)((double)g->camera_offset_y * g->camera_zoom);
    if (g->zoom_active != 0.0f) {
        cam_x = (float)(cam_x + (double)g->shake_x * g->stage_scale / scl);
        cam_y = (float)(cam_y + (double)g->shake_y * g->stage_scale);
    }
    cam_y /= scl;
    pos_x = cam_x / k;
    pos_y = cam_y / k;

    /* Ikemen GO's Stage.drawModel. */
    fov = (float)(s3->fov * S3D_PI / 180.0);
    scale_corr = lc_h * k / gh;
    pos_mul = (float)tan(fov / 2.0) * -s3->offset[2] / (lc_h / 2.0f);
    aspect_corr = (zoff / lc_h - zoff * k / gh) * 2.0f;
    syo = -(zoff - lc_h / 2.0f) * (1.0f - scl) / scl;
    syo2 = -(zoff - lc_h / 2.0f) * (1.0f - scale_corr) / lc_h * 2.0f;
    ty = (float)(k * g->camera_y) / gh * 2.0f + syo2 + aspect_corr;

    memset(proj, 0, sizeof(proj));
    proj[0] = proj[5] = proj[10] = proj[15] = 1.0f;
    Stage3D_Translate(proj, 0.0f, ty, 0.0f);
    Stage3D_Scale(proj, scale_corr, scale_corr, 1.0f);
    Stage3D_Translate(proj, 0.0f, s3->yshift * scl, 0.0f);
    Stage3D_Perspective(persp, fov, (float)g_targetWidth / (float)g_targetHeight, s3->near_z, s3->far_z);
    Stage3D_Mul(proj, proj, persp);

    memset(view, 0, sizeof(view));
    view[0] = view[5] = view[10] = view[15] = 1.0f;
    Stage3D_Translate(view, pos_x * -pos_mul + s3->offset[0] / scl, (pos_y / scl + syo) * pos_mul + s3->offset[1],
                      s3->offset[2] / scl);
    Stage3D_Scale(view, s3->scale[0], s3->scale[1], s3->scale[2]);

    /* The stage PalFX, as the sprite programs take it. */
    if (g_palFxCur != NULL) {
        const PalFx *p = g_palFxCur;
        fx.colorbal = (float)(p->color * (1.0 / 256));
        fx.add[0] = (float)(p->add[0] / 255.0);
        fx.add[1] = (float)(p->add[1] / 255.0);
        fx.add[2] = (float)(p->add[2] / 255.0);
        fx.mul[0] = (float)(p->mul[0] * (1.0 / 256));
        fx.mul[1] = (float)(p->mul[1] * (1.0 / 256));
        fx.mul[2] = (float)(p->mul[2] * (1.0 / 256));
        fx.invertall = p->invertall;
    } else {
        fx.colorbal = 1.0f;
        fx.add[0] = fx.add[1] = fx.add[2] = 0.0f;
        fx.mul[0] = fx.mul[1] = fx.mul[2] = 1.0f;
        fx.invertall = 0;
    }

    if (layer == 0) {
        Model_DrawShadows(s3->model);
        Model_Draw(s3->model, 0, -1, 0, proj, view, &fx);
        Model_Draw(s3->model, 0, 0, 0, proj, view, &fx);
    } else if (layer == 1) {
        Model_Draw(s3->model, 0, 1, 0, proj, view, &fx);
        Model_Draw(s3->model, 1, 1, 1, proj, view, &fx);
    }
}
