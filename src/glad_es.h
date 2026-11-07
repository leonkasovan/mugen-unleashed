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

/*
 * Claim the desktop GL header too.
 *
 * glad (both dialects) substitutes a function-pointer variable for every GL
 * entry point and then `#define`s the GL name onto it: `#define glClearColor
 * glad_glClearColor`. Any system header read after that point that declares the
 * real prototype therefore rewrites into a declaration of `glad_glClearColor`
 * and the build stops with "redeclared as different kind of symbol".
 *
 * glad-2.1 guards against this by defining `__gl_h_`, the guard of glvnd's
 * <GL/gl.h>. glad-es-3.0 only defines `__gl2_h_`/`__gl3_h_`, because a GLES
 * build is not expected to see a desktop GL header -- but this one does:
 * src/video.c includes <GL/glu.h> after mugen.h, and glu.h pulls in <GL/gl.h>.
 * Defining the guard here makes the system header self-suppress exactly as it
 * does on the desktop build, instead of fighting the loader.
 */
#ifndef __gl_h_
#define __gl_h_
#endif

/* GLES has no fixed-function matrix stack: these desktop entry points the
 * engine's GL renderer uses must not compile under MUGEN_GLES. Any use is
 * a port bug, caught here instead of failing obscurely at link time. */
#ifdef glBegin
#undef glBegin
#define glBegin(x) GL_BEGIN_REMOVED_USE_VBO
#endif

#endif /* MUGEN_GLAD_ES_H */
