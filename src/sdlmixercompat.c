/*
 * Mix_QuerySpecEx: not in stock SDL_mixer 1.2, only in Elecbyte's build.
 *
 * src/sound.c calls it after Mix_OpenAudio to learn the mixer's real frequency,
 * output format and channel count, plus the chunk size. Stock SDL_mixer's
 * Mix_QuerySpec returns the first three but has no way to report the chunk
 * size, so that one argument is supplied here.
 *
 * The chunk size reported is the one the engine asked Mix_OpenAudio for
 * (g_sndChunkSize). Stock SDL_mixer opens the device with exactly the chunk
 * size it is given -- it does not round it -- so reporting the request back is
 * what Elecbyte's build would have returned. If a future SDL_mixer did round
 * it, this would be wrong; that is checked by the mugen.log sound lines rather
 * than assumed.
 *
 * This exists only for the static build (see the Makefile). The MSVC/CMake
 * build links the game's original SDL_mixer.dll, which exports the real
 * function, so this file is not compiled there.
 */
#include "mugen.h"

extern int __cdecl Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);

/* The chunk size the engine requested; sound.c sets it before Mix_OpenAudio. */
extern int g_sndChunkSize;

int __cdecl Mix_QuerySpecEx(int *frequency, Uint16 *format, int *channels, int *chunksize)
{
    int num;

    num = Mix_QuerySpec(frequency, format, channels);
    if (num == 0) {
        return 0;
    }
    if (chunksize != NULL) {
        *chunksize = g_sndChunkSize;
    }
    return num;
}
