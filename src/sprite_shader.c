#include "mugen.h"
#include "sprite_shader.h"
#include "shader_src.h"

/* Assets are loaded by character precache too. The active character or stage
 * owns the asset for the lifetime of each simulation/draw reference. */
struct SpriteShaderAsset {
    struct SpriteShaderAsset *next;
    const void *owner;
    unsigned id;
    unsigned refs;
    unsigned program_id;
    char *path;
    char *name;
    char *source;
};
static SpriteShaderAsset *assets;
static unsigned last_id;
static SDL_SpinLock asset_lock;

typedef struct ShaderBinding {
    struct ShaderBinding *next;
    const void *object;
    SpriteShaderState state;
    SpriteShaderState baseline;
    AnimState animation[2];
    unsigned char animated[2];
    const struct ShaderBinding *stage_rule;
    const struct ShaderBinding *stage_pending;
    Anim *stage_anim;
    unsigned stage_animation_mask;
    int stage_action[2];
} ShaderBinding;

#define SHADER_BUCKETS 64
static ShaderBinding *bindings[SHADER_BUCKETS];
const SpriteShaderState *g_spriteShaderDraw;
static const SpriteShaderAsset *draw_asset;

static char *CopyTrimmed(const char *text, size_t length)
{
    while (length && isspace((unsigned char)*text)) {
        ++text;
        --length;
    }
    while (length && isspace((unsigned char)text[length - 1]))
        --length;
    if (length >= 2 && text[0] == '"' && text[length - 1] == '"') {
        ++text;
        length -= 2;
    }
    char *out = malloc(length + 1);
    if (out != NULL) {
        memcpy(out, text, length);
        out[length] = 0;
    }
    return out;
}

static void FreeAsset(SpriteShaderAsset *asset)
{
    free(asset->name);
    free(asset->path);
    free(asset->source);
    free(asset);
}

void SpriteShader_Load(void *owner, Ini *def, const char *dir, Stream *archive)
{
    const char *line;
    if (Ini_SelectGroup(def, "Shaders") == -1)
        return;
    while ((line = Ini_NextLine(def)) != NULL) {
        const char *equals = strchr(line, '=');
        if (equals == NULL)
            continue;
        SpriteShaderAsset *asset = calloc(1, sizeof(*asset));
        if (asset == NULL)
            return;
        asset->name = CopyTrimmed(line, (size_t)(equals - line));
        asset->path = CopyTrimmed(equals + 1, strlen(equals + 1));
        if (!asset->name || !asset->name[0] || !asset->path || !asset->path[0]) {
            FreeAsset(asset);
            continue;
        }
        Stream *source;
        if (archive) {
            Stream_ZipUnload(archive);
            source = Stream_ZipLoad(archive, asset->path) ? archive : NULL;
        } else {
            char path[MAX_PATH];
            if (strlen(dir) + strlen(asset->path) + 2 > sizeof(path)) {
                FreeAsset(asset);
                continue;
            }
            Path_Join(asset->path, dir, path);
            source = Stream_OpenFile(path, "rb");
        }
        if (source) {
            enum { MAX_SHADER_SOURCE = 256 * 1024 };
            if (Stream_Seek(source, 0, SEEK_END) == 0) {
                long size = Stream_Tell(source);
                if (size > 0 && size <= MAX_SHADER_SOURCE && Stream_Seek(source, 0, SEEK_SET) == 0) {
                    asset->source = malloc((size_t)size + 1);
                    if (asset->source && Stream_Read(asset->source, 1, (size_t)size, source) == (size_t)size) {
                        asset->source[size] = 0;
                    } else {
                        free(asset->source);
                        asset->source = NULL;
                    }
                }
            }
            if (!archive)
                Stream_Close(source);
        }
        if (!asset->source)
            Log_Printf("ShaderSet: could not read %s\n", asset->path);
        asset->owner = owner;
        SDL_AtomicLock(&asset_lock);
        asset->id = ++last_id;
        asset->refs = 1;
        asset->program_id = asset->id;
        for (SpriteShaderAsset *other = assets; asset->source && other; other = other->next) {
            if (other->source && strcmp(asset->source, other->source) == 0) {
                asset->program_id = other->program_id;
                break;
            }
        }
        asset->next = assets;
        assets = asset;
        SDL_AtomicUnlock(&asset_lock);
    }
}

