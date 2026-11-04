/* GLES capability probe (M8, RENDERING_PLAN.md section 12). */
#ifndef MUGEN_GLES_PROBE_H
#define MUGEN_GLES_PROBE_H

#ifdef MUGEN_GLES
/* Capability bits Gles_Probe reports. */
#define GLES_CAP_NPOT        0x01
#define GLES_CAP_FBO         0x02
#define GLES_CAP_GLSL_ES_100  0x04

/* Probes the current context. Returns 1 and fills *caps when the context is a
 * usable ES 2.0+ with NPOT and FBO; 0 otherwise. */
int Gles_Probe(unsigned int *caps);
#endif

#endif
