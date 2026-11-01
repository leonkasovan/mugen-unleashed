/*
 * config.h for the vendored libresample, standing in for the file upstream's
 * configure generates.
 *
 * upstream resample_defs.h does:
 *
 *     #if !defined(WIN32) && !defined(__CYGWIN__)
 *     #include "config.h"
 *     #endif
 *
 * so the Windows builds never need this file (MinGW defines WIN32), while the
 * Linux build requires one to exist. The only symbol resample_defs.h reads from
 * it is HAVE_INTTYPES_H; everything else it needs comes from the standard
 * headers.
 *
 * Values describe the platforms this project builds for -- glibc and musl on
 * Linux, where <inttypes.h> and <stdint.h> are both C99, and newlib on the PS
 * Vita (port/psvita), which is C99 too. Regenerate with ./configure if
 * libresample is ever taken from upstream again.
 */
#ifndef LIBRESAMPLE_CONFIG_H
#define LIBRESAMPLE_CONFIG_H

/*
 * PS Vita (newlib): resample_defs.h's HAVE_INTTYPES_H branch typedefs its own
 * BOOL and WORD. newlib's <sys/types.h> already defines both (BOOL as an enum,
 * WORD as short), pulled in through <stdio.h>, so the two collide. Defining
 * resample's names away is the smallest faithful fix: the library uses BOOL
 * only as a truth value and WORD only as an unsigned sample index, both of
 * which the newlib typedefs cover.
 *
 * Keyed on __vita__, which is the macro GCC itself defines for the Vita target.
 * __VITA__ (capitalised) only appears once an SDL header has been included, so
 * it is not reliable in a header the library sources include first.
 *
 * Kept here rather than in port/psvita because this header exists precisely to
 * describe the target's C library, and the library sources must not learn about
 * the port.
 */
#ifdef __vita__
#define LIBRESAMPLE_NEWLIB_TYPES
#endif

#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H   1
#define HAVE_STDLIB_H   1
#define HAVE_STRING_H   1
#define HAVE_MATH_H     1

#endif /* LIBRESAMPLE_CONFIG_H */