void SpriteShader_FreeAssets(void *owner)
{
    SDL_AtomicLock(&asset_lock);
    SpriteShaderAsset **entry = &assets;
    while (*entry) {
        SpriteShaderAsset *asset = *entry;
        if (asset->owner == owner) {
            *entry = asset->next;
            if (--asset->refs == 0) FreeAsset(asset);
        } else {
            entry = &asset->next;
        }
    }
    SDL_AtomicUnlock(&asset_lock);
}

unsigned SpriteShader_FindAsset(const void *owner, const char *name)
{
    const SpriteShaderAsset *asset;
    SDL_AtomicLock(&asset_lock);
    for (asset = assets; asset; asset = asset->next) {
        if (asset->owner == owner && _stricmp(name, asset->name) == 0)
            break;
    }
    unsigned id = asset ? asset->id : 0;
    SDL_AtomicUnlock(&asset_lock);
    return id;
}

const SpriteShaderAsset *SpriteShader_DrawAsset(void)
{
    return draw_asset;
}

unsigned SpriteShader_ProgramId(const SpriteShaderAsset *asset)
{
    return asset->program_id;
}

float SpriteShader_Aspect(void)
{
    if (g_targetHeight <= 0 || g_gameCoord[0] <= 0 || g_gameCoord[1] <= 0)
        return 1.0f;
    return (float)((g_targetWidth * g_gameCoord[1]) / (g_targetHeight * g_gameCoord[0]));
}

const char *SpriteShader_AssetPath(const SpriteShaderAsset *asset)
{
    return asset->path;
}

static const char *Field(Ini *ini, const char *prefix, const char *name)
{
    char field[64];
    snprintf(field, sizeof(field), "%s%s", prefix, name);
    return Ini_GetValue(ini, field);
}

SpriteShaderParams *SpriteShader_Parse(Ini *ini, CharData *owner, ExprCompileCtx *ctx, const char *prefix)
{
    SpriteShaderParams *params = calloc(1, sizeof(*params));
    if (!params)
        return NULL;
    const char *name = Field(ini, prefix, "name");
    if (!name)
        name = Ini_GetValue(ini, "shader");
    if (name) {
        params->name = CopyTrimmed(name, strlen(name));
        if (!params->name)
            goto fail;
    }
    for (int i = 0; i < 16; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "param%d", i);
        const char *value = Field(ini, prefix, key);
        if (!value) {
            snprintf(key, sizeof(key), "shaderparam.p%d", i);
            value = Ini_GetValue(ini, key);
        }
        if (value) {
            const char *end = NULL;
            if (!Expr_CompileCtx(value, &params->params[i], owner, &end, EXPR_WANT_FLOAT, ctx) ||
                end == NULL || *end)
                goto fail;
            params->used |= 1u << i;
        }
    }
    if (prefix[0]) {
        const char *time = Field(ini, prefix, "time");
        if (!time) time = Ini_GetValue(ini, "shadertime");
        params->configured |= time != NULL;
        Expr_SetInt(&params->duration, INT_MIN);
        if (!Expr_CompileCtx(time, &params->duration, owner, NULL, EXPR_WANT_INT, ctx))
            goto fail;
        static const char *const passes[] = {"shadow", "reflection", "afterimage"};
        for (int i = 0; i < 3; ++i) {
            Expr_SetInt(&params->passes[i], -1);
            const char *value = Field(ini, prefix, passes[i]);
            params->configured |= value != NULL;
            if (!Expr_CompileCtx(value, &params->passes[i], owner, NULL, EXPR_WANT_INT, ctx))
                goto fail;
        }
    }
    for (int i = 0; i < 2; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "tex%d.anim", i + 1);
        const char *value = Field(ini, prefix, key);
        if (!value) {
            snprintf(key, sizeof(key), "shadertex%d.anim", i + 1);
            value = Ini_GetValue(ini, key);
        }
        if (value) {
            if (!Expr_CompileCtx(value, &params->texture[i][0], owner, NULL, EXPR_WANT_INT, ctx))
                goto fail;
            params->texture_kind[i] = 2;
            continue;
        }
        snprintf(key, sizeof(key), "tex%d.spr", i + 1);
        value = Field(ini, prefix, key);
        if (!value) {
            snprintf(key, sizeof(key), "shadertex%d.spr", i + 1);
            value = Ini_GetValue(ini, key);
        }
        if (value) {
            const char *end = value;
            if (!Cns_ParseValuesCtx(value, "ii", owner, &end, ctx, &params->texture[i][0],
                                    &params->texture[i][1]) || *end)
                goto fail;
            params->texture_kind[i] = 1;
        }
    }
    params->configured |= params->name || params->used || params->texture_kind[0] || params->texture_kind[1];
    return params;
