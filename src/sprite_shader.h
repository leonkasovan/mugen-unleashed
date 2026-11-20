#ifndef MUGEN_SPRITE_SHADER_H
#define MUGEN_SPRITE_SHADER_H

typedef struct SpriteShaderAsset SpriteShaderAsset;
enum {
    SPRITE_SHADER_SHADOW = 1,
    SPRITE_SHADER_REFLECTION = 2,
    SPRITE_SHADER_AFTERIMAGE = 4,
    SPRITE_SHADER_ALL_PASSES = 7
};

typedef struct SpriteShaderState {
    unsigned asset;
    int time;
    unsigned elapsed;
    float params[16];
    unsigned inherit;
    SpriteSet *textures[2];
    int sprite[2];
} SpriteShaderState;

typedef struct SpriteShaderParams {
    char *name;
    unsigned used;
    int configured;
    Expr params[16];
    Expr texture[2][2];
    unsigned char texture_kind[2];
    Expr duration;
    Expr passes[3];
} SpriteShaderParams;

/* A queue record embeds the original ABI layout as a prefix. */
typedef struct SpriteShaderQueueItem {
    SpriteQueueItem item;
    SpriteShaderState effect;
} SpriteShaderQueueItem;

extern const SpriteShaderState *g_spriteShaderDraw;
int SpriteQueue_AddEffect(const SpriteQueueItem *item, SpriteQueue *q, const SpriteShaderState *effect);

void SpriteShader_Load(void *owner, Ini *def, const char *dir, Stream *archive);
void SpriteShader_FreeAssets(void *owner);
unsigned SpriteShader_FindAsset(const void *owner, const char *name);
const SpriteShaderAsset *SpriteShader_DrawAsset(void);
unsigned SpriteShader_ProgramId(const SpriteShaderAsset *asset);
float SpriteShader_Aspect(void);
void SpriteShader_BindResources(const int textures[2], int grab);
void SpriteShader_FreeTextures(void);
const char *SpriteShader_AssetPath(const SpriteShaderAsset *asset);
SpriteShaderParams *SpriteShader_Parse(Ini *ini, CharData *owner, ExprCompileCtx *ctx, const char *prefix);
void SpriteShader_FreeParams(SpriteShaderParams *params);
void SpriteShader_Apply(const void *object, const void *owner, Player *p,
                        const SpriteShaderParams *params, ExprEvalCtx *frame,
                        int time, unsigned inherit);
void SpriteShader_ApplyObject(const void *object, Player *p, const SpriteShaderParams *params, ExprEvalCtx *frame);
void SpriteShader_ReadStage(const void *object, Bg *bg, Ini *ini, const char *prefix, int baseline);
void SpriteShader_StageOverride(const void *object, const void *controller);
void SpriteShader_StageTick(const void *object, int enabled);
void SpriteShader_StageReset(const void *object);
void SpriteShader_Inherit(const void *object, const SpriteShaderState *state);
const SpriteShaderState *SpriteShader_Pass(const void *object, unsigned pass);
const SpriteShaderState *SpriteShader_Get(const void *object);
void SpriteShader_Clear(const void *object);
void SpriteShader_Tick(const void *object);
void SpriteShader_Draw(SpriteSet *set, int index, const DrawParams *dp, const SpriteShaderState *effect);
char *SpriteShader_Fragment(const char *base, const SpriteShaderAsset *asset, const char *coord);
typedef struct ExplodShaderParams {
    ExplodParams base;
    SpriteShaderParams *effect;
} ExplodShaderParams;
typedef struct ProjectileShaderParams {
    ProjectileParams base;
    SpriteShaderParams *effect;
} ProjectileShaderParams;

void Shadow_DrawEffect(AnimState *st, const DrawParams *dp, float y, float offset, float sx, float sy,
                       const int *shadow, const int *reflection, const void *actor);
int AfterImage_UpdateEffect(const DrawParams *dp, AfterImage *ai, const PalRemap *remap, int active,
                            const SpriteShaderState *effect);
void SpriteShader_ClearAfterImages(AfterImage *ai);
void Anim_DrawEffect(AnimState *st, const DrawParams *dp, float sx, float sy, const SpriteShaderState *effect);

#endif
