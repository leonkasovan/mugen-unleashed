/*
 * newlib heap size for the PS Vita port.
 *
 * VitaSDK's crt0 sizes the newlib heap from `_newlib_heap_size_user`, a *weak*
 * symbol. Its logic (disassembled from _init_vita_heap in the linked image) is:
 *
 *     mov.w  r2, #134217728        @ 0x8000000 -- the default, 128 MiB
 *     movw   r3, #_newlib_heap_size_user
 *     cbz    r3, 1f                @ symbol undefined (weak -> 0)? keep the default
 *     ldr    r2, [r3]              @ otherwise take the program's value
 *   1: str    r2, [_newlib_heap_size]
 *     blx    sceKernelAllocMemBlock
 *
 * and if `sceKernelAllocMemBlock` fails it zeroes `_newlib_heap_memblock`,
 * `_newlib_heap_base` and `_newlib_heap_size`, leaving a heap of size 0.
 *
 * Defining the symbol here, rather than leaving it undefined, is what makes the
 * heap fit: the 128 MiB default is larger than the whole process memory budget
 * a SELF may request. vita-make-fself's `-m` accepts at most 0x12800 KiB
 * (74 MiB; see MUGEN_VITA_MEMSIZE in CMakeLists.txt), so the default
 * allocation always fails and every malloc returns NULL.
 *
 * That failure is worth spelling out because of how it presents. The first
 * allocation to fail is not in the engine: libvitaGL.a contains C++ (its GLSL
 * expression parser), and its static constructor runs from __libc_init_array
 * *before main*. `operator new` throws std::bad_alloc, nothing catches it,
 * std::terminate runs __verbose_terminate_handler and aborts. The console
 * therefore shows C2-12828-1 with `PC` inside newlib's `_kill_r`, no mugen.log
 * is written at all, and the crash appears unrelated to memory.
 *
 * MUGEN_VITA_HEAP_BYTES is supplied by CMake so the heap and the SELF's budget
 * cannot drift apart; CMake also refuses to configure if the heap would not fit.
 */
#include <stddef.h>

#ifndef MUGEN_VITA_HEAP_BYTES
#error "MUGEN_VITA_HEAP_BYTES must be defined by port/psvita/CMakeLists.txt"
#endif

/*
 * The heap the engine's malloc/new draws from. Non-weak, so it overrides the
 * weak reference in VitaSDK's crt0.
 */
unsigned int _newlib_heap_size_user = MUGEN_VITA_HEAP_BYTES;