fail:
    SpriteShader_FreeParams(params);
    return NULL;
}

void SpriteShader_FreeParams(SpriteShaderParams *params)
{
    if (params) {
        for (int i = 0; i < 16; ++i)
            Expr_Free(&params->params[i]);
        for (int i = 0; i < 2; ++i) {
            Expr_Free(&params->texture[i][0]);
            Expr_Free(&params->texture[i][1]);
        }
        Expr_Free(&params->duration);
        for (int i = 0; i < 3; ++i)
            Expr_Free(&params->passes[i]);
        free(params->name);
        free(params);
    }
}

static ShaderBinding **Binding(const void *object)
{
    ShaderBinding **entry = &bindings[((uintptr_t)object >> 4) % SHADER_BUCKETS];
    while (*entry && (*entry)->object != object)
        entry = &(*entry)->next;
    return entry;
}

const SpriteShaderState *SpriteShader_Get(const void *object)
{
    ShaderBinding *binding = *Binding(object);
    return binding && binding->state.asset ? &binding->state : NULL;
}

void SpriteShader_Clear(const void *object)
{
    ShaderBinding **entry = Binding(object);
    ShaderBinding *binding = *entry;
    if (binding) {
        *entry = binding->next;
        free(binding);
    }
}

void SpriteShader_Tick(const void *object)
{
    ShaderBinding *binding = *Binding(object);
    if (binding) {
        if (binding->state.time > 0 && --binding->state.time == 0)
            SpriteShader_Clear(object);
        else {
            ++binding->state.elapsed;
            for (int i = 0; i < 2; ++i) {
                if (!binding->animated[i]) continue;
                Anim_Step(&binding->animation[i]);
                binding->state.sprite[i] = binding->animation[i].sprite;
            }
        }
    }
}

static void StartTextureAnimation(ShaderBinding *binding, int slot, Anim *asset,
                                  SpriteSet *sprites, int action)
{
    AnimState *anim = &binding->animation[slot];
    memset(anim, 0, sizeof(*anim));
    anim->anim = asset;
    anim->sprites = sprites;
    anim->action = -1;
    binding->animated[slot] = 0;
    binding->state.sprite[slot] = -1;
    if (asset && asset->actions && asset->action_count > 0 && sprites && sprites->sprites &&
        Anim_SetAction(anim, action)) {
        Anim_Reset(anim);
        Anim_Step(anim);
        binding->animated[slot] = 1;
        binding->state.sprite[slot] = anim->sprite;
        binding->state.textures[slot] = sprites;
    }
}

