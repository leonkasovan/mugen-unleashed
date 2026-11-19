#ifndef MUGEN_VIDEO_BG_H
#define MUGEN_VIDEO_BG_H

#include <stdint.h>

typedef struct VideoBg VideoBg;
VideoBg *VideoBg_Open(const char *path, int loop);
void VideoBg_Close(VideoBg *video);
void VideoBg_Reset(VideoBg *video);
const uint8_t *VideoBg_Frame(VideoBg *video, int64_t time_us, int width, int height,
                             int *frame_width, int *frame_height, unsigned *serial);

#endif
