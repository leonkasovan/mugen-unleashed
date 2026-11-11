/* SDL2-only mixer query. The chunk size belongs to the engine request;
 * SDL2_mixer exposes frequency/format/channels through Mix_QuerySpec. */
#include "mugen.h"
#include <SDL_mixer.h>
int Sound_QueryMixerSpec(int *frequency, Uint16 *format, int *channels, int *chunksize)
{
    int opened = Mix_QuerySpec(frequency, format, channels);
    if (opened && chunksize != NULL) *chunksize = g_sndChunkSize;
    return opened;
}