void SpriteShader_Apply(const void *object, const void *owner, Player *p,
                        const SpriteShaderParams *params, ExprEvalCtx *frame,
                        int time, unsigned inherit)
{
    if (time == 0 || (params->name && !params->name[0])) {
        SpriteShader_Clear(object);
        return;
    }
    ShaderBinding **entry = Binding(object);
    ShaderBinding *binding = *entry;
    unsigned asset = binding ? binding->state.asset : 0;
    if (params->name) {
        asset = SpriteShader_FindAsset(owner, params->name);
        if (!asset) {
            Log_Printf("ShaderSet: unknown shader %s\n", params->name);
            return;
        }
    }
    if (!asset)
        return;
    if (!binding) {
        binding = calloc(1, sizeof(*binding));
        if (!binding)
            return;
        binding->object = object;
        *entry = binding;
    }
    if (binding->state.asset != asset) {
        memset(&binding->state, 0, sizeof(binding->state));
        memset(binding->animated, 0, sizeof(binding->animated));
    }
    binding->state.asset = asset;
    binding->state.time = time;
    binding->state.inherit = inherit;
    for (int i = 0; i < 16; ++i)
        if (params->used & (1u << i))
            binding->state.params[i] = Expr_EvalFloatCtx(p, &params->params[i], frame);
    for (int i = 0; i < 2; ++i) {
        if (!params->texture_kind[i]) continue;
        int first = Expr_EvalIntCtx(p, &params->texture[i][0], 0, frame);
        binding->animated[i] = 0;
        binding->state.textures[i] = NULL;
        if (!p->sprites || !p->sprites->sprites) continue;
        if (params->texture_kind[i] == 1) {
            int second = Expr_EvalIntCtx(p, &params->texture[i][1], 0, frame);
            binding->state.sprite[i] = KeyedArray_Find(p->sprites->sprites, first, second);
        } else {
            StartTextureAnimation(binding, i, p->data->anim, p->sprites, first);
            continue;
        }
        binding->state.textures[i] = p->sprites;
    }
}

void SpriteShader_ApplyObject(const void *object, Player *p, const SpriteShaderParams *params, ExprEvalCtx *frame)
{
    if (!params || !params->configured)
        return;
    const SpriteShaderState *current = SpriteShader_Get(object);
    int time = Expr_EvalIntCtx(p, &params->duration, 0, frame);
    unsigned passes = current ? current->inherit : SPRITE_SHADER_ALL_PASSES;
    for (int i = 0; i < 3; ++i) {
        int flag = Expr_EvalIntCtx(p, &params->passes[i], 0, frame);
        if (flag >= 0)
            passes = (passes & ~(1u << i)) | ((unsigned)(flag != 0) << i);
    }
    SpriteShader_Apply(object, p->data, p, params, frame,
                       time != INT_MIN ? time : current ? current->time : -1, passes);
}

static ShaderBinding *Store(const void *object, const SpriteShaderState *state)
{
    ShaderBinding **entry = Binding(object);
    if (!*entry) {
        *entry = calloc(1, sizeof(**entry));
        if (!*entry) return NULL;
        (*entry)->object = object;
    }
    (*entry)->state = *state;
    return *entry;
}

void SpriteShader_Inherit(const void *object, const SpriteShaderState *state)
{
    if (state)
        Store(object, state);
    else
        SpriteShader_Clear(object);
}

const SpriteShaderState *SpriteShader_Pass(const void *object, unsigned pass)
{
    const SpriteShaderState *state = SpriteShader_Get(object);
    return state && (state->inherit & pass) ? state : NULL;
}

void SpriteShader_ReadStage(const void *object, Bg *bg, Ini *ini, const char *prefix, int baseline)
{
    const char *name = Field(ini, prefix, "name");
    if (!name && baseline) return;
    SpriteShaderState state = {0};
    if (name) {
        char *alias = CopyTrimmed(name, strlen(name));
        if (!alias) return;
        state.asset = SpriteShader_FindAsset(bg, alias);
        if (alias[0] && !state.asset)
            Log_Printf("ShaderSet: unknown background shader %s\n", alias);
        free(alias);
    }
    state.time = -1;
    for (int i = 0; i < 16; ++i) {
        char key[24];
        snprintf(key, sizeof(key), "%sparam%d", prefix, i);
        Ini_GetFloat(ini, key, &state.params[i]);
    }
    for (int i = 0; i < 2 && bg->sprites && bg->sprites->sprites; ++i) {
        char key[32];
        int group, number;
        snprintf(key, sizeof(key), "%stex%d.spr", prefix, i + 1);
        if (Ini_Scanf(ini, key, "%i,%i", &group, &number) == 2) {
            state.textures[i] = bg->sprites;
            state.sprite[i] = KeyedArray_Find(bg->sprites->sprites, group, number);
        }
    }
    ShaderBinding *binding = Store(object, &state);
    if (!binding) return;
    binding->stage_anim = bg->anim;
    for (int i = 0; i < 2; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "%stex%d.anim", prefix, i + 1);
        if (bg->sprites && Ini_GetInt(ini, key, &binding->stage_action[i])) {
            binding->stage_animation_mask |= 1u << i;
            binding->state.textures[i] = bg->sprites;
            StartTextureAnimation(binding, i, bg->anim, bg->sprites, binding->stage_action[i]);
        }
    }
    if (baseline)
        binding->baseline = binding->state;
}

