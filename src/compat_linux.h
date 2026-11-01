/*
 * Minimal Win32/POSIX compatibility layer for the Linux build.
 *
 * The engine was reconstructed from a 32-bit Windows PE, so it uses MSVC
 * keywords, Windows type names and a handful of Win32 calls. Rather than edit
 * every source file, this header supplies the names that have a faithful POSIX
 * equivalent, and src/compat_linux.c implements the calls that really do
 * something.
 *
 * Nothing here changes behaviour on Windows: the whole file is compiled out
 * when _WIN32 is defined (see mugen.h), so the MSVC and MinGW builds compile
 * the original code path unchanged.
 *
 * Deliberately *not* emulated here: window styles, fullscreen window poking,
 * the RichEdit error dialog and the window icon. Those are Windows-specific by
 * nature, so the few files that use them carry an explicit
 *
 *     #ifdef _WIN32  ... original  #else  ... Linux  #endif
 *
 * instead of pretending a POSIX equivalent exists. See PLANS.md, "Linux port".
 */
#ifndef MUGEN_COMPAT_LINUX_H
#define MUGEN_COMPAT_LINUX_H

/*
 * Used by the non-Windows targets: Linux (glibc, native) and the PS Vita
 * (newlib, cross-compiled with VitaSDK). __vita__ is predefined by the VitaSDK
 * toolchain's target headers and SDL_config_vita.h; the Vita is newlib, not
 * glibc, so anything glibc-specific below is kept behind a check.
 */
#ifndef _WIN32

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------- MSVC keywords */

/*
 * __int64: MinGW defines this in _mingw.h, glibc does not (36 uses in src/).
 * __cdecl: a calling-convention attribute. Linux GCC rejects it on every
 *          architecture -- verified on x86-64, and it does not exist on arm64
 *          either -- unlike MinGW where it is a real keyword. System V
 *          AMD64 and AArch64 have a single integer calling convention, so it
 *          is correctly a no-op rather than merely tolerated.
 */
/*
 * __int64 must be exactly the type int64_t is, because symbols.h declares some
 * functions with int64_t while src/ defines them with __int64, and a mismatch
 * is a type conflict. On LP64 int64_t is `long`, on 32-bit it is
 * `long long`, so a fixed spelling would be wrong on one of them. GCC's
 * __INT64_TYPE__ is the right spelling for the current target.
 * "unsigned __int64" then expands to the matching unsigned type.
 */
#define __int64   __INT64_TYPE__
#define __cdecl
#define __stdcall
#ifndef __fastcall
/*
 * newlib (VitaSDK) already defines __fastcall in <sys/cdefs.h>, which arrives
 * through <math.h> below. Only that one: __int64, __cdecl and __stdcall are not
 * newlib's, so they are defined above for every non-Windows target. Redefining
 * __fastcall would be a "redefined" warning on every file.
 */
#define __fastcall
#endif

/* ------------------------------------------------------------ Windows types */

typedef int             BOOL;
typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;
typedef unsigned int    UINT;
typedef long            LONG;
typedef int             LRESULT;
typedef void *          LPVOID;
typedef void *          HANDLE;
typedef char *          LPSTR;
typedef const char *    LPCSTR;
typedef uintptr_t       WPARAM;
typedef intptr_t        LPARAM;
typedef intptr_t        INT_PTR;
typedef intptr_t        LONG_PTR;

typedef void *          HMODULE;
typedef void *          HINSTANCE;
typedef void *          HICON;
typedef void *          HWND;
typedef void *          HMENU;

#define TRUE  1
#define FALSE 0

#define WINAPI
#define APIENTRY
#define CALLBACK

/* Windows caps paths at 260 characters. The engine sizes two path buffers with
 * it (src/glrender.c, src/sdlmain.c), so the same value keeps those buffers the
 * same size as the original. */
#define MAX_PATH 260

/*
 * __declspec(noreturn) -> the GNU attribute. Only noreturn is used in src/
 * (4 sites); the storage-class spellings (dllimport/dllexport) belong to the
 * Windows DLL builds and never appear here.
 */
#define __declspec(x) __attribute__((x))

/*
 * LARGE_INTEGER wraps the 64-bit performance counter. src/timer.c and
 * src/matchload.c read .QuadPart and subtract, so only that member is needed.
 */
