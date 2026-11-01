// =============================================================================
// Android port of mugen-unleashed.
//
// SDL2 does not expose SDL_SetVideoMode / SDL_HWSURFACE / SDL_SWSURFACE; it
// replaced them with a window + renderer model. The engine uses exactly those
// three calls (plus SDL_Flip and SDL_UpdateRect) as its whole video contract, so
// what it needs is a translation layer rather than a rewrite: SDL_SetVideoMode
// becomes "create a window and a streaming renderer, then hand back a software
// surface matching the requested format".
//
// That keeps every SDL call site in src/ untouched, which matters because the
// engine's own video code (src/video.c, src/gfx.c) reproduces the original
// M.U.G.E.N binary and should not be edited for one target.
// =============================================================================
#ifndef ANDROID_MAIN_H
#define ANDROID_MAIN_H

#ifdef __ANDROID__

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The SDL 1.2 video subset, implemented over SDL2.
 *
 * Android_SetVideoMode creates the window, the streaming texture and a software
 * surface whose pixels the engine writes directly; Android_Flip/UpdateRect
 * upload that surface and present it.
 */
int  Android_VideoInit(void);
SDL_Surface *Android_SetVideoMode(int width, int height, int bpp, Uint32 flags);
void Android_UpdateRect(Sint32 x, Sint32 y, Uint32 w, Uint32 h);
/* SDL 1.2's SDL_AudioDriverName: copies the current driver name. */
char *Android_AudioDriverName(char *namebuf, int maxlen);

/* SDL 1.2's SDL_GetKeyState: returns the key array and its length. */
const Uint8 *Android_GetKeyState(int *count);