void SpriteShader_StageOverride(const void *object, const void *controller)
{
    const ShaderBinding *rule = *Binding(controller);
    if (!rule)
        return;
    ShaderBinding *binding = *Binding(object);
    if (!binding) {
        const SpriteShaderState empty = {0};
        binding = Store(object, &empty);
    }
    if (binding)
        binding->stage_pending = rule;
}

void SpriteShader_StageReset(const void *object)
{
    ShaderBinding *binding = *Binding(object);
    if (binding) {
        if (binding->baseline.asset) {
            binding->state = binding->baseline;
            binding->stage_rule = NULL;
            binding->stage_pending = NULL;
            memset(binding->animated, 0, sizeof(binding->animated));
            for (int i = 0; i < 2; ++i)
                if (binding->stage_animation_mask & (1u << i))
                    StartTextureAnimation(binding, i, binding->stage_anim, binding->baseline.textures[i], binding->stage_action[i]);
        } else {
            SpriteShader_Clear(object);
        }
    }
}

void SpriteShader_StageTick(const void *object, int enabled)
{
    ShaderBinding *binding = *Binding(object);
    /* Only the last matching BGCtrl changes the effect. Earlier matches
     * cannot restart its elapsed time or auxiliary animation each tick. */
    if (binding && binding->stage_pending) {
        const ShaderBinding *rule = binding->stage_pending;
        binding->stage_pending = NULL;
        if (binding->stage_rule != rule) {
            unsigned elapsed = binding->state.asset == rule->state.asset ? binding->state.elapsed : 0;
            binding->state = rule->state;
            binding->state.elapsed = elapsed;
            binding->stage_rule = rule;
            memset(binding->animated, 0, sizeof(binding->animated));
            for (int i = 0; i < 2; ++i)
                if (rule->stage_animation_mask & (1u << i))
                    StartTextureAnimation(binding, i, rule->stage_anim, rule->state.textures[i], rule->stage_action[i]);
        }
    } else if (binding && binding->stage_rule) {
        SpriteShader_StageReset(object);
    }
    if (enabled) SpriteShader_Tick(object);
}

void SpriteShader_Draw(SpriteSet *set, int index, const DrawParams *dp, const SpriteShaderState *effect)
{
    if (!effect && !g_spriteShaderDraw) {
        Sprite_Draw(set, index, dp);
        return;
    }
    const SpriteShaderState *saved = g_spriteShaderDraw;
    const SpriteShaderAsset *saved_asset = draw_asset;
    SpriteShaderAsset *asset = NULL;
    if (effect) {
        SDL_AtomicLock(&asset_lock);
        for (asset = assets; asset && asset->id != effect->asset; asset = asset->next) {}
        if (asset) ++asset->refs;
        SDL_AtomicUnlock(&asset_lock);
    }
    g_spriteShaderDraw = asset ? effect : NULL;
    draw_asset = asset;
    Sprite_Draw(set, index, dp);
    g_spriteShaderDraw = saved;
    draw_asset = saved_asset;
    if (asset) {
        SDL_AtomicLock(&asset_lock);
        if (--asset->refs == 0) FreeAsset(asset);
        SDL_AtomicUnlock(&asset_lock);
    }
}

/* Replace the stock fragment's entry point and color/coordinate input with
 * private variables. The effect calls that same indexed or RGBA variant,
 * including its exact masking, perspective and PalFX calculations. */
