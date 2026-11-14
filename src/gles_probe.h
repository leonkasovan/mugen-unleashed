/* GLES capability probe (M8, RENDERING_PLAN.md section 12). */
#ifndef MUGEN_GLES_PROBE_H
#define MUGEN_GLES_PROBE_H

#ifdef MUGEN_GLES
/* Capability bits Gles_Probe reports. */
#define GLES_CAP_NPOT        0x01
#define GLES_CAP_FBO         0x02
#define GLES_CAP_GLSL_ES_100  0x04

/* Probes the current context. Returns 1 and fills *caps when the context is a
 * usable ES 3.0+ with NPOT and FBO; 0 otherwise. */
int Gles_Probe(unsigned int *caps);

/*
 * Port addition: writes the driver's strings, the limits this renderer cares
 * about and which optional extensions are present, to mugen.log. Debug builds
 * only -- it exists so an optimisation decision can be made from what the device
 * actually exposes instead of from assumptions about it.
 */
void Gles_LogCapabilities(void);
#endif

#endif
