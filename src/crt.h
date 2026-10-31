/*
 * The C runtime headers the engine uses.
 */
#ifndef MUGEN_CRT_H
#define MUGEN_CRT_H

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * <direct.h> is the MSVC header for _mkdir/_chdir. POSIX puts the same calls in
 * <sys/stat.h> and <unistd.h>; compat_linux.h maps the names.
 */
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#endif /* MUGEN_CRT_H */
