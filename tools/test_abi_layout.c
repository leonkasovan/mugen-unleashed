/*
 * M4 layout report (§8 langkah 5+7): prints sizeof/offsetof for the structs
 * and members that raw-offset sites read, on the compiler that builds it.
 *
 * Built by tools/test_abi_layout.sh twice: once with the 32-bit kit (must
 * match the reference offsets documented in AGENTS.md) and once with the
 * 64-bit kit (shows where the natural layout diverges -- every divergence
 * is a site that must use member access, never a hardcoded offset).
 *
 * Includes only the self-contained type headers (no mugen.h/SDL), with the
 * minimal prelude the headers need. This is a report, not a pass/fail gate:
 * the ABI01/ABI02 runtime gates live in tools/test_render_host.c (32-bit
 * reference side).
 */
#include <stdio.h>
#include <stddef.h>

/* Full engine header: needs SDL headers (SDL 1.2 on the 32-bit kit, SDL2
 * on the 64-bit kit) plus glad/minizip/lua includes like any engine unit. */
#include "mugen.h"

#ifdef main
#undef main             /* SDL renames main() to SDL_main on Windows */
#endif

#define SZ(t) printf("sizeof(%s) = 0x%x\n", #t, (unsigned)sizeof(t))
#define OFF(t, f) printf("offsetof(%s, %s) = 0x%x\n", #t, #f, (unsigned)offsetof(t, f))

int main(void)
{
#if MUGEN_LAYOUT_32BIT
    printf("ABI: 32-bit reference\n");
#else
    printf("ABI: 64-bit port\n");
#endif
    /* Camera sites (camera.c). */
    SZ(Bg);
    OFF(Bg, legacy);
    SZ(BgElem);
    OFF(BgElem, y);
    /* Input site (ai.c). */
    SZ(InputBuf);
    OFF(InputBuf, keys);
    /* Projectile site (char.c / playerstate.c). */
    SZ(Projectile);
    OFF(Projectile, afterimage);
    /* GlTex: pointer-free by design (§8.6), stable on both ABIs. */
    SZ(GlTex);
    OFF(GlTex, size);
    /* Containers whose data pointer grows on 64-bit. */
    SZ(KeyedArray);
    OFF(KeyedArray, data);
    SZ(Sprite);
    OFF(Sprite, pixels);
    SZ(SpriteSet);
    SZ(Palette);
    OFF(Palette, data);
    return 0;
}
