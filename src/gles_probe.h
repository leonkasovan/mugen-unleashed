/* GLES capability probe (M8, RENDERING_PLAN.md section 12). */
#ifndef MUGEN_GLES_PROBE_H
#define MUGEN_GLES_PROBE_H

#ifdef MUGEN_GLES
/* Returns 1 for a usable ES 3.0+ context and writes the texture limit if
 * requested. Returns 0 without setting the output if the context is unusable. */
int Gles_Probe(int *max_texture_size);

/*
 * Port addition: writes the driver's strings, the limits this renderer cares
 * about and which optional extensions are present, to mugen.log. Debug builds
 * only -- it exists so an optimisation decision can be made from what the device
 * actually exposes instead of from assumptions about it.
 */
void Gles_LogCapabilities(void);
#endif

#endif