/* Accepts SDL 1.2's (surface, x, y, w, h) and forwards the rectangle only. */
void Android_UpdateRectX(SDL_Surface *surf, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
void Android_Flip(void);
void Android_VideoQuit(void);
/* Marks the end of a frame and presents it at most once (see android_video.c). */
void Android_PresentFrame(void);
int  Android_ToggleFullscreen(SDL_Surface *screen);

/* The engine-facing aliases for these live further down, in the full SDL 1.2
 * compatibility block, together with the argument-shape handling they need. */

/* Values SDL2 dropped; kept so src/video.c compiles unchanged. */
#ifndef SDL_HWSURFACE
#define SDL_HWSURFACE       0x00000001
#endif
#ifndef SDL_SWSURFACE
#define SDL_SWSURFACE       0x00000000
#endif
#ifndef SDL_ASYNCBLIT
#define SDL_ASYNCBLIT       0x00000004
#endif
#ifndef SDL_ANYFORMAT
#define SDL_ANYFORMAT       0x10000000
#endif
#ifndef SDL_HWPALETTE
#define SDL_HWPALETTE       0x20000000
#endif
#ifndef SDL_DOUBLEBUF
#define SDL_DOUBLEBUF       0x40000000
#endif
#ifndef SDL_FULLSCREEN
#define SDL_FULLSCREEN      0x80000000
#endif
#ifndef SDL_RESIZABLE
#define SDL_RESIZABLE       0x00000010
#endif
#ifndef SDL_NOFRAME
#define SDL_NOFRAME         0x00000020
#endif
#ifndef SDL_HWACCEL
#define SDL_HWACCEL         0x00000100
#endif
/* SDL 1.2 surface flags the engine tests bitwise. SDL2 renumbered them, so the
 * 1.2 values are restored here: the engine's own code compares against these,
 * and Android_SetVideoMode never sets them on the surface it returns. */
#ifndef SDL_SRCALPHA
#define SDL_SRCALPHA        0x00010000
#endif
#ifndef SDL_SRCCOLORKEY
#define SDL_SRCCOLORKEY     0x00001000
#endif
#ifndef SDL_RLEACCEL
#define SDL_RLEACCEL        0x00008000
#endif

/* GL types: no GL header is included on this target, but video.c declares
 * projection helpers with GLdouble parameters. */
typedef double GLdouble;
typedef float  GLfloat;
typedef int    GLint;
typedef unsigned int GLenum;

#ifndef SDL_PREALLOC
#define SDL_PREALLOC        0x01000000
#endif
/*
 * SDL_OPENGL is still defined by SDL2, but its value differs (0x00000002 in
 * 1.2, a separate SDL_WindowFlags bit in SDL2). The engine ORs it into the
 * flags it passes to SDL_SetVideoMode, which this port ignores, so any distinct
 * non-zero value is enough to keep the comparisons in video.c meaningful.
 */
#ifndef SDL_OPENGL
#define SDL_OPENGL          0x00000002
#endif

/*
 * SDL 1.2's SDLKey array bound and the type name. SDL2 renamed the enumerator
 * to SDLK_LAST -> SDL_NUM_SCANCODES and the type to SDL_Keycode. title.c uses
 * both to bound-check a config value and to name a key.
 */
#define SDLK_LAST           SDL_NUM_SCANCODES
typedef SDL_Keycode SDLKey;

/* SDL 1.2 return convention, which SDL_Init call sites rely on. */
#ifndef SDL_SUCCESS
#define SDL_SUCCESS 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ANDROID__ */

#endif /* ANDROID_MAIN_H */

/* ===========================================================================
 * Full SDL 1.2 compatibility surface.
 *
 * Included by every translation unit via -include (see the CMakeLists), because
 * the engine calls these names in a dozen files and threading #ifdefs through
 * all of them would bury the reconstruction's fidelity behind port code.
 *
 * Each entry is a genuine SDL 1.2 name that SDL2 removed or renamed.
 * =========================================================================== */
#ifdef __ANDROID__

/* --- removed constants ---------------------------------------------------- */

/* SDL2 renamed the keysym; the original's name is kept at the call sites. */
#ifndef SDLK_SCROLLOCK
#define SDLK_SCROLLOCK SDLK_SCROLLLOCK
#endif

/* SDL_GL_SWAP_CONTROL was a deprecated 1.2 hint; SDL2 has no equivalent and the
 * engine only calls SDL_GL_SetAttribute with it before creating a GL context,
 * which this port never does. Accepting and ignoring it is correct. */
#ifndef SDL_GL_SWAP_CONTROL
#define SDL_GL_SWAP_CONTROL 16
#endif

/* SDL_Color gained a fifth field in some builds; the engine writes color.unused.
 * SDL_Keysym likewise. Neither field exists in SDL2, so the writes are dropped
 * with macros rather than edited out of the reconstruction. */
#ifndef SDL_COLOR_NO_UNUSED_FIELD
#define SDL_COLOR_NO_UNUSED_FIELD 1
#endif

/* --- removed functions ---------------------------------------------------- */

/* Key repeat is a platform setting in SDL2. The engine calls this from
 * Console_Toggle, which is keyboard-driven and unreachable on a device. */
#define SDL_EnableKeyRepeat(a, b) (0)

/* SDL2 has no cursor on Android; the engine hides it when fullscreen. */
#define SDL_ShowCursor(state) (0)

/* SDL_GetKeyState became SDL_GetKeyboardState, which takes no count argument.
 * The engine stores a contiguous snapshot of the key array, so a small wrapper
 * keeps that shape: it records the array length the engine expects. */
#define SDL_GetKeyState(countp) Android_GetKeyState(countp)

/* SDL2 removed SDL_EnableUNICODE; text arrives as SDL_TextInputEvent instead. */
#define SDL_EnableUNICODE(on) (0)

/* Renamed in SDL2. */
#define SDL_AudioDriverName(buf, len) (Android_AudioDriverName(buf, len))
#define SDL_WM_SetCaption(title, icon) ((void)0)
/* SDL_putenv still exists in SDL2 but returns int; the engine ignores it. */
#define SDL_putenv(...) ((void)0)
/* SDL2 dropped the 1.2 GL attribute helper path used before SetVideoMode. */
#define SDL_GL_SetAttribute(...) (0)

/* WOps-level loaders that SDL_mixer 1.2 had; SDL2's take an extra argument
 * (handled at the call site in bgmplugins.c), so nothing here. */

/* --- OpenGL stubs ---------------------------------------------------------
 *
 * glrender.c and gldetect.c are excluded from this build, and the software
 * (System) renderer is the only one config.c will select. However prim.c and
 * gfx.c still *define* their OpenGL twins (Gfx_GlClear, Gfx_GlPutPixel, ...) and
 * those bodies reference GL entry points and enums. They are only ever reached
 * through the g_gfx* renderer vtable, which is never pointed at them here.
 *
 * Rather than delete faithful reconstructions or wrap forty call sites, the GL
 * names they use are declared as no-ops. Any call is therefore a silent no-op
 * instead of a link error -- which is the correct behaviour for code that
 * selects its renderer elsewhere, and keeps the reconstruction intact.
 */
#if !defined(GL_COLOR_BUFFER_BIT)
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_SCISSOR_TEST     0x0C11
#define GL_POINTS           0x0000
#define GL_LINES            0x0001
#define GL_LINE_STRIP       0x0003
#define GL_TRIANGLES        0x0004
#define GL_TRIANGLE_FAN     0x0006
#define GL_TEXTURE_2D       0x0DE1
#define GL_BLEND            0x0BE2
#define GL_SRC_ALPHA        0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_MODELVIEW        0x1700
#define GL_PROJECTION       0x1701
#define GL_TEXTURE          0x1702
#define GL_QUADS            0x0007
#define GL_LOGIC_OP         0x0BF1
#define GL_XOR              0x5382
#define GL_ALPHA_FUNC       0x0BC0
#define GL_BLEND_EQUATION   0x8009
#endif

#ifndef ANDROID_GL_NOOP
#define ANDROID_GL_NOOP 1
#define glClearColor(...)       ((void)0)
#define glClearDepth(...)       ((void)0)
#define glClear(...)            ((void)0)
#define glDisable(...)          ((void)0)
#define glEnable(...)           ((void)0)
#define glBegin(...)            ((void)0)
#define glEnd()                 ((void)0)
#define glVertex2f(...)         ((void)0)
#define glVertex2i(...)         ((void)0)
#define glColor4f(...)          ((void)0)
#define glColor4ub(...)         ((void)0)
#define glColor3f(...)          ((void)0)
#define glBeginEnd(...)         ((void)0)
#define glScissor(...)          ((void)0)
#define glBlendFunc(...)        ((void)0)
#define glViewport(...)         ((void)0)
#define glMatrixMode(...)       ((void)0)
#define glLoadIdentity()        ((void)0)
#define glOrtho(...)            ((void)0)
#define glTexEnvi(...)          ((void)0)
#define glBindTexture(...)      ((void)0)
#define glTexParameteri(...)    ((void)0)
#define glPushMatrix()          ((void)0)
#define glPopMatrix()           ((void)0)
#define glTranslated(...)       ((void)0)
#define glScaled(...)           ((void)0)
#define glLineWidth(...)        ((void)0)
#define glPointSize(...)        ((void)0)
#define glBlendEquation(...)    ((void)0)
#define glLogicOp(...)          ((void)0)
#define glPushAttrib(...)       ((void)0)
#define glPopAttrib()           ((void)0)
#define glTexCoord2f(...)       ((void)0)
#define glTexCoord2i(...)       ((void)0)
#define MugenVita_LogicOp(...)  ((void)0)
#endif

/* --- the video subset ----------------------------------------------------- */

/* Declared in full above; these aliases must come last so the definitions win. */
#define SDL_SetVideoMode(...) Android_SetVideoMode(__VA_ARGS__)
#define SDL_UpdateRect(...) Android_UpdateRectX(__VA_ARGS__)
#define SDL_Flip(...)       Android_Flip()
#define SDL_WM_ToggleFullScreen(...) (1)

/* No GL renderer on this target: glrender.c is excluded and the software
 * renderer is the only path. gfx.c still nominates GL calls inside its
 * RENDER_OPENGL branches, which can never run here, so they are compiled out
 * through this macro instead of being deleted from the reconstruction. */
#define SDL_GL_SwapBuffers() ((void)0)

/* The Vita port's frame profiling has no Android equivalent yet; a no-op keeps
 * the call sites readable and is honest about the missing measurement. */
#define Vita_PerfAddScale(us)  ((void)(us))
#define Vita_PerfAddFlip(us)   ((void)(us))

/* SDL_Color no longer has a fifth "unused" byte, and SDL_Keysym no longer has
 * "unicode". Both are assigned by the engine; the field names are rewritten. */
#define SDL_COLOR_UNUSED_FIELD 1

/* SDL_GetWMInfo does not exist in SDL2; error.c's Windows path is not compiled
 * on Android, but symbols.h may still reference the name. */
#define SDL_GetWMInfo(info) (0)

#endif /* __ANDROID__ */
