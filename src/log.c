/*
 * Status log (mugen.log) and match log (original modules around 0x40C3F0 and
 * 0x46C270).
 */
#include "mugen.h"
#include <time.h>

/* Number of mugen.log lines kept in memory for the error dialog. */
#define LOG_MIRROR_LINES 64

/*
 * Port addition: the local wall clock, for [Debug] TimeStamps. Written as a
 * helper rather than a bare localtime() call because this file is also built by
 * MSVC, which has localtime_s, and because a single static buffer is not safe
 * once the BGM thread logs.
 */
static int Log_LocalTime(struct tm *out)
{
    time_t now = time(NULL);

#if defined(_WIN32)
    return localtime_s(out, &now) == 0;
#else
    return localtime_r(&now, out) != NULL;
#endif
}

/*
 * Port addition: writes buf to the log with the local time in front of every
 * line, for [Debug] TimeStamps = 1.
 *
 * One prefix per line within the message, so a multi-line dump stays readable
 * and each line can be matched against what the engine was doing. A blank line
 * stays blank -- a prefix on its own would be noise. The text after the prefix
 * is byte-for-byte what the unstamped path writes.
 *
 * Note that the engine logs several startup steps as fragments that end
 * without a newline ("Initializing timer...", "performance timer enabled..."),
 * so those calls each get their own prefix mid-line. That is deliberate: those
 * fragments are the steps whose timing a report needs.
 */
static void Log_WriteStamped(FILE *fp, const char *buf)
{
    const char *p = buf;

    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) + 1 : strlen(p);

        if (!(len == 1 && p[0] == '\n')) {
            struct tm tmv;
            char stamp[16];

            if (Log_LocalTime(&tmv) &&
                strftime(stamp, sizeof(stamp), "[%H:%M:%S] ", &tmv) > 0) {
                fputs(stamp, fp);
            }
        }
        fwrite(p, 1, len, fp);
        p += len;
    }
}

/* 0x0040C3F0 */
int Log_Init(void)
{
    FILE *fp;
    Log *log;
    TextRing *mirror;

    fp = fopen("mugen.log", "wt");
#ifdef MUGEN_VITA_GL
    /*
     * PS Vita port (port/psvita): the launch directory is not always writable
     * -- a LiveArea start can leave the process in a read-only mount -- and
     * when it is not, this fopen failed, Log_Init returned 0, every subsequent
     * Log_Printf became a no-op and the port's only diagnostic vanished. The
     * engine ignores Log_Init's return value (as the original does), so nothing
     * reported it. ux0:data is always writable for a homebrew.
     *
     * The handle is kept, so Log_Close closes it like any other.
     */
    if (fp == NULL) {
        fp = fopen("ux0:data/mugen.log", "wt");
    }
#endif
    if (fp == NULL) {
        return 0;
    }
    log = Mem_Calloc(1, sizeof(Log));
    if (log == NULL) {
        fclose(fp);
    } else {
        log->level = 9;
        log->fp = fp;
        log->autoflush = 1;
        log->owns_fp = 1;
    }
    g_log = log;
    if (log == NULL) {
        return 0;
    }
    mirror = TextRing_Create(LOG_LINE_SIZE, LOG_MIRROR_LINES);
    if (mirror == NULL) {
        return 0;
    }
    Log_SetMirror(mirror);
    Log_Printf("---------------------------------------------------\n"
               "M.U.G.E.N ver %i.%i.%i %s (%i.%02i.%02i) status log\n"
               "---------------------------------------------------\n",
               MUGEN_VERSION_MAJOR, MUGEN_VERSION_MINOR, MUGEN_VERSION_PATCH, MUGEN_VERSION_TAG,
               MUGEN_BUILD_YEAR, MUGEN_BUILD_MONTH, MUGEN_BUILD_DAY);
    if (g_logTimeStamps != 0) {
        /* Port addition: the date, which the per-line prefix does not carry. */
        struct tm tmv;

        if (Log_LocalTime(&tmv)) {
            Log_Printf("started %04d-%02d-%02d %02d:%02d:%02d\n",
                       tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                       tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        }
    }
    return 1;
}

/* 0x0040C4A0 */
void Log_Printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    Log_VWrite(g_log, ap, 1, fmt);
    va_end(ap);
}

/* 0x0046C270 */
void Log_Close(void)
{
    Log *log = g_log;

    if (log == NULL) {
        return;
    }
    if (log->fp != NULL) {
        if (log->mirror != NULL && log->owns_mirror) {
            TextRing_Free(log->mirror);
            log->mirror = NULL;
            log->owns_mirror = 0;
        }
        fflush(log->fp);
        if (log->owns_fp) {
            fclose(log->fp);
        }
    }
    free(log);
}

/* 0x0046C2E0: the log takes ownership of the ring. */
void Log_SetMirror(TextRing *ring)
{
    Log *log = g_log;

    if (log == NULL) {
        return;
    }
    if (log->mirror != NULL && log->owns_mirror) {
        TextRing_Free(log->mirror);
    }
    log->mirror = ring;
    log->owns_mirror = 1;
}

/* 0x0046C320 */
void Log_VWrite(Log *log, va_list ap, int level, const char *fmt)
{
    char buf[LOG_LINE_SIZE];

    if (log == NULL || level > log->level || log->fp == NULL) {
        return;
    }
    _vsnprintf(buf, LOG_LINE_SIZE - 1, fmt, ap);
    buf[LOG_LINE_SIZE - 1] = '\0';
    if (g_logTimeStamps != 0) {
        Log_WriteStamped(log->fp, buf);
    } else {
        fprintf(log->fp, "%s", buf);
    }
    if (log->autoflush) {
        fflush(log->fp);
    }
    if (log->mirror != NULL) {
        TextRing_Append(log->mirror, buf);
    }
}

/* 0x0046C3C0: writes to the match log given with -log, if any. */
void MatchLog_Printf(int level, const char *fmt, ...)
{
    va_list ap;

    if (g_matchLog == NULL) {
        return;
    }
    va_start(ap, fmt);
    Log_VWrite(g_matchLog, ap, level, fmt);
    va_end(ap);
}
