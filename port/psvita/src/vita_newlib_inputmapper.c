/*
 * newlib heap size for sdl-input-mapper.
 *
 * VitaSDK's crt0 sizes the newlib heap from the weak symbol
 * `_newlib_heap_size_user` and falls back to 128 MiB when it is undefined.
 * mugen-unleashed's Vita port learned that the hard way: 128 MiB exceeds what a
 * SELF may be given, `sceKernelAllocMemBlock` fails, crt0 zeroes
 * `_newlib_heap_size`, and every allocation from then on returns NULL -- which
 * surfaced as an abort inside a C++ static constructor before main, with no log.
 *
 * This tool allocates almost nothing, but it should not rely on the console
 * tolerating the default either. 24 MiB is plenty for SDL plus a screen-sized
 * surface and leaves the rest of the process budget to the video driver.
 */
#define INPUT_MAPPER_HEAP_BYTES (24u * 1024u * 1024u)

unsigned int _newlib_heap_size_user = INPUT_MAPPER_HEAP_BYTES;
