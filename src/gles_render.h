/*
 * GLES renderer lifecycle hooks (M8, RENDERING_PLAN.md section 12.6).
 *
 * Android can destroy the GL context at any time: the app is backgrounded, the
 * GPU driver resets, or the system reclaims memory. Every GL object name the
 * renderer holds is then stale -- drawing with them goes nowhere and deleting
 * them is undefined -- so the renderer has to be told, and has to be able to
 * rebuild once a new context is current.
 *
 * What survives a context loss and what does not:
 *   survives  the engine's CPU-side state: SpriteSet data, decoded sprite
 *             pixels, palettes, the draw parameters;
 *   lost      texture names, framebuffer/renderbuffer names, program and
 *             shader names, the vertex buffer.
 *
 * So the renderer keeps every GlTex's dimensions (never cleared on loss) and
 * revives the object on next use; nothing has to be re-queued from the engine.
 * This is deliberately different from the SDL_Renderer path's
 * Sdl2_ResetRenderer, which is a separate lifecycle for a separate backend.
 */
#ifndef MUGEN_GLES_RENDER_H
#define MUGEN_GLES_RENDER_H

#ifdef MUGEN_GLES

/* 1 when the renderer has a context and its programs are linked. */
int Gles_IsReady(void);

/* Capability bits the last probe resolved (GLES_CAP_* in gles_probe.h). */
unsigned int Gles_LastCaps(void);

/*
 * Called after the client context has gone away, before it is deleted.
 *
 * Drops every GL name the renderer owns -- programs, the vertex buffer, and
 * each live texture's name and depth renderbuffer -- while keeping the sizes so
 * the objects can be recreated. Safe to call when nothing is initialised.
 */
void Gles_OnContextLost(void);

/*
 * Called once a new context is current and the GLES entry points have been
 * loaded again: recompiles and relinks the programs, re-establishes the GL
 * state, and drops any cached texture storage description so the next upload
 * reallocates rather than calling glTexSubImage2D on a dead name.
 *
 * Returns 1 when the renderer is usable again, 0 when the rebuild failed (the
 * caller keeps the System renderer in that case).
 */
int Gles_OnContextRestore(int drawable_w, int drawable_h);

#endif /* MUGEN_GLES */

#endif /* MUGEN_GLES_RENDER_H */
