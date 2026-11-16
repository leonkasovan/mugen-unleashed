/* File-level probe: use the production updater, without starting the engine. */
#include "mugen.h"
#include "selectupdate.h"
#include <errno.h>
#include <stdarg.h>

void Log_Printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap);
}

#ifdef SELECTUPDATE_FAULTS
#include <fcntl.h>
#include <unistd.h>
static const char *failure, *target;
static int fired, allocation, fail_allocation;
void *__real_malloc(size_t n);
void *__real_realloc(void *p, size_t n);
int __real_rename(const char *from, const char *to);
size_t __real_fwrite(const void *p, size_t size, size_t n, FILE *fp);
int __real_fflush(FILE *fp);
int __real_fclose(FILE *fp);
void *__wrap_malloc(size_t n)
{
    if (++allocation == fail_allocation) { errno = ENOMEM; return NULL; }
    return __real_malloc(n);
}
void *__wrap_realloc(void *p, size_t n)
{
    if (++allocation == fail_allocation) { errno = ENOMEM; return NULL; }
    return __real_realloc(p, n);
}
int __wrap_rename(const char *from, const char *to)
{
    if (failure != NULL && strcmp(failure, "rename") == 0 && strcmp(to, target) == 0) {
        errno = EIO; return -1;
    }
    return __real_rename(from, to);
}
size_t __wrap_fwrite(const void *p, size_t size, size_t n, FILE *fp)
{
    if (!fired && failure != NULL && strcmp(failure, "write") == 0) {
        fired = 1; errno = ENOSPC; return 0;
    }
    return __real_fwrite(p, size, n, fp);
}
int __wrap_fflush(FILE *fp)
{
    if (!fired && failure != NULL && strcmp(failure, "flush") == 0) {
        fired = 1; errno = ENOSPC; return EOF;
    }
    return __real_fflush(fp);
}
int __wrap_fclose(FILE *fp)
{
    int writable = (fcntl(fileno(fp), F_GETFL) & O_ACCMODE) != O_RDONLY;
    int result = __real_fclose(fp);
    if (writable && !fired && failure != NULL && strcmp(failure, "close") == 0) {
        fired = 1; errno = EIO; return EOF;
    }
    return result;
}
#endif

int main(int argc, char **argv)
{
    int characters, stages, result;
    if (argc == 2 && strcmp(argv[1], "--features") == 0) {
#ifdef SELECTUPDATE_FAULTS
        puts("faults");
#endif
        return 0;
    }
    if (argc < 4) return 2;
    characters = atoi(argv[2]); stages = atoi(argv[3]);
    if (argc > 4 && strcmp(argv[4], "default") == 0) {
        SelectUpdate_Auto(argv[1]); return 0;
    }
#ifdef SELECTUPDATE_FAULTS
    if (argc > 4) { failure = argv[4]; fail_allocation = atoi(failure); target = argv[1]; }
#endif
    if (argc > 4 && strcmp(argv[4], "auto") == 0) {
        SelectUpdate_Configure(characters, stages);
        SelectUpdate_Auto(argv[1]); return 0;
    }
    if (characters && !stages) result = Select_UpdateCharInSelectDef(argv[1]);
    else if (stages && !characters) result = Select_UpdateStageInSelectDef(argv[1]);
    else result = Select_UpdateSelectDef(argv[1], characters, stages);
    printf("result:%d\n", result);
    return result < 0;
}
