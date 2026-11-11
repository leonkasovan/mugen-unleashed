# Third-party code

Vendored engine dependencies and reference ABI headers. Each keeps its own
license; notices are in the directories and headers listed below. SDL2,
SDL2_mixer and SDL2_ttf come from the selected platform dependency tree.

| Directory | What | License |
|---|---|---|
| `lua-5.1.4/` | Lua 5.1.4 (lua.org) | MIT (`COPYRIGHT`) |
| `libpng-1.2.38/` | libpng 1.2.38 | libpng license (`LICENSE`) |
| `glad-2.1/` | **glad 2.1** OpenGL loader, generated for this engine (GL 2.1 **compatibility** profile + `ARB_fragment_program`, `ARB_texture_non_power_of_two`, `EXT_blend_logic_op`, `EXT_framebuffer_object`) | MIT; `khrplatform.h` carries the Khronos MIT text |
| `glew-1.5.4/` | GLEW 1.5.4 — **no longer built.** The original linked it statically; the reconstruction now uses glad. Kept for reference and for the `.data` addresses recorded in `src/symbols.h` | Modified BSD / MIT (`LICENSE.txt`) |
| `minizip-0.15/` | unzip 0.15 by Gilles Vollant (zlib 1.1.3 contrib) | zlib license (`unzip.h`) |
| `cgltf-1.14/` | cgltf 1.14 by Johannes Kuhlmann, the glTF reader of 3D stages (a mugen-unleashed addition, not in the original) | MIT (`LICENSE`) |
| `stb/` | stb_image 2.30 by Sean Barrett (nothings/stb commit 013ac3b), the JPEG decoder of 3D stage textures (a mugen-unleashed addition) | public domain or MIT (end of `stb_image.h`) |
| `sdk/include/zlib.h`, `zconf.h` | zlib 1.2.3 headers | zlib license |
| `sdk/include/pthread.h`, `sched.h` | pthreads-win32 2.8.0 headers | GNU LGPL 2.1 |
| `sdk/include/libresample.h` | declarations for the game's `libresample.dll` (libresample 0.1.3), written for this project from the calls in `mugen.exe`. historical ABI reference only; builds use `libresample/include` | — |
| `libresample/` | libresample 0.1.3 by Dominic Mazzoni, vendored and ported to multi-channel to match Elecbyte's ABI. See `libresample/VENDORED.md` | BSD / LGPL |


SDL1 SDK headers and game-DLL import definitions are removed. Static Windows
builds do not need `Elecbyte.MUGEN.libs`. The remaining `sdk/include` contains
independent pthread/zlib declarations used by the 32-bit build and legacy
libresample ABI documentation. It is not an SDL SDK or an MSVC build path.