typedef union LARGE_INTEGER {
    int64_t QuadPart;
} LARGE_INTEGER;

/*
 * RECT is the only Win32 struct the sources declare a variable of (the saved
 * window rectangles in src/video.c).
 */
typedef struct RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

/* ------------------------------------------------------------ CRT renames */

/*
 * The engine uses the MSVC spellings. Macros, not wrappers, so the call sites
 * keep their exact original text.
 */

#define _stricmp    strcasecmp
#define _strnicmp   strncasecmp

/*
 * _snprintf/_vsnprintf are the pre-C99 MSVC versions, which do not guarantee
 * NUL termination. The reconstruction relies on writing into zero-filled
 * buffers, so snprintf/vsnprintf give the same practical result with the
 * guarantee added. Noted in PLANS.md.
 */
#define _snprintf  snprintf
#define _vsnprintf vsnprintf

#define _alloca    alloca
#define _mkdir(p)  mkdir((p), 0777)

/* The MSVC "secure/deprecated" aliases for plain POSIX calls. */
#define _getcwd    getcwd
#define _chdir     chdir

/* _strlwr: absent from glibc (strcasecmp compares, it does not convert).
 * compat_linux.c provides it. */
char *_strlwr(char *s);

/* --------------------------------- Win32 calls implemented in compat_linux.c */

/*
 * Library loading: LoadLibraryA / GetProcAddress / FreeLibrary.
 *
 * On Linux they are dlopen / dlsym / dlclose. The PS Vita has no dynamic
 * loader for homebrew (SDL_config_vita.h sets SDL_LOADSO_DISABLED), but the
 * engine only ever opens BGM plugin DLLs through these three, and every call
 * site already treats a NULL handle as "plugin not available" -- which is the
 * truth on the Vita. compat_linux.c therefore implements them as "always
 * fail" there, rather than pulling in dlopen.
 */
HMODULE LoadLibraryA(LPCSTR name);
void *  GetProcAddress(HMODULE module, LPCSTR name);
BOOL    FreeLibrary(HMODULE module);

/*
 * Timers over clock_gettime(CLOCK_MONOTONIC). timeGetTime keeps its millisecond
 * unit, and QueryPerformanceCounter/Frequency keep their 64-bit counter
 * contract, because src/timer.c and src/matchload.c do arithmetic on those
 * values and the original's log output depends on the units.
 *
 * newlib on the Vita provides clock_gettime(CLOCK_MONOTONIC) and nanosleep
 * (SDL_config_vita.h sets HAVE_NANOSLEEP), so the same code serves both.
 */
DWORD   timeGetTime(void);
BOOL    QueryPerformanceCounter(int64_t *count);
BOOL    QueryPerformanceFrequency(int64_t *freq);
UINT    timeBeginPeriod(UINT ms);
UINT    timeEndPeriod(UINT ms);

/* Sleep: milliseconds, as on Windows. Implemented with nanosleep. */
void    Sleep(DWORD ms);

/*
 * InterlockedExchangeAdd: atomically add and return the previous value, as the
 * Win32 intrinsic of that name. The BGM thread uses it on its position
 * counters. src/compat_linux.c maps it to GCC's __sync_fetch_and_add.
 */
LONG    Compat_InterlockedExchangeAdd(volatile LONG *addend, LONG value);
#define InterlockedExchangeAdd(a, v) Compat_InterlockedExchangeAdd((a), (v))

/* _isnan: MSVC spelling of isnan(). */
#define _isnan(x) isnan(x)

/* The executable's own module handle; on ELF this is RTLD_DEFAULT. */
HMODULE GetModuleHandleA(LPCSTR name);

/* The font directory, the POSIX stand-in for SHGetFolderPathA(CSIDL_FONTS).
 * Returns a static string; NULL if none of the usual directories exists. */
const char *Compat_FontsDir(void);

/*
 * The two-letter language code, the POSIX stand-in for
 * GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, ...), which
 * src/config.c uses when mugen.cfg has no [Config] language. Reads LANG then
 * LC_ALL, takes the part before '_' or '.', and returns FALSE if neither yields
 * a code so the caller keeps its "en" fallback.
 */
BOOL Compat_GetSystemLanguage(char *out, size_t size);

#endif /* !_WIN32 */

#endif /* MUGEN_COMPAT_LINUX_H */
