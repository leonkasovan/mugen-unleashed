# Third-party code

Libraries the original `mugen.exe` linked statically, built from their original sources in the
same versions, and the headers and import definitions of the DLLs the game ships with. Each keeps
its own license; the license files and notices are in the directories and headers below.

| Directory | What | License |
|---|---|---|
| `lua-5.1.4/` | Lua 5.1.4 (lua.org) | MIT (`COPYRIGHT`) |
| `libpng-1.2.38/` | libpng 1.2.38 | libpng license (`LICENSE`) |
| `glew-1.5.4/` | GLEW 1.5.4 | Modified BSD / MIT (`LICENSE.txt`) |
| `minizip-0.15/` | unzip 0.15 by Gilles Vollant (zlib 1.1.3 contrib) | zlib license (`unzip.h`) |
| `sdk/include/SDL/` | SDL 1.2.15 and SDL_mixer 1.2.11 headers | GNU LGPL 2.1 (headers) |
| `sdk/include/SDL/SDL_ttf.h` | SDL_ttf 2.0.11 header | zlib license |
| `sdk/include/zlib.h`, `zconf.h` | zlib 1.2.3 headers | zlib license |
| `sdk/include/pthread.h`, `sched.h` | pthreads-win32 2.8.0 headers | GNU LGPL 2.1 |
| `sdk/include/libresample.h` | declarations for the game's `libresample.dll` (libresample 0.1.3), written for this project from the calls in `mugen.exe`. DLL build only; the static build uses `libresample/include` | — |
| `libresample/` | libresample 0.1.3 by Dominic Mazzoni, vendored and ported to multi-channel to match Elecbyte's ABI. See `libresample/VENDORED.md` | BSD / LGPL |
| `sdk/def/*.def` | export lists of the game's DLLs, from which the build makes import libraries | — |

The DLLs themselves are not included. At run time the executable uses the copies in the game's
`Elecbyte.MUGEN.libs` folder.