static char *Replace(const char *src, const char *from, const char *to)
{
    size_t fromlen = strlen(from), tolen = strlen(to), size = strlen(src);
    const char *hit;
    for (const char *p = src; (hit = strstr(p, from)) != NULL; p = hit + fromlen)
        size = size - fromlen + tolen;
    char *result = malloc(size + 1);
    if (!result)
        return NULL;
    char *out = result;
    while ((hit = strstr(src, from)) != NULL) {
        size_t prefix = (size_t)(hit - src);
        memcpy(out, src, prefix);
        memcpy(out + prefix, to, tolen);
        out += prefix + tolen;
        src = hit + fromlen;
    }
    strcpy(out, src);
    return result;
}

/* The Vita wrapper binds four vectors. Its shader compiler can optimize
 * unused array elements, so a bulk 16-element upload is unsafe there. */
static char *VitaParameterAccess(const char *source)
{
    const char *start = source;
    char *buffer = malloc(strlen(source) * 3 + 1);
    if (!buffer) return NULL;
    char *out = buffer;
    while (*source) {
        const char *comment_end = NULL;
        if (strncmp(source, "//", 2) == 0) {
            comment_end = strchr(source, '\n');
            if (!comment_end) comment_end = source + strlen(source);
        } else if (strncmp(source, "/*", 2) == 0) {
            comment_end = strstr(source + 2, "*/");
            comment_end = comment_end ? comment_end + 2 : source + strlen(source);
        }
        if (comment_end) {
            size_t length = (size_t)(comment_end - source);
            memcpy(out, source, length);
            out += length;
            source = comment_end;
            continue;
        }
        if ((source == start || (!isalnum((unsigned char)source[-1]) && source[-1] != '_')) &&
            strncmp(source, "param", 5) == 0) {
            const char *index = source + 5;
            while (isspace((unsigned char)*index)) ++index;
            if (*index == '[') {
                ++index;
                while (isspace((unsigned char)*index)) ++index;
                char *end;
                long slot = strtol(index, &end, 10);
                if (index == end || slot < 0 || slot >= 16) goto fail;
                while (isspace((unsigned char)*end)) ++end;
                if (*end != ']') goto fail;
                out += sprintf(out, "mugenParams%ld.%c", slot / 4, "xyzw"[slot % 4]);
                source = end + 1;
                continue;
            }
        }
        *out++ = *source++;
    }
    *out = 0;
    return buffer;
fail:
    free(buffer);
    return NULL;
}

char *SpriteShader_Fragment(const char *base, const SpriteShaderAsset *asset, const char *coord)
{
    if (!asset->source)
        return NULL;
    const char *include = "#include \"sprite_lib.frag.glsl\"";
    if (!strstr(asset->source, include)) {
        Log_Printf("ShaderSet: %s requires the portable sprite library include\n", asset->path);
        return NULL;
    }
    const char *declaration = SHADER_DEFINES_MARK "\nvec4 mugenSourceUV;\nvec4 mugenSourceColor;\n";
    char *with_vars = Replace(base, SHADER_DEFINES_MARK, declaration);
    char *stock = with_vars ? Replace(with_vars, "void main(", "void mugenStockColor(") : NULL;
    free(with_vars);
    char *colors = stock ? Replace(stock, "gl_FragColor", "mugenSourceColor") : NULL;
    free(stock);
    const char *varying = strcmp(coord, "v_texcoord") == 0 ? "varying vec4 v_texcoord;" :
                          strcmp(coord, "vTexCoord") == 0 ? "varying vec4 vTexCoord;" : "";
    char *sample = colors ? Replace(colors, coord, "mugenSourceUV") : NULL;
    free(colors);
    if (!sample)
        return NULL;
    if (varying[0]) {
        char *fixed = Replace(sample, "varying vec4 mugenSourceUV;", varying);
        free(sample);
        sample = fixed;
        if (!sample)
            return NULL;
    }
    const char *helper =
        "\n#define FragColor gl_FragColor\n#define COMPAT_TEXTURE texture2D\n"
        "uniform float param[16];\nuniform float iTime, sTime, aspectRatio;\nuniform vec2 iResolution;\n"
        "uniform sampler2D tex1, tex2;\n"
        "vec4 ikemenSampleTex1(vec2 uv) { return texture2D(tex1, clamp(uv, 0.0, 1.0)); }\n"
        "vec4 ikemenSampleTex2(vec2 uv) { return texture2D(tex2, clamp(uv, 0.0, 1.0)); }\n"
        "vec2 ikemenSpriteUV() {\n"
        "  vec4 point = %s;\n"
        "#if defined(PERSPECTIVE_NONE)\n"
        "  return point.xy;\n"
        "#elif defined(PERSPECTIVE_STANDARD)\n"
        "  return point.xy / point.w;\n"
        "#else\n"
        "  return vec2(point.x / point.w, point.y);\n"
        "#endif\n"
        "}\n"
        "vec4 ikemenSpriteColor(vec2 uv) {\n"
        "  mugenSourceUV = vec4(uv, 0.0, 1.0);\n"
        "  mugenStockColor();\n"
        "  return mugenSourceColor;\n"
        "}\n";
    char helpers[1536];
    snprintf(helpers, sizeof(helpers), helper, coord);
    char *effect;
    if (strcmp(coord, "vTexCoord") == 0) {
        char *access = VitaParameterAccess(asset->source);
        char *packed = Replace(helpers, "uniform float param[16];",
                               "uniform vec4 mugenParams0, mugenParams1, mugenParams2, mugenParams3;");
        effect = access && packed ? Replace(access, include, packed) : NULL;
        free(access);
        free(packed);
    } else {
        effect = Replace(asset->source, include, helpers);
    }
    char *combined = NULL;
    if (effect) {
        size_t size = strlen(sample) + strlen(effect) + 2;
        combined = malloc(size);
        if (combined)
            snprintf(combined, size, "%s\n%s", sample, effect);
    }
    free(sample);
    free(effect);
    return combined;
}

