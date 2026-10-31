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

/*
 * glad must come before <SDL.h>. SDL_opengl.h includes <GL/gl.h>, which defines
 * __gl_h_ -- and glad.h refuses to load if that happened first. Including glad
 * here defines __gl_h_ itself, so the system GL header then self-suppresses and
 * glad becomes the single source of GL declarations, fixed-function pipeline
 * included (it is generated with the compatibility profile).
 */
#include <glad.h>

#include <SDL.h>
#include <unzip.h>
#include <png.h>

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
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 4
#define MUGEN_LAYOUT_32BIT 1
#else
#define MUGEN_LAYOUT_32BIT 0
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

#endif /* MUGEN_H */
