/*
 * Port addition: writes the embedded configuration defaults into the game tree.
 *
 * There is no original counterpart; the original binary always read whatever
 * data/mugen.cfg the tree carried. This project ships res/configs/ -- the base
 * file and one overlay per platform -- and compiles both into the executable,
 * so an install/ tree, an APK or a VPK needs no configuration copied into it by
 * hand.
 *
 * Only a missing file is written. An existing one is the player's: the options
 * screen rewrites [Options], the volumes and the key groups in place, so
 * replacing it would discard saved settings. Failure is not fatal either: the
 * engine then reports the same "Can't open config file" it always did.
 *
 * The bytes compiled in are written as-is; the generators normalise line
 * endings to LF, so every target extracts the same text.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "mugen.h"
#include "configembed.h"
#include "config_data.h"        /* generated: g_config_mugen_* */

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#if !defined(__vita__)
#include <sys/syscall.h>
#endif
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int ConfigEmbedded_WriteAndClose(FILE *file, const char *data, size_t size)
{
    size_t written = fwrite(data, 1, size, file);
    return fclose(file) == 0 && written == size;
}

static void ConfigEmbedded_ReportError(const char *path, int err)
{
    if (err == EEXIST) {
        Log_Printf("config: %s appeared while writing; kept it\n", path);
    } else {
        Log_Printf("config: could not create %s: %s\n", path, strerror(err));
    }
}

/* Port fix: Vita's link() is an ENOSYS stub. On Vita and on filesystems
 * without either atomic publication primitive, fall back to exclusive
 * creation. It reports failure rather than replacing anything that appeared
 * while this ran, and a process killed mid-write can leave a partial file
 * behind. */
#if !defined(_WIN32)
static int ConfigEmbedded_PublishExclusive(const char *path, const char *data, size_t size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    FILE *file;

    if (fd < 0) {
        ConfigEmbedded_ReportError(path, errno);
        return 0;
    }
    file = fdopen(fd, "wb");
    if (file == NULL) {
        close(fd);
        Log_Printf("config: could not open %s\n", path);
        return 0;
    }
    if (!ConfigEmbedded_WriteAndClose(file, data, size)) {
        Log_Printf("config: could not write %s\n", path);
        return 0;
    }
    return 1;
}
#endif

#if !defined(__vita__)
static int ConfigEmbedded_OpenTemp(const char *path, char *tmp, size_t capacity)
{
#if defined(_WIN32)
    unsigned int attempt;
    for (attempt = 0; attempt < 100; ++attempt) {
        int n = snprintf(tmp, capacity, "%s.%lu.%u.tmp", path,
                         (unsigned long)GetCurrentProcessId(), attempt);
        int fd;
        if (n < 0 || (size_t)n >= capacity) {
            errno = ENAMETOOLONG;
            return -1;
        }
        fd = _open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
        if (fd >= 0 || errno != EEXIST) {
            return fd;
        }
    }
    errno = EAGAIN;
    return -1;
#else
    int n = snprintf(tmp, capacity, "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return mkstemp(tmp);
#endif
}

static int ConfigEmbedded_Publish(const char *tmp, const char *path,
                                  const char *data, size_t size)
{
#if defined(_WIN32)
    if (MoveFileExA(tmp, path, 0) != 0) {
        return 1;
    }
    {
        unsigned long err = (unsigned long)GetLastError();
        if (err == (unsigned long)ERROR_ALREADY_EXISTS ||
            err == (unsigned long)ERROR_FILE_EXISTS) {
            ConfigEmbedded_ReportError(path, EEXIST);
        } else {
            Log_Printf("config: could not publish %s (Windows error %lu)\n",
                       path, err);
        }
    }
#else
    int err;
#if defined(SYS_renameat2)
    /* Linux RENAME_NOREPLACE (1) also works on filesystems without hard links. */
    if (syscall(SYS_renameat2, AT_FDCWD, tmp, AT_FDCWD, path, 1U) == 0) {
        return 1;
    }
    err = errno;
    /* EPERM as well as ENOSYS/EINVAL/EOPNOTSUPP: a sandbox may deny
     * renameat2 while link() still works. */
    if (err != ENOSYS && err != EINVAL && err != EOPNOTSUPP && err != EPERM) {
        ConfigEmbedded_ReportError(path, err);
        return 0;
    }
#endif
    if (link(tmp, path) == 0) {
        return 1;
    }
    err = errno;
    if (err == ENOSYS || err == EOPNOTSUPP || err == EPERM) {
        return ConfigEmbedded_PublishExclusive(path, data, size);
    }
    ConfigEmbedded_ReportError(path, err);
#endif
    return 0;
}
#endif

static void ConfigEmbedded_Write(const char *path, const char *data, size_t size)
{
    FILE *file = fopen(path, "rb");
#if !defined(__vita__)
    char tmp[512];
    int fd;
    int published;
#endif

    if (file != NULL) {
        fclose(file);
        return;
    }
#if defined(__vita__)
    if (!ConfigEmbedded_PublishExclusive(path, data, size)) {
        return;
    }
#else
    /* Write to a unique sibling before publishing without replacement. */
    fd = ConfigEmbedded_OpenTemp(path, tmp, sizeof(tmp));
    if (fd < 0) {
        ConfigEmbedded_ReportError(path, errno);
        return;
    }
#if defined(_WIN32)
    file = _fdopen(fd, "wb");
#else
    file = fdopen(fd, "wb");
#endif
    if (file == NULL) {
#if defined(_WIN32)
        _close(fd);
#else
        close(fd);
#endif
        remove(tmp);
        Log_Printf("config: could not open temporary file for %s\n", path);
        return;
    }
    if (!ConfigEmbedded_WriteAndClose(file, data, size)) {
        remove(tmp);
        Log_Printf("config: could not write %s\n", path);
        return;
    }
    published = ConfigEmbedded_Publish(tmp, path, data, size);
    remove(tmp);
    if (!published) {
        return;
    }
#endif
    Log_Printf("config: wrote %s from the embedded default\n", path);
}

void Config_Embedded_Extract(void)
{
    ConfigEmbedded_Write("data/mugen.cfg", g_config_mugen_cfg,
                         sizeof(g_config_mugen_cfg) - 1);
#ifdef CONFIG_PORT_EMBEDDED
#ifdef CONFIG_PORT_LEGACY
    /*
     * Port fix: an upgraded arm64 tree keeps the overlay it already had.
     * Writing the new default beside the old file would leave two overlays
     * with different values and no way for the player to tell which one the
     * engine reads. The old one is honoured (config.c merges it) and the new
     * name is left absent, which is exactly the pre-upgrade behaviour.
     */
    {
        FILE *legacy = fopen(CONFIG_PORT_LEGACY, "rb");
        if (legacy != NULL) {
            fclose(legacy);
            Log_Printf("config: keeping %s (the previous name of %s)\n",
                       CONFIG_PORT_LEGACY, CONFIG_PORT_PATH);
            return;
        }
    }
#endif
    ConfigEmbedded_Write(CONFIG_PORT_PATH, CONFIG_PORT_EMBEDDED,
                         sizeof(CONFIG_PORT_EMBEDDED) - 1);
#endif
}
