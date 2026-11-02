/* Host-only link instrumentation. Calls the real SDL2 implementation while
 * counting uploads/presents from android_video.c. No SDL function is mocked.
 * Linked only by port/android/tests/CMakeLists.txt using GNU ld --wrap. */
#include <SDL.h>

int AndroidTest_Uploads;
int AndroidTest_Presents;

extern int __real_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                                    const void *pixels, int pitch);
extern void __real_SDL_RenderPresent(SDL_Renderer *renderer);

int __wrap_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                             const void *pixels, int pitch)
{
    AndroidTest_Uploads++;
    return __real_SDL_UpdateTexture(texture, rect, pixels, pitch);
}

void __wrap_SDL_RenderPresent(SDL_Renderer *renderer)
{
    AndroidTest_Presents++;
    __real_SDL_RenderPresent(renderer);
}
