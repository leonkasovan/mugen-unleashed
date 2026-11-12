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
 * of the model, and a stage with a model needs no [BG] element. [BGCtrl3D]
 * groups switch the model's animations and nodes, on Ikemen GO's timing.
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
#ifdef MUGEN_MODEL3D

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

/* Reads up to n comma separated ints of key; returns how many. */
static int Stage3D_ReadInts(Ini *ini, const char *key, int *out, int n)
{
    const char *s = Ini_GetValue(ini, key);
    int i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char *end;
        double v;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        v = strtod(s, &end);
        if (end == s) {
            break;
        }
        out[i] = (int)v;
        s = end;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s != ',') {
            i++;
            break;
        }
        s++;
    }
    return i;
}

/* Whether the first word of group header ("[Name rest]") is name, ignoring case. */
static int Stage3D_IsGroup(const char *header, const char *name)
{
    size_t n = strlen(name);

    return header != NULL && header[0] == '[' && _strnicmp(header + 1, name, n) == 0 &&
           (header[1 + n] == ']' || header[1 + n] == ' ');
}

/*
 * The [BGCtrl3D] groups of the stage (Ikemen GO's loadStage): type Anim or
 * Visible on the nodes and animations whose "id" extra is in ctrlid, at
 * time = start, end, loop, the loop defaulting to the looptime of the last
 * [BGCtrlDef] before.
 */
static void Stage3D_LoadCtrls(Stage3D *s3, Ini *ini)
{
    Model *mdl = s3->model;
    const char *header;
    int count = 0;
    int looptime = -1;

    ini->group = -1;
    while ((header = Ini_NextGroup(ini)) != NULL) {
        if (Stage3D_IsGroup(header, "bgctrl3d")) {
            count++;
        }
    }
    if (count == 0) {
        return;
    }
    s3->ctrls = calloc(count, sizeof(Stage3DCtrl));
    if (s3->ctrls == NULL) {
        return;
    }
    ini->group = -1;
    while ((header = Ini_NextGroup(ini)) != NULL) {
        Stage3DCtrl *c;
        const char *type;
        int ids[64];
        int id_count, i, k;

        if (Stage3D_IsGroup(header, "bgctrldef")) {
            looptime = -1;
            Ini_GetInt(ini, "looptime", &looptime);
            continue;
        }
        if (!Stage3D_IsGroup(header, "bgctrl3d")) {
            continue;
        }
        c = &s3->ctrls[s3->ctrl_count++];
        type = Ini_GetValue(ini, "type");
        if (type != NULL && _stricmp(type, "anim") == 0) {
            c->type = S3D_CTRL_ANIM;
        } else if (type != NULL && _stricmp(type, "visible") == 0) {
            c->type = S3D_CTRL_VISIBLE;
        } else {
            c->type = S3D_CTRL_NULL;
        }
        c->start = 0;
        c->loop = looptime;
        Ini_GetInt(ini, "time", &c->start);
        c->end = c->start;
        {
            int t[3] = { c->start, c->end, c->loop };
            Stage3D_ReadInts(ini, "time", t, 3);
            c->start = t[0];
            c->end = t[1];
            c->loop = t[2];
        }
        Stage3D_ReadInts(ini, "value", &c->value, 1);
        id_count = Stage3D_ReadInts(ini, "ctrlid", ids, 64);
        if (id_count == 0 || (id_count == 1 && ids[0] == -1)) {
            continue;
        }
        c->nodes = malloc((mdl->node_count + 1) * sizeof(int));
        c->anims = malloc((mdl->anim_count + 1) * sizeof(int));
        if (c->nodes == NULL || c->anims == NULL) {
            continue;
        }
        for (k = 0; k < id_count; k++) {
            int seen = 0;
            for (i = 0; i < k; i++) {
                seen |= ids[i] == ids[k];
            }
            if (seen) {
                continue;
            }
            for (i = 0; i < mdl->node_count; i++) {
                if (mdl->nodes[i].id == ids[k]) {
                    c->nodes[c->node_count++] = i;
                }
            }
            for (i = 0; i < mdl->anim_count; i++) {
                if (mdl->anims[i].id == ids[k]) {
                    c->anims[c->anim_count++] = i;
                }
            }
        }
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
            int64_t started = Timer_GetTicks();
            /*
             * Port addition: the model load now reports its own result line,
             * because the asset summary (counts, texture cost) is printed
             * between the two and the old "Loading model ...OK" pairing was
             * broken by it. A failed model is not a failed stage -- the 2D
             * background still loads -- and "failed" alone left a black screen
             * unexplained.
             */
            Log_Printf("  Loading model %s\n", file);
            s3->model = Model_Load(file);
            if (s3->model != NULL) {
                Log_Printf("  3D model loaded in %d ms\n", (int)(Timer_GetTicks() - started));
            } else {
                Log_Printf("  3D model rejected after %d ms; the stage falls back to its 2D "
                           "background, or draws nothing if it has none\n",
                           (int)(Timer_GetTicks() - started));
            }
        } else {
            Log_Printf("Can't find model %s\n", v);
        }
    }
    if (s3->model != NULL && Ini_SelectGroup(ini, "Model") != -1) {
        float lc_h = (float)s3->localcoord_h;
        float pos_mul;
        Stage3D_ReadFloats(ini, "offset", s3->offset, 3);
        pos_mul = (float)tan(s3->fov * S3D_PI / 180.0 / 2.0) * -s3->offset[2] / (lc_h / 2.0f);
        st->zoffset = (int)(lc_h / 2.0f - s3->offset[1] / pos_mul - s3->yshift * lc_h / 2.0f);
        Stage3D_ReadFloats(ini, "scale", s3->scale, 3);
        v = Ini_GetValue(ini, "environment");
        if (v != NULL) {
            char dir[0x200];
            char file[0x200];
            float intensity = 1.0f;
            Ini_GetFloat(ini, "environmentintensity", &intensity);
            Path_Split(path, dir, NULL);
            if (File_Find(v, file, NULL, dir)) {
                Log_Printf("  Loading environment %s...", file);
                Log_Printf(Model_LoadEnvironment(s3->model, file, intensity) ? "OK\n" : "failed\n");
            } else {
                Log_Printf("Can't find environment %s\n", v);
            }
        }
    }
    if (s3->model != NULL) {
        Stage3D_LoadCtrls(s3, ini);
    }
    Ini_SelectGroupIndex(ini, group);
}

