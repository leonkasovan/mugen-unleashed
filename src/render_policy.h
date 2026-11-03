/*
 * Renderer selection policy: one place that decides the effective renderer.
 *
 * RENDERING_PLAN.md section 10.1 and 12.7 ask for requested mode, effective
 * renderer and the reason to be distinct, explicit concepts. Before this, the
 * decision was spread over #ifdef blocks in config.c (Android forcing System)
 * and system.c (the Vita fallback), so neither the table nor its reasons could
 * be inspected or tested.
 *
 * This unit is pure: the caller passes what the build actually provides (the
 * capabilities), and gets back the effective mode plus a reason string. No GL,
 * no SDL, no engine globals -- so tools/test_render_policy.sh can check the
 * whole table on the host.
 *
 * Fidelity note: this replaces the *decision*, never the drawing. The
 * original's mode set and its cfg values are untouched, and the desktop paths
 * (where every mode is compiled) resolve to whatever the config asked for.
 */
#ifndef MUGEN_RENDER_POLICY_H
#define MUGEN_RENDER_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

/* What this build can actually render with. Filled by the caller from the
 * compiled-in function tables and the runtime capability probe -- not from
 * macros or link success (plan section 10.6). */
typedef struct RenderCaps {
    int system;         /* software renderer available (always 1) */
    int gl_screen;      /* OpenGLScreen presenter available */
    int gl_sprite;      /* GPU sprite renderer available (shaders + FBO) */
} RenderCaps;

/* The outcome. reason is a static string, NULL when nothing was overridden. */
typedef struct RenderDecision {
    int effective;
    int fell_back;          /* requested != effective */
    const char *reason;
} RenderDecision;

/* Renderer mode values, mirroring src/game.h (kept local so this unit needs
 * no engine headers and stays testable on the host). */
#define RENDER_POLICY_SYSTEM        0
#define RENDER_POLICY_OPENGL_SCREEN 1
#define RENDER_POLICY_OPENGL        2

/*
 * Resolves the requested config mode against what is available.
 *
 * Rules:
 *  - System is always available; it is the recovery path on every platform.
 *  - OpenGL requires gl_sprite AND gl_screen: without the GPU sprite tables the
 *    mode would draw through NULL/stub entries (plan section 12.2: removing the
 *    System force alone selects an incomplete path).
 *  - OpenGLScreen requires gl_screen only: it rasterizes on the CPU and uses GL
 *    to present, so it never needs the sprite programs.
 *  - An unknown mode resolves to System with a reason (the config parser
 *    rejects unknown values first, so this is the defensive path).
 *
 * Never claims a capability the caller did not report.
 */
void RenderPolicy_Resolve(int requested, const RenderCaps *caps, RenderDecision *out);

#ifdef __cplusplus
}
#endif

#endif /* MUGEN_RENDER_POLICY_H */
