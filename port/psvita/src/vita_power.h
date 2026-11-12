#ifndef MUGEN_VITA_POWER_H
#define MUGEN_VITA_POWER_H

/*
 * Port addition: ask the Vita for the clocks retail games run at.
 *
 * Unsigned code starts on the system's default profile, which is below what a
 * game gets after it calls scePowerSetClockFrequency. This engine is CPU-bound
 * (see the perf breakdown in src/perf.c), so that difference is felt directly:
 * the same build runs faster for free once the clocks are raised.
 *
 * Best effort on purpose. A rejected request is not a failure -- some system
 * states refuse it, and the port has always run on the default clocks -- so the
 * outcome is logged once and the game continues either way.
 */
void VitaPower_Init(void);

#endif /* MUGEN_VITA_POWER_H */