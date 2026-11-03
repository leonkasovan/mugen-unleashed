/* GLES 3.0 loader for the M8 backend (RENDERING_PLAN.md section 12).
 *
 * Wraps third_party/glad-es-3.0 (generated: --api="gles2=3.0") under a
 * distinct include name so it is never confused with the desktop glad
 * (third_party/glad-2.1, GL 2.1 compatibility profile). The two loaders
 * share an include guard upstream, so including both in one translation
 * unit is a build error by design -- a file is either desktop-GL or
 * GLES, never both (plan section 12.1).
 */
#ifndef MUGEN_GLAD_ES_H
#define MUGEN_GLAD_ES_H

/* The vendored GLES loader lives at third_party/glad-es-3.0/; the build
 * adds that directory to the include path when MUGEN_GLES is set. */
#include <glad.h>

/* GLES has no fixed-function matrix stack: these desktop entry points the
 * engine's GL renderer uses must not compile under MUGEN_GLES. Any use is
 * a port bug, caught here instead of failing obscurely at link time. */
#ifdef glBegin
#undef glBegin
#define glBegin(x) GL_BEGIN_REMOVED_USE_VBO
#endif

#endif /* MUGEN_GLAD_ES_H */
