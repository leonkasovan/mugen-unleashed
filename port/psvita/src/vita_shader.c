/* Port fix: preserve the runtime compiler's native load error. vitaGL maps
 * compiler initialization failures to GL_INVALID_OPERATION, which cannot
 * distinguish a missing file from an invalid module or denied operation. */
#include "mugen.h"
#include <errno.h>

static int compiler_ready;

/* Diagnostic only: the native loader decides whether a module is usable.
 * An SCE header alone does not prove that a file has been decrypted. */
static void VitaShader_LogFile(const char *path)
{
    unsigned char header[16];
    unsigned int magic;
    size_t bytes;
    long size = -1;
    FILE *file;

    errno = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        Log_Printf("GL: compiler file %s: open failed (errno=%i)\n", path, errno);
        return;
    }
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    rewind(file);
    bytes = fread(header, 1, sizeof(header), file);
    Log_Printf("GL: compiler file %s: size=%ld, header bytes=%u, read error=%i\n",
               path, size, (unsigned)bytes, ferror(file) != 0);
    fclose(file);
    if (bytes < 4) return;
    magic = (unsigned int)header[0] | ((unsigned int)header[1] << 8) |
            ((unsigned int)header[2] << 16) | ((unsigned int)header[3] << 24);
    if (magic == 0x00454353 && bytes >= 12) {
        Log_Printf("GL: compiler SCE header: sdk_type=0x%04X, header_type=0x%04X\n",
                   (unsigned)(header[8] | (header[9] << 8)),
                   (unsigned)(header[10] | (header[11] << 8)));
    } else if (magic == 0x464c457f) {
        Log_Printf("GL: compiler file is raw ELF; use ShaRKF00D's generated .suprx, not its .elf\n");
    } else {
        Log_Printf("GL: compiler header magic=0x%08X\n", magic);
    }
}

int VitaGl_InitShaderCompiler(void)
{
    static const char *paths[2] = {
        "ur0:/data/libshacccg.suprx", "ur0:/data/external/libshacccg.suprx"
    };
    int results[2];
    int i;

    Log_Printf("GL: compiler preflight entered (cached=%i)\n",compiler_ready);
    if (compiler_ready) return 1;
    if (!VitaGl_HasContext()) {
        Log_Printf("GL: runtime compiler requires an owned vitaGL context\n");
        SDL_SetError("Vita shader compiler requires an owned display");
        return 0;
    }
    /* Match vitaGL's start_shader_compiler allocator setup. shark_init is
     * idempotent; vitaGL can subsequently call it to set its own ready flag. */
    shark_set_allocators(vglMalloc, vglFree);
    for (i = 0; i < 2; i++) {
        VitaShader_LogFile(paths[i]);
        results[i] = shark_init(paths[i]);
        Log_Printf("GL: shark_init(%s) -> 0x%08X\n", paths[i], (unsigned)results[i]);
        if (results[i] >= 0) {
            compiler_ready = 1;
            Log_Printf("GL: runtime compiler initialization succeeded\n");
            return 1;
        }
    }
    /* Keep both results: an absent fallback must not hide the primary file's
     * actual module-loader failure. Do not unload somebody else's module. */
    SDL_SetError("Vita shader compiler load failed (primary 0x%08X, fallback 0x%08X)",
                 (unsigned)results[0], (unsigned)results[1]);
    Log_Printf("GL: runtime compiler unavailable (primary 0x%08X, fallback 0x%08X)\n",
               (unsigned)results[0], (unsigned)results[1]);
    return 0;
}
