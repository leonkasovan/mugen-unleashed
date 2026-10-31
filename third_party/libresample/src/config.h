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
 * Linux, where <inttypes.h> and <stdint.h> are both C99. Regenerate with
 * ./configure if libresample is ever taken from upstream again.
 */
#ifndef LIBRESAMPLE_CONFIG_H
#define LIBRESAMPLE_CONFIG_H

#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H   1
#define HAVE_STDLIB_H   1
#define HAVE_STRING_H   1
#define HAVE_MATH_H     1

#endif /* LIBRESAMPLE_CONFIG_H */
