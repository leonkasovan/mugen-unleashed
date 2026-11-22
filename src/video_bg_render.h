#ifndef MUGEN_VIDEO_BG_RENDER_H
#define MUGEN_VIDEO_BG_RENDER_H

struct VideoBgLayer;
struct VideoBgLayer *VideoBgLayer_Open(Ini *ini, const char *dir);
void VideoBgLayer_Close(struct VideoBgLayer *layer);
void VideoBgLayer_Reset(struct VideoBgLayer *layer);
void VideoBgLayer_Draw(struct VideoBgLayer *layer, int ticks, float x, float y);
int VideoBgLayer_IsType(const char *type);

#endif