/* Frees the stage's model and controllers (Stage_Unload). */
void Stage3D_Free(void)
{
    Stage3D *s3 = &g_stage3d;
    int i;

    for (i = 0; i < s3->ctrl_count; i++) {
        free(s3->ctrls[i].nodes);
        free(s3->ctrls[i].anims);
    }
    free(s3->ctrls);
    s3->ctrls = NULL;
    s3->ctrl_count = 0;
    s3->time = 0;
    Model_Free(s3->model);
    s3->model = NULL;
}

/*
 * Runs the [BGCtrl3D] active at the stage time (Ikemen GO's bgCtrlAction):
 * from start to end, or with a loop, from start for (end - start) ticks of
 * every loop.
 */
static void Stage3D_RunCtrls(Stage3D *s3)
{
    int i, k;

    for (i = 0; i < s3->ctrl_count; i++) {
        Stage3DCtrl *c = &s3->ctrls[i];
        int active = 0;

        if (c->start < 0 || (c->loop >= 0 && c->start >= c->loop)) {
            continue;
        }
        if (c->loop > 0 && c->end > c->loop) {
            c->end = c->loop;
        }
        if (s3->time >= c->start) {
            if (c->loop > 0) {
                active = (s3->time - c->start) % c->loop <= c->end - c->start;
            } else {
                active = s3->time <= c->end;
            }
        }
        if (!active) {
            continue;
        }
        switch (c->type) {
        case S3D_CTRL_ANIM:
            for (k = 0; k < c->anim_count; k++) {
                Model_EnableAnim(s3->model, c->anims[k], c->value != 0);
            }
            break;
        case S3D_CTRL_VISIBLE:
            for (k = 0; k < c->node_count; k++) {
                s3->model->nodes[c->nodes[k]].visible = c->value != 0;
            }
            break;
        }
    }
}

/*
 * One tick of the model, when the stage backgrounds tick (Stage_UpdateBgCamera):
 * the controllers, the animations, then the stage time (Ikemen GO's Stage.action
 * and Stage.tick).
 */
void Stage3D_Tick(void)
{
    Stage3D *s3 = &g_stage3d;

    if (s3->model == NULL) {
        return;
    }
    Stage3D_RunCtrls(s3);
    Model_Step(s3->model);
    s3->time++;
}

/* The model back at its start, with the stage backgrounds (Bg_Reset). */
void Stage3D_Reset(void)
{
    g_stage3d.time = 0;
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
    float fov, pos_mul, scale_corr, syo, syo2, aspect_corr, ty, outline;
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

    /* mesh outlines are as wide as Ikemen GO's: meshOutline * 0.003 * tan(fov) */
    outline = (float)(0.003 * tan(fov));
    if (layer == 0) {
        Model_DrawShadows(s3->model);
        Model_Draw(s3->model, 0, -1, 0, proj, view, &fx, outline, NULL);
        Model_Draw(s3->model, 0, 0, 0, proj, view, &fx, outline, NULL);
    } else if (layer == 1) {
        Model_Draw(s3->model, 0, 1, 0, proj, view, &fx, outline, NULL);
        Model_Draw(s3->model, 1, 1, 1, proj, view, &fx, outline, NULL);
    }
}

#endif /* MUGEN_MODEL3D */
