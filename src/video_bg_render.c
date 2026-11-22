/* mugen-unleashed: renders a decoded video using the engine's RGBA sprite path. */
#ifdef MUGEN_VIDEO_BG
#include "mugen.h"
#include "video_bg.h"
#include "video_bg_render.h"

struct VideoBgLayer {
    VideoBg *video;
    SpriteSet *set;
    GlTex *texture;
    unsigned uploaded;
    int width, height;
};

int VideoBgLayer_IsType(const char *type)
{
    return type && Str_StartsWithI(type, "video") && strlen(type) == 5;
}

struct VideoBgLayer *VideoBgLayer_Open(Ini *ini, const char *dir)
{
    const char *file = Ini_GetValue(ini, "path");
    const char *loop = Ini_GetValue(ini, "loop");
    char path[0x208];
    if (!file || !*file || strlen(file) + strlen(dir) + 2 > sizeof(path) ||
        (g_renderMode != RENDER_OPENGL && g_renderMode != RENDER_OPENGL_SCREEN))
        return NULL;
    Path_Join(file, dir, path);
    struct VideoBgLayer *layer = calloc(1, sizeof(*layer));
    if (!layer)
        return NULL;
    layer->video = VideoBg_Open(path, loop && (!strcmp(loop, "1") || Str_StartsWithI(loop, "true")));
    layer->set = SpriteSet_Create(1, 0);
    Sprite sprite = {0};
    sprite.bpp = 32;
    sprite.format = SPRITE_FORMAT_TEXTURE;
    sprite.pal_index = -1;
    sprite.scale_x = sprite.scale_y = 1;
    if (!layer->video || !layer->set || !SpriteSet_AddSprite(layer->set, &sprite, 0, 0)) {
        VideoBgLayer_Close(layer);
        return NULL;
    }
    return layer;
}

void VideoBgLayer_Close(struct VideoBgLayer *layer)
{
    if (!layer)
        return;
    VideoBg_Close(layer->video);
    if (layer->set)
        SpriteSet_Free(layer->set);
    if (layer->texture)
        GlTex_Free(layer->texture);
    free(layer);
}

void VideoBgLayer_Reset(struct VideoBgLayer *layer)
{
    VideoBg_Reset(layer->video);
}

void VideoBgLayer_Draw(struct VideoBgLayer *layer, int ticks, float x, float y)
{
    int w, h;
    unsigned serial;
    /* Bg_Tick runs before the first draw. WebM timestamps are rounded to milliseconds. */
    int64_t elapsed = ticks > 0 ? (int64_t)(ticks - 1) * 1000000 / 60 : 0;
    const uint8_t *pixels = VideoBg_Frame(layer->video, elapsed + 500,
                                         g_targetWidth, g_targetHeight, &w, &h, &serial);
    if (!pixels)
        return;
    if (!layer->texture)
        layer->texture = GlTex_Create();
    if (!layer->texture)
        return;
    if (!layer->texture->id || layer->uploaded != serial || layer->width != w || layer->height != h) {
        if (!GlTex_UploadRGBA(h, w, layer->texture, pixels))
            return;
        layer->uploaded = serial;
        layer->width = w;
        layer->height = h;
    }
    Sprite *sprite = KeyedArray_Get(layer->set->sprites, 0, 0);
    sprite->pixels = layer->texture;
    sprite->w = w;
    sprite->h = h;
    DrawParams dp = {0};
    dp.flags = DRAW_POS | DRAW_SCALE;
    dp.x = (g_targetWidth - w) * 0.5f + x;
    dp.y = (g_targetHeight - h) * 0.5f + y;
    dp.scale_x = dp.scale_y = dp.scale2_x = dp.scale2_y = 1;
    dp.top_stretch = dp.bottom_stretch = dp.width_ratio = dp.alpha_src = 1;
    Sprite_DrawGL(layer->set, 0, &dp);
}
#endif
