/*
 * Win32 calls the engine makes, implemented over POSIX for the Linux build.
 *
 * Only calls with a faithful equivalent live here. Window handling, the error
 * dialog and the window icon are Windows-specific by nature and are handled
 * with #ifdef at their call sites (see src/compat_linux.h).
 *
 * Compiled to an empty object on Windows; the Makefile/CMake only build it for
 * the Linux targets.
 */
#include "mugen.h"

#ifndef _WIN32

#include <dlfcn.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------------- misc */

/*
 * strlwr: MSVC lowercases in place. glibc has no equivalent (strcasecmp is a
 * comparison, not a conversion), so provide it. ASCII only, like the original.
 */
char *_strlwr(char *s)
{
    char *p;

    if (s == NULL) {
        return NULL;
    }
    for (p = s; *p != '\0'; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
    return s;
}

/* ------------------------------------------------------------ library loading */

/*
 * LoadLibraryA / GetProcAddress / FreeLibrary map to dlopen / dlsym / dlclose.
 *
 * One difference worth knowing: Windows appends ".dll" to a bare name and
 * searches the executable's directory first; dlopen searches the library path
 * and needs the real filename. The BGM plugins are named with an explicit
 * ".dll" in the config (bgmplugins.c builds "libmpg123-0.dll"), so the call
 * sites pass a full name and are unaffected. A plugin that only exists as a
 * Windows DLL cannot be loaded on Linux at all -- the built-in mpg123 and
 * SDL_mixer subsystems are the ones that work.
 */
HMODULE LoadLibraryA(LPCSTR name)
{
    if (name == NULL) {
        return NULL;
    }
    return (HMODULE)dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

void *GetProcAddress(HMODULE module, LPCSTR name)
{
    if (module == NULL || name == NULL) {
        return NULL;
    }
    return dlsym((void *)module, name);
}

BOOL FreeLibrary(HMODULE module)
{
    if (module == NULL) {
        return FALSE;
    }
    return dlclose((void *)module) == 0;
}

/*
 * GetModuleHandleA(NULL) asks for the running executable. ELF has no handle
 * for that; RTLD_DEFAULT is the closest thing and is what dlsym would want
 * anyway. The engine only uses the result to find its own resources (icon,
 * dialog template), which the Linux build does not have.
 */
HMODULE GetModuleHandleA(LPCSTR name)
{
    (void)name;
    return (HMODULE)RTLD_DEFAULT;
}

/* --------------------------------------------------------------------- timers */

/*
 * Windows' timeGetTime counts milliseconds from boot and wraps at 32 bits.
 * clock_gettime(CLOCK_MONOTONIC) has the same monotonic property, so the
 * subtraction-based arithmetic in src/timer.c and src/matchload.c behaves the
 * same, including the wrap.
 */
DWORD timeGetTime(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)((unsigned long long)ts.tv_sec * 1000ULL
                   + (unsigned long long)(ts.tv_nsec / 1000000));
}

/*
 * The performance counter. The original's frequency is 10,000,000 (100 ns
 * units) and it logs that number ("frequency 10000000" in mugen.log), so the
 * same units are used here: clock_gettime gives nanoseconds, divided by 100.
 * Keeping the original's frequency means the timer arithmetic in src/timer.c
 * and src/matchload.c sees the same magnitudes it was written for, and the log
 * line matches.
 */
#define COMPAT_QPC_HZ 10000000LL

BOOL QueryPerformanceCounter(int64_t *count)
{
    struct timespec ts;

    if (count == NULL) {
        return FALSE;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *count = ((int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec) / 100;
    return TRUE;
}

BOOL QueryPerformanceFrequency(int64_t *freq)
{
    if (freq == NULL) {
        return FALSE;
    }
    *freq = COMPAT_QPC_HZ;
    return TRUE;
}

/*
 * InterlockedExchangeAdd: GCC's __sync builtins give the same atomic
 * fetch-and-add with the same "return the old value" contract. The BGM thread
 * only needs the atomicity, not a particular memory order beyond it.
 */
LONG Compat_InterlockedExchangeAdd(volatile LONG *addend, LONG value)
{
    return __sync_fetch_and_add(addend, value);
}

/*
 * Sleep waits the given number of milliseconds. nanosleep is interruptible by a
 * signal, so it resumes for the remainder rather than returning early: the
 * engine uses Sleep to pace frames and would run too fast otherwise.
 */
void Sleep(DWORD ms)
{
    struct timespec req;
    struct timespec rem;

    req.tv_sec = (time_t)(ms / 1000);
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &rem) == -1) {
        req = rem;
    }
}

/* timeBeginPeriod sets the Windows scheduler's timer resolution. There is no
 * equivalent to set; both calls report success so callers see no change. */
UINT timeBeginPeriod(UINT ms)
{
    (void)ms;
    return 0;
}

UINT timeEndPeriod(UINT ms)
{
    (void)ms;
    return 0;
}

/* -------------------------------------------------------------------- folders */

/*
 * SHGetFolderPathA(CSIDL_FONTS) on Windows returns the system font directory.
 * Freedesktop systems keep fonts in the XDG data dirs; return the first that
 * exists so the font search in src/system.c has somewhere real to look.
 */
const char *Compat_FontsDir(void)
{
    static const char *dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        NULL
    };
    int i;

    for (i = 0; dirs[i] != NULL; i++) {
        struct stat st;

        if (stat(dirs[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            return dirs[i];
        }
    }
    return NULL;
}

/*
 * The language code the engine asks Windows for when mugen.cfg has no
 * [Config] language. Reads LANG, then LC_ALL; takes the part before the first
 * '_' or '.', so "en_GB.UTF-8" yields "en". Returns FALSE when neither variable
 * names a language, which leaves the caller's "en" fallback in place.
 */
BOOL Compat_GetSystemLanguage(char *out, size_t size)
{
    const char *vars[] = { "LANG", "LC_ALL", NULL };
    int i;

    if (out == NULL || size < 2) {
        return FALSE;
    }
    for (i = 0; vars[i] != NULL; i++) {
        const char *v = getenv(vars[i]);
        size_t n = 0;

        if (v == NULL || v[0] == '\0') {
            continue;
        }
        /* "C" and "POSIX" are not languages. */
        if (strcmp(v, "C") == 0 || strcmp(v, "POSIX") == 0) {
            continue;
        }
        while (v[n] != '\0' && v[n] != '_' && v[n] != '.' && n + 1 < size) {
            out[n] = v[n];
            n++;
        }
        out[n] = '\0';
        if (n >= 2) {
            return TRUE;
        }
    }
    return FALSE;
}

#endif /* !_WIN32 */
