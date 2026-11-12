/* Port integration: desktop model shaders are not a VitaGL/GLES backend.
 * Keep the existing 2D ports linkable and report explicit model requests. */
#include "mugen.h"
#ifndef MUGEN_MODEL3D

Stage3D g_stage3d;
int g_modelShadows = 1;

Model *Model_Load(const char *path)
{
    Log_Printf("3D models are unavailable in this build: %s\n", path ? path : "(null)");
    return NULL;
}
Model *Model_LoadAsset(const char *path) { return Model_Load(path); }
void Model_OnContextLost(void) {}
void Model_Free(Model *mdl) { (void)mdl; }
void Model_RendererQuit(void) {}
void Model_Step(Model *mdl) { (void)mdl; }
void Model_Reset(Model *mdl) { (void)mdl; }
void Model_EnableAnim(Model *mdl, int index, int enabled)
{ (void)mdl; (void)index; (void)enabled; }
int Model_LoadEnvironment(Model *mdl, const char *path, float intensity)
{ (void)mdl; (void)path; (void)intensity; return 0; }
void Model_DrawShadows(Model *mdl) { (void)mdl; }
void Model_Draw(Model *mdl, int scene, int layer, int default_layer,
                const float *proj, const float *view, const ModelFx *fx,
                float outline, const float *camera)
{
    (void)mdl; (void)scene; (void)layer; (void)default_layer;
    (void)proj; (void)view; (void)fx; (void)outline; (void)camera;
}

Model *Bg3D_LoadModel(const char *file, const char *dir)
{ (void)dir; return Model_Load(file); }
void Bg3D_SetModel(Model *mdl) { (void)mdl; }
void Bg3D_EndModel(void) {}
void Bg3D_LoadMotifModel(Ini *ini)
{
    const char *file = Ini_GetValue(ini, "model");
    if (file != NULL && *file != '\0') Model_Load(file);
}
Model *Bg3D_MotifModel(void) { return NULL; }
void Bg3D_FreeMotifModel(void) {}
void Bg3D_Attach(const Bg *bg, Ini *ini) { (void)bg; (void)ini; }
void Bg3D_Detach(const Bg *bg) { (void)bg; }
void Bg3D_Tick(const Bg *bg) { (void)bg; }
void Bg3D_Draw(const Bg *bg, int layer) { (void)bg; (void)layer; }

void Stage3D_Load(Stage *stage, Ini *ini, const char *path)
{
    int group = ini->group;
    (void)stage; (void)path;
    if (Ini_SelectGroup(ini, "bgdef") != -1) Bg3D_LoadMotifModel(ini);
    Ini_SelectGroupIndex(ini, group);
}
void Stage3D_Free(void) {}
void Stage3D_Tick(void) {}
void Stage3D_Reset(void) {}
void Stage3D_Draw(int layer) { (void)layer; }
#endif