#if !defined(ANDROID_PORT) || defined(MUGEN_GLES)
static GlTex *effect_textures[3];

static unsigned EffectTexture(int slot)
{
    SpriteSet *set = g_spriteShaderDraw->textures[slot];
    int index = g_spriteShaderDraw->sprite[slot];
    if (set && set->sprites && index >= 0 && index < set->sprites->capacity &&
        set->sprites->slots[index].used) {
        Sprite *sprite = (Sprite *)(set->sprites->data + set->sprites->elem_size * index);
        if (sprite->format == SPRITE_FORMAT_TEXTURE) {
            GlTex *tex = sprite->pixels;
#ifdef MUGEN_GLES
            if (tex && !tex->id && !GlTex_Revive(tex))
                tex = NULL;
#endif
            return tex ? tex->id : 0;
        }
        void *pixels = Sprite_GetPixels(set, index);
        if (pixels) {
            if (!effect_textures[slot]) effect_textures[slot] = GlTex_Create();
            GlTex *tex = effect_textures[slot];
            if (GlTex_Upload2D(sprite->h, sprite->bpp, tex, pixels, sprite->w))
                return tex->id;
        }
    }
    return 0;
}

void SpriteShader_BindResources(const int textures[2], int grab)
{
    for (int i = 0; i < 2; ++i) {
        if (textures[i] == -1) continue;
        glActiveTexture(GL_TEXTURE0 + 2 + i);
        glBindTexture(GL_TEXTURE_2D, EffectTexture(i));
        glUniform1i(textures[i], 2 + i);
    }
#ifndef MUGEN_VITA_GL
    if (grab >= 0) {
        glActiveTexture(GL_TEXTURE0 + 4);
        if (!effect_textures[2]) effect_textures[2] = GlTex_Create();
        GlTex *tex = effect_textures[2];
        if (tex && ((tex->w == g_targetWidth && tex->h == g_targetHeight && tex->id) ||
                    GlTex_Alloc(tex, g_targetWidth, g_targetHeight))) {
            glBindTexture(GL_TEXTURE_2D, tex->id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_targetWidth, g_targetHeight);
            glUniform1i(grab, 4);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(grab, 4);
        }
    }
#else
    (void)grab;
#endif
    glActiveTexture(GL_TEXTURE0);
}

void SpriteShader_FreeTextures(void)
{
    for (int i = 0; i < 3; ++i) {
        if (effect_textures[i]) GlTex_Free(effect_textures[i]);
        effect_textures[i] = NULL;
    }
}
#endif
