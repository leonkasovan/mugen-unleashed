/*
 * PS Vita clock request (port/psvita).
 *
 * Not part of the original binary; there is no address to name, and no
 * equivalent exists on the desktop builds -- an OS there owns the clock.
 *
 * The Vita exposes scePowerSetClockFrequency(cpu, gpu, bus) in MHz, with the
 * retail maximum at 444/222/222. A game may ask for it; unsigned homebrew runs
 * on the system's default profile until it does. Nothing in this port asked,
 * so every measurement taken so far was taken at the default clocks -- which is
 * one reason "the Vita runs 3D games fine, why not this" was the wrong question:
 * the console was doing what it was told, at the clocks it was given.
 *
 * The values are the documented maximum, not a guess. scePowerSetClockFrequency
 * returns a status code, and the effective clocks can be read back with
 * scePowerGetCpuClockFrequencyInt; both are logged so the next device log says
 * what the engine actually ran at rather than what it asked for.
 */
#include "mugen.h"

#ifdef MUGEN_VITA_GL
#include <psp2/power.h>
#include "vita_power.h"

#define VITA_POWER_ARM_MHZ 444
#define VITA_POWER_GPU_MHZ 222
#define VITA_POWER_BUS_MHZ 222

/*
 * VitaSDK exposes the per-domain setters rather than the combined
 * scePowerSetClockFrequency a game may use; the effect is the same and each
 * returns its own status, so they are reported separately.
 */
void VitaPower_Init(void)
{
    int arm = scePowerSetArmClockFrequency(VITA_POWER_ARM_MHZ);
    int gpu = scePowerSetGpuClockFrequency(VITA_POWER_GPU_MHZ);
    int bus = scePowerSetBusClockFrequency(VITA_POWER_BUS_MHZ);

    Log_Printf("power: clocks arm/gpu/bus %d/%d/%d MHz, requested %d/%d/%d (status %d/%d/%d)\n",
               scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(),
               scePowerGetBusClockFrequency(), VITA_POWER_ARM_MHZ, VITA_POWER_GPU_MHZ,
               VITA_POWER_BUS_MHZ, arm, gpu, bus);
    if (arm < 0 || gpu < 0 || bus < 0) {
        /* Not fatal. Some system states refuse the request; the default profile
         * is what the port ran on before this existed, so it continues. */
        Log_Printf("power: the higher clocks were refused; running on the default profile\n");
    }
}
#endif /* MUGEN_VITA_GL */