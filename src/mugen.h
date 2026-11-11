/*
 * Common header of the engine sources. Include it first.
 */
#ifndef MUGEN_H
#define MUGEN_H

/*
 * Windows-only declarations. On other platforms compat_linux.h supplies the
 * MSVC keywords, Windows type names and the few Win32 calls that have a
 * faithful POSIX equivalent, so the engine sources compile unchanged.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include "compat_linux.h"
#include <alloca.h>
#include <sys/stat.h>
#endif

/* UINTPTR_MAX, used below to detect the pointer width on every compiler. */
#include <stdint.h>

/*
 * Port addition: does the build use the native pthread_t/pthread_mutex_t/
 * pthread_cond_t instead of the 32-bit pthreads-win32 shapes the original
 * stored (see bgm.h)? Only the 32-bit Windows reference uses that layout;
 * POSIX targets and 64-bit MinGW take the native types. Derived here, in the
 * one header every source includes, so globals.c, bgm.c and the lock sites in
 * gameloop.c/system.c/matchload.c all agree without a shared -D.
 */
/* Port fix: Vita's 32-bit pthread_t is a 4-byte integer, not the original
 * Windows 8-byte handle. Native pthread types are required on every POSIX
 * target as well as on 64-bit MinGW. Android keeps its SDL thread path. */
#if !defined(ANDROID_PORT) && (!defined(_WIN32) || (defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ > 4))
#define BGM_NATIVE_PTHREAD 1
#endif

/*
 * glad must come before <SDL.h>. SDL_opengl.h includes <GL/gl.h>, which defines
 * __gl_h_ -- and glad.h refuses to load if that happened first. Including glad
 * here defines __gl_h_ itself, so the system GL header then self-suppresses and
 * glad becomes the single source of GL declarations, fixed-function pipeline
 * included (it is generated with the compatibility profile).
 */
#ifdef MUGEN_GLES
/*
 * GLES 3.0 backend: Android (M8, RENDERING_PLAN.md section 12) and Linux ARM64
 * (PROBLEMS.md P1). glad-es-3.0 becomes the GL declaration source -- never the
 * desktop glad -- and the build compiles src/gles_render.c instead of
 * src/glrender.c. No fixed-function pipeline, no sampler1D, GLSL ES shaders.
 *
 * Checked before ANDROID_PORT so the two platforms share one rule: the GLES
 * dialect is selected by MUGEN_GLES alone, and a file is either desktop-GL or
 * GLES, never both. The two loaders share an include guard upstream, so
 * including both in one translation unit is a build error by design.
 */
#include <glad_es.h>
#elif defined(ANDROID_PORT)
/* Android uses native SDL2 rendering; desktop GL is not compiled.
 * MUGEN_GLES (M8) selects the GLES 3.0 backend instead; without it Android has
 * no GL declarations at all. Default OFF: System stays the renderer until the
 * GLES path is qualified per RENDERING_PLAN.md section 12. */
#elif defined(MUGEN_VITA_GL)
/*
 * PS Vita port (port/psvita): glad is not part of that build. It is generated
 * for the desktop GL loader (wglGetProcAddress / glXGetProcAddressARB against
 * libGL.so / opengl32.dll), which the Vita does not have. The native vitaGL
 * adapter owns the display, while SDL2 supplies input/audio and the System
 * presenter. vitaGL declares GL over SceGxm in <vitaGL.h>, included directly
 * and becomes the single source of GL declarations on this target. Capability
 * probing and the few entry points vitaGL lacks move to port/psvita/src, whose
 * header is included here so that symbols.h can map the g_glew* macros onto
 * them.
 */
#include <vitaGL.h>
#include "vita_gl_shim.h"
#include "vita_fps.h"
#else
#include <glad.h>
#endif

#include <SDL.h>
#if defined(MUGEN_VITA_GL) && defined(MUGEN_SDL2)
/* Port change: native vitaGL owns its display, SDL2 must use ordinary GXM. */
#if SDL_MAJOR_VERSION != 2 || !SDL_VIDEO_RENDER_VITA_GXM || \
    SDL_VIDEO_VITA_VGL || SDL_VIDEO_VITA_PIB || SDL_VIDEO_VITA_PVR
#error "Vita requires ordinary VitaSDK sdl2 (GXM), not sdl2_vitagl/PIB/PVR"
#endif
#endif
#include "engine_keys.h"
#ifdef ANDROID_PORT
#include "android_platform.h"
#endif
#include <unzip.h>
#include <png.h>

/*
 * "Compiled against SDL2" as opposed to SDL 1.2. The Android port was the
 * first SDL2 consumer, so its #ifdef predates this macro; the SDL2 desktop
 * build (MUGEN_SDL2, Makefile SDL_BACKEND=2) needs exactly the same API
 * adaptations, so the two are named once here and the SDL2-semantics sites
 * test this instead of ANDROID_PORT directly. Android-only behaviour (its
 * lifecycle events, its no-GL renderer) keeps testing ANDROID_PORT.
 */
#if defined(MUGEN_SDL2) || defined(ANDROID_PORT)
#define MUGEN_SDL2_API 1
#endif

/*
 * Does the input layer bind pads through SDL's *game-controller* semantics
 * instead of translating raw axis/hat/button indices?
 *
 * Yes on every SDL2 target. The raw model derives an engine code from the
 * device's own topology (axis n -> n*2+1/+2, hat -> axes*2+1..4, button b ->
 * axes*2+hats*4+1+b), which only lines up with the numbers in `[P<n> Joystick]`
 * on a pad whose topology matches the PC pad those numbers were written for. A
 * modern controller does not: with 8 axes and no hats its face buttons start at
 * code 17 while the config's A/B/X/Y (13..16) are axis codes there, and its
 * d-pad arrives as buttons past the 32 codes one pad slot holds -- so the pad
 * opens, the stick steers, and the face buttons and d-pad are wrong or dead.
 * That is exactly the "buttons swapped with the d-pad" report.
 *
 * SDL_GameController maps a device through SDL's own database, so A/B/X/Y, the
 * d-pad, the shoulders and Start mean the same thing everywhere. `[P<n> Gamepad]`
 * configures that layout, and a missing group falls back to the built-in one.
 *
 * Gated on MUGEN_SDL2_API rather than ANDROID_PORT: this was Android-only while
 * Android was the only SDL2 consumer, which left linux-arm64 and the SDL2
 * Windows builds on the raw path. The SDL 1.2 backend is the reference build
 * for the differential tests and keeps the original's raw-joystick lifecycle,
 * so it stays out.
 */
#if defined(MUGEN_SDL2_API)
#define MUGEN_GAMEPAD_POLICY 1
#endif

/*
 * Does this build's SDL_mixer decode WAV, MP3 and Ogg Vorbis by itself?
 *
 * Both of this project's SDL_mixer link lines pull the codecs in statically --
 * `pkg-config --libs --static SDL_mixer` and `... SDL2_mixer` each list
 * -lmpg123 -lvorbisfile -lFLAC -lopus -lwavpack -- so neither needs Elecbyte's
 * libmpg123-0.dll or vorbisplug.dll to play those files. The sdlmix plugin
 * advertises the three extensions when this is set (bgmplugins.c), which is
 * what lets a static build run with no plugin DLLs at all.
 *
 * Android has always done this; the desktop backends now do too. A build that
 * linked a decoder-less SDL_mixer (a distro package compiled without
 * --enable-music-mp3) can leave it undefined and fall back to the plugin-DLL
 * route, at the cost of needing those DLLs present.
 */
#ifndef MUGEN_NO_SDLMIX_NATIVE_CODECS
#define MUGEN_SDLMIX_NATIVE_CODECS 1
#endif

#ifdef MUGEN_SDL2
/*
 * The removed SDL 1.2 entry points, reimplemented over SDL2 (src/sdl2compat.c).
 */
#include "sdl2compat.h"

#ifndef SDL_putenv
/*
 * SDL 1.2 had SDL_putenv(const char *"NAME=value"); SDL2 replaced it with
 * SDL_setenv(name, value, overwrite). The three reconstructed call sites pass
 * complete "NAME=value" strings, so the split happens here. A change to SDL's
 * own environment matters here in one real case: [Video] DirectX sets
 * SDL_VIDEODRIVER before SDL_Init, which is what still selects the driver.
 * (SDL2 chooses its video driver through SDL_VIDEODRIVER read at init; the
 * SDL_VIDEO_CENTERED hint is superseded by SDL_WINDOWPOS_CENTERED, which the
 * shim already uses.)
 */
static inline int Sdl2_putenv_inline(const char *kv)
{
    char name[64];
    const char *eq;
    size_t n;

    if (kv == NULL) {
        return -1;
    }
    eq = strchr(kv, '=');
    if (eq == NULL) {
        return SDL_setenv(kv, "", 1);
    }
    n = (size_t)(eq - kv);
    if (n >= sizeof(name)) {
        n = sizeof(name) - 1;
    }
    memcpy(name, kv, n);
    name[n] = '\0';
    return SDL_setenv(name, eq + 1, 1);
}
#define SDL_putenv Sdl2_putenv_inline
#endif
#endif

#include "crt.h"
#include "ftol.h"

/* Version reported by the original build. */
#define MUGEN_VERSION_MAJOR 1
#define MUGEN_VERSION_MINOR 1
#define MUGEN_VERSION_PATCH 0
#define MUGEN_VERSION_TAG   "Beta 1 P1"
#define MUGEN_BUILD_YEAR    2013
#define MUGEN_BUILD_MONTH   8
#define MUGEN_BUILD_DAY     11

/*
 * Checks that a decompiled struct matches the layout of the original.
 *
 * These are fidelity checks for the 32-bit layout: they pin the exact sizes and
 * offsets of the original mugen.exe, which is only meaningful while pointers
 * are 4 bytes. On a 64-bit build pointers double and 105 of the 200 asserted
 * structs necessarily move, so the checks are compiled out rather than relaxed
 * one by one -- they are assertions about the original binary, not about the
 * program's correctness. Nothing in src/ reads a struct by absolute offset, so
 * the relayout is functionally safe; see PLANS.md.
 *
 * MUGEN_LAYOUT_32BIT is 1 on every 32-bit target, so the reference builds keep
 * every check.
 */
/*
 * PORTABILITY FIX: this used __SIZEOF_POINTER__, which is a GCC predefined
 * macro. MSVC does not define it, so on MSVC x86 the reference checks below
 * were compiled out even though pointers are 4 bytes there. UINTPTR_MAX comes
 * from <stdint.h> on every compiler and means the same thing.
 */
#if UINTPTR_MAX == 0xFFFFFFFFu
#define MUGEN_LAYOUT_32BIT 1
#else
#define MUGEN_LAYOUT_32BIT 0
#endif

/* A 32-bit Windows build is the reference: it must keep the checks. */
#if defined(_WIN32) && !defined(_WIN64) && !MUGEN_LAYOUT_32BIT
#error "32-bit Windows build without MUGEN_LAYOUT_32BIT: pointer width not detected"
#endif

#if MUGEN_LAYOUT_32BIT
#define MUGEN_ASSERT_SIZE(type, size) \
    _Static_assert(sizeof(type) == (size), "sizeof(" #type ") != " #size)
#define MUGEN_ASSERT_OFFSET(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), "offsetof(" #type ", " #field ") != " #offset)
#else
/*
 * Still a file-scope declaration, and still references the type and the field,
 * so a typo or an incomplete type is caught. Only the original's size/offset is
 * no longer required, because it describes a 32-bit binary.
 */
#define MUGEN_ASSERT_SIZE(type, size) \
    _Static_assert(sizeof(type) > 0, #type " must be a complete type")
#define MUGEN_ASSERT_OFFSET(type, field, offset) \
    _Static_assert(offsetof(type, field) < sizeof(type), #type "." #field " must exist")
#endif

#include "util.h"
#include "stream.h"
#include "ini.h"
#include "palette.h"
#include "keyedarray.h"
#include "hashtable.h"
#include "luautil.h"
#include "ai.h"
#include "sprite.h"
#include "sff.h"
#include "textring.h"
#include "log.h"
#include "gfx.h"
#include "glrender.h"
#include "video.h"
#include "events.h"
#include "sound.h"
#include "bgm.h"
#include "keybind.h"
#include "font.h"
#include "syselem.h"
#include "fight.h"
#include "clsn.h"
#include "anim.h"
#include "bg.h"
#include "stage.h"
#include "expr.h"
#include "trigger.h"
#include "select.h"
#include "title.h"
#include "motif.h"
#include "storyboard.h"
#include "game.h"
#include "console.h"
#include "char.h"
#include "player.h"
#include "cnsparse.h"
#include "statedef.h"
#include "playerstate.h"
#include "sctrl.h"
#include "result.h"
#include "pngio.h"
#include "spritecodec.h"

#include "symbols.h"
#ifdef MUGEN_VITA_GL
#include "vita_video.h"
#endif

#endif /* MUGEN_H */
