/* Real compiler preflight, with only file selection/context/vitaShaRK mocked.
 * This verifies diagnostics and ownership; it does not load a Vita module. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUGEN_H
static int context_ready = 1, primary_exists = 1, fallback_exists;
static int primary_result, fallback_result = (int32_t)0x80010002u;
static int loads, opens, allocator_sets, open_denied;
static char logs[8192], error_text[256];

static void Log_Printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    size_t used = strlen(logs);
    va_start(ap, fmt); n = vsnprintf(logs + used, sizeof(logs) - used, fmt, ap); va_end(ap);
    assert(n >= 0 && (size_t)n < sizeof(logs) - used);
}
static void SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); vsnprintf(error_text, sizeof(error_text), fmt, ap); va_end(ap);
}
static int VitaGl_HasContext(void) { return context_ready; }
static void *vglMalloc(size_t bytes) { return malloc(bytes); }
static void vglFree(void *p) { free(p); }
static void shark_set_allocators(void *(*alloc)(size_t), void (*release)(void *))
{
    assert(alloc == vglMalloc && release == vglFree); allocator_sets++;
}
static int shark_init(const char *path)
{
    assert(allocator_sets == 1); loads++;
    if (strcmp(path, "ur0:/data/libshacccg.suprx") == 0) {
        assert(loads == 1); return primary_result;
    }
    assert(strcmp(path, "ur0:/data/external/libshacccg.suprx") == 0 && loads == 2);
    return fallback_result;
}
static FILE *Test_Open(const char *path, const char *mode)
{
    int exists;
    assert(strcmp(mode, "rb") == 0); opens++;
    if (strcmp(path, "ur0:/data/libshacccg.suprx") == 0) exists = primary_exists;
    else {
        assert(strcmp(path, "ur0:/data/external/libshacccg.suprx") == 0);
        exists = fallback_exists;
    }
    if (open_denied || !exists) { errno = open_denied ? EACCES : ENOENT; return NULL; }
    return fopen("compiler-fixture.bin", mode);
}
#define fopen Test_Open
#include "../port/psvita/src/vita_shader.c"
#undef fopen

int main(int argc, char **argv)
{
    unsigned char header[16] = {'S','C','E',0,3,0,0,0,0xc0,0,1,0};
    size_t bytes = sizeof(header);
    int expected_ready = 1;
    FILE *f;
    assert(argc == 2);
    if (!strcmp(argv[1], "fallback")) {
        primary_exists = 0; primary_result = (int32_t)0x80010002u;
        fallback_exists = 1; fallback_result = 0;
    } else if (!strcmp(argv[1], "load-failure")) {
        primary_result = (int32_t)0x8002d003u; expected_ready = 0;
    } else if (!strcmp(argv[1], "raw-elf")) {
        header[0]=0x7f; header[1]='E'; header[2]='L'; header[3]='F';
        primary_result = (int32_t)0x8002d003u; expected_ready = 0;
    } else if (!strcmp(argv[1], "short-header")) {
        bytes = 3; primary_result = (int32_t)0x8002d003u; expected_ready = 0;
    } else if (!strcmp(argv[1], "open-denied")) {
        open_denied = 1; primary_result = (int32_t)0x8001000du; expected_ready = 0;
    } else if (!strcmp(argv[1], "no-context")) {
        context_ready = 0; expected_ready = 0;
    } else assert(!strcmp(argv[1], "success"));
    f = fopen("compiler-fixture.bin", "wb"); assert(f);
    assert(fwrite(header, 1, bytes, f) == bytes); assert(fclose(f) == 0);
    assert(VitaGl_InitShaderCompiler() == expected_ready);
    if (!context_ready) {
        assert(!loads && !opens && !allocator_sets && error_text[0]);
    } else if (expected_ready) {
        int before_loads=loads, before_opens=opens;
        assert(compiler_ready && strstr(logs, "sdk_type=0x00C0") && strstr(logs, "size=16"));
        assert(strstr(logs, "initialization succeeded") && !error_text[0]);
        assert(VitaGl_InitShaderCompiler() && loads == before_loads && opens == before_opens);
        assert(allocator_sets == 1);
        if (fallback_exists) {
            assert(loads == 2 && strstr(logs, "0x80010002"));
        } else assert(loads == 1);
    } else {
        assert(!compiler_ready && loads == 2 && opens == 2 && error_text[0]);
        assert(strstr(logs, "ur0:/data/libshacccg.suprx"));
        assert(strstr(logs, "ur0:/data/external/libshacccg.suprx"));
        assert(strstr(logs, "fallback 0x80010002") && strstr(error_text, "fallback 0x80010002"));
        if (open_denied) {
            char expected[32]; snprintf(expected, sizeof(expected), "errno=%i", EACCES);
            assert(strstr(logs, expected) && strstr(logs, "primary 0x8001000D"));
        } else assert(strstr(logs, "primary 0x8002D003") && strstr(error_text, "primary 0x8002D003"));
        if (bytes == 3) assert(strstr(logs, "size=3, header bytes=3"));
        if (header[0] == 0x7f) assert(strstr(logs, "raw ELF"));
    }
    assert(remove("compiler-fixture.bin") == 0);
    printf("ok: Vita compiler preflight %s\n", argv[1]);
    return 0;
}
