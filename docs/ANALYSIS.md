# The original `mugen.exe`

M.U.G.E.N 1.1.0 Beta 1 P1 (Elecbyte, built on 2013-08-11).

This document describes the original binary and how the code in `src/` reconstructs it:
- the module map;
- the peculiarities of the code MSVC 2008 generated, and how they are reproduced;
- how the result was checked against the original.

## Contents

| | |
|---|---|
| Functions of the game code reconstructed | all of them: 1479, `Hash_Lookup3` from lookup3 included |
| Ghidra functions in the game range (0x401000–0x49C3BF) | 1491; 1478 are in `src/`, the other 13 (~3.4 KB) are minizip, built from its source |
| Global variables | 336, with the initial values of the original's `.data` / `.rdata` (`src/globals.c`) |
| Structs whose layout is checked (`MUGEN_ASSERT_SIZE`) | 203 |
| Code | ~69k lines in 73 modules |

Every struct field the game uses has a name. The names are:
- for fields the code reads, the name says what they are;
- `unused_XXX` for fields the original only writes;
- `pad_XXX` for fields it never touches.

`XXX` is the field's offset.

## The binary

| Field | Value |
|---|---|
| Format | PE32, i386, GUI subsystem |
| Timestamp | 2013-08-11 15:23:01 UTC |
| Linker | MSVC 9.0 (Visual Studio 2008 SP1, build 30729) |
| Image base | 0x00400000 (no ASLR) |
| Entry point | 0x004DC80A, the CRT's `__tmainCRTStartup` |
| Debug information | none |
| Packer / protection | none |
| C runtime | `MSVCR90.dll` (VC90.CRT 9.0.21022.8 manifest) |
| Version | FILEVERSION 1.1.0.650, "1.1.0 Beta 1 P1" |

| Section | Address | Virtual size | Contents |
|---|---|---|---|
| `.text`  | 0x401000 | 0xDC8FA (~882 KB) | code |
| `.rdata` | 0x4DE000 | 0x2176C (~134 KB) | strings, import table, tables |
| `.data`  | 0x500000 | 0x5700 (0x1000 in the file) | globals; the rest is BSS |
| `.rsrc`  | 0x506000 | 0xB428 | 9 icons (group 101), dialog 200, version, manifest |

The Rich header lists 67 C objects, 109 C objects built with whole-program optimisation
(LTCG, /GL), 2 C++ objects (CRT stubs) and 8 assembly objects (CRT). The game is plain C.
LTCG inlined functions across files and changed the calling convention of many internal
functions (see [Peculiarities](#peculiarities-of-the-binary)).

## Dependencies

Linked dynamically, from the private assembly `Elecbyte.MUGEN.libs` that the manifest names:

| DLL | Use |
|---|---|
| SDL.dll (1.2) | window, events, keyboard, joysticks, audio |
| SDL_mixer.dll | sound effects, MIDI / tracker music |
| SDL_ttf.dll | TrueType fonts |
| SDL_gfx.dll | `zoomSurface` |
| zlib1.dll (1.2.3) | inflate / deflate (SFF v2, PNG) |
| libresample.dll | audio resampling |
| pthreadVC2.dll | the BGM thread |
| OPENGL32 / GLU32 | OpenGL renderer |
| libmpg123-0.dll | loaded with LoadLibrary by the built-in "mpg123" BGM plugin |

Linked statically (built from source in `third_party/`, not reconstructed):

| Library | Version | In this project |
|---|---|---|
| Lua | 5.1.4 | `third_party/lua-5.1.4` |
| libpng | 1.2.38 | `third_party/libpng-1.2.38` |
| GLEW | 1.5.4 | `third_party/glew-1.5.4` |
| minizip (unzip) | 0.15 | `third_party/minizip-0.15` (characters in .zip files) |
| lookup3 (Bob Jenkins) | 2006 | `src/lookup3.c`: `hashlittle` with the initial value 0x1337B47E folded in |
| SDLmain | 1.2.15 | `src/sdlmain.c`, rebuilt from the binary |
| CRT start-up | VC9 | the compiler's static CRT |

BGM plugins (`plugins/*.dll`) export one function, `getBGMSubsystem`. The built-in ones,
"mpg123" and "sdlmix", are in `src/bgmplugins.c`.

## Layout of `.text`

| Range | Contents |
|---|---|
| 0x401000–0x49C3BF | game code (with minizip and lookup3) |
| 0x49C3C0–0x4B66F3 | libpng 1.2.38 |
| 0x4B66F4–0x4B68E5 | import thunks (SDL, SDL_mixer, SDL_ttf, zlib) |
| 0x4B68F0–0x4C4DCF | GLEW 1.5.4 |
| 0x4C4DE0–0x4DC549 | Lua 5.1.4 |
| 0x4DC54A–0x4DD8FF | CRT start-up, SDLmain (0x4DCF80–0x4DD700), `__alldiv`, `__ftol2_sse`... |

## Module map

There is one `.c` file per module of the original, in link order (the order of the string
literals in `.rdata` and of the code in `.text`). The ranges are those of the original modules.

| File | Original range | Contents |
|---|---|---|
| `afterimage.c` | 0x401000–0x40168F | afterimages: ring buffer of frames with draw parameters and PalFX |
| `expr.c` | 0x401690–0x40369F | CNS expressions: parser, compiled expressions, evaluator |
| `select.c` | 0x4036A0–0x40B06F | select screen: `select.def`, `[Select Info]` layout, cursors, team menus |
| `cmdline.c` | 0x40B070–0x40B9C9 | command line |
| `error.c` | 0x40BAA0–0x40C7CA | errors, character warnings, debug clipboards, the error dialog |
| `file.c` | 0x40C1D0–0x40C3E8 | file lookup |
| `log.c` | 0x40C3F0…, 0x46C270… | `mugen.log` and the match log |
| `gameloop.c` | 0x40C7D0–0x40EBBF | the game loop and the common screen loop |
| `continue.c` | 0x40EBC0–0x40F53F | continue screen |
| `frame.c` | 0x40F540–0x40FB4F | coordinates, end of frame, safe mode, screenshots |
| `explod.c` | 0x40FB50–0x41249F | explods and the system element draw list |
| `gameflow.c` | 0x4124A0–0x412EBF | the game flow and coordinate conversions |
| `camera.c` | 0x415A10–0x416E9F | the camera |
| `round.c` | 0x4124A0–0x419A4F | rounds: fight tick, round state machine, combos |
| `match.c` | 0x4124A0–0x419A4F | fight HUD: bars, faces, names, timer, win icons |
| `helper.c` | 0x419A50–0x41A4F7 | `Helper` and `DestroySelf` |
| `system.c` | 0x41A500–0x41E46F | start-up and shutdown, `fight.def`, window focus |
| `keybind.c` | 0x41E480–0x41E875 | console key bindings |
| `luabind.c` | 0x41E880–0x4214FF | Lua player bindings |
| `main.c` | 0x421500–0x421A3F | `SDL_main` |
| `dbgcmd.c` | 0x421A40–0x421E7F | debug commands shared by the console and Lua |
| `matchload.c` | 0x421E80–0x4259EF | loading, starting and ending a match; stages; the match RNG |
| `console.c` | 0x4259F0–0x42873F | debug console |
| `storyboard.c` | 0x428740… | storyboards |
| `title.c` | 0x428AF0–0x42E2EF | title, options, key configuration, training menu |
| `luaapi.c` | 0x42E2F0–0x42EF4F | the Lua engine and the `mugen` library |
| `ai.c` | 0x42EF50–0x433DFF | computer players: learned data (`ElecbyteAIB`), move recording, decisions |
| `playerrel.c` | 0x433E00–0x434B1B | relations between players |
| `char.c` | 0x434B20–0x43BF3F | characters: cache, loading, players |
| `playerstate.c` | 0x43BF40–0x44444F | player runtime: states, animation, life / power, hits |
| `result.c` | 0x444450–0x444A9F | survival results |
| `sctrl.c` | 0x444AA0–0x45380F | state controllers |
| `trigger.c` | 0x453810–0x458C2F | triggers |
| `cnsparse.c` | 0x458C30–0x45B2CF | CNS parsing helpers |
| `statedef.c` | 0x45B210–0x45C4CF | `[Statedef]` and the state machine |
| `motif.c` | 0x45C4D0–0x45DE7F | the motif (`system.def`) |
| `config.c` | 0x45DE80–0x45FC7F | `mugen.cfg` |
| `versus.c` | 0x45FC80–0x4612DF | versus and victory screens |
| `util.c` | 0x4612E0–0x4618B5 | string, path and math helpers |
| `palette.c` | 0x4618C0–0x4627D3 | palettes, remapping, PalFX |
| `video.c` | 0x4627E0–0x4635EF | SDL video mode, window, draw parameters |
| `anim.c` | 0x4635F0–0x46509B | AIR animations and the sprite queue |
| `sprite.c` | 0x4650A0–0x4661DE | sprites and sprite sets |
| `textring.c` | 0x4661E0… | text line ring buffer |
| `keyedarray.c` | 0x466550–0x466A21 | keyed arrays |
| `stream.c` | 0x466A30–0x467500 | file, memory and zip streams |
| `sff.c` | 0x467510–0x469A45 | SFF v1.01, SFF v2.x, PCX |
| `ini.c` | 0x469A50–0x46B20D | the text parser of `.def/.cfg/.cns/.air` files |
| `events.c` | 0x46B210–0x46B79F | SDL events and input state |
| `gfx.c` | 0x46B7A0–0x46C26F | graphics core: fades, render targets, renderer tables |
| `font.c` | 0x46C3E0–0x46E22F | FNT v1 / v2 and TrueType fonts |
| `keyboard.c` | 0x46E290–0x46E42F | keyboard state and key queue |
| `sound.c` | 0x46E430–0x46FB42 | sound effects |
| `bgm.c` | 0x46FB50–0x470C9F | background music and its plugins |
| `bg.c` | 0x470CA0–0x4745EF | backgrounds, parallax, background controllers |
| `pads.c` | 0x4745F0–0x474CCF | joysticks |
| `timer.c` | 0x476C90–0x47713F | timer and frame pacing |
| `pngio.c` | 0x477140–0x477ABF | PNG screenshots and SFF v2 PNG sprites |
| `playerinput.c` | 0x477AC0–0x477EB6 | player input buffer |
| `misc.c` | 0x477EC0–0x478040 | decoded sprite cache glue |
| `luautil.c` | 0x478050–0x478606 | Lua helpers |
| `cmdinput.c` | 0x478610–0x478CBA | command recognition (`.cmd`) |
| `hashtable.c` | 0x478CC0–0x479573 | hash tables and console command tables |
| `lang.c` | 0x479580 | language code |
| `clsn.c` | 0x4795C0–0x479C9F | collision boxes |
| `glrender.c` | 0x479CA0–0x47CC0F | OpenGL renderer and GLSL shaders |
| `softrender.c` | 0x47CC10–0x484BB0 | software sprite rasterizer (RGB565) |
| — | 0x4849B0–0x485770 | minizip (built from source) |
| `prim.c` | 0x485780–0x4886DF | drawing primitives |
| `wav.c` | 0x4886E0–0x48944F | WAV reader and resampler |
| `bgmthread.c` | 0x489450–0x48AB5F | BGM thread |
| `bgmplugins.c` | 0x48AB60–0x48C3DF | built-in BGM plugins (mpg123, sdlmix) |
| `spritecodec.c` | 0x48C3E0–0x48D9AF | SFF v2 codecs (RLE8, RLE5, LZ5) and the decompression buffer |
| `lookup3.c` | 0x48D9B0 | lookup3 `hashlittle` |
| `gldetect.c` | 0x48DED0–0x48EE6F | OpenGL offset detection |
| `softblend.c` | 0x48EE70–0x49AA4F | software sprites with additive blending |
| `bgmstream.c` | 0x49AA50–0x49C3BF | audio format converters and the BGM stream |
| `sdlmain.c` | 0x4DCF80–0x4DD700 | SDLmain |

The types are in the headers that `src/mugen.h` includes, one per subject. The main structs are:
- `GameVars` (0x13108 bytes, `g_game`);
- `Player` (0x2490);
- `CharData` (0x8F8);
- `Stage` (0x5C0);
- `CmdLineArgs` (0x640).

## Peculiarities of the binary

The goal is the original's behaviour, bugs included. The quirks that are kept are listed in the
comments of each function ("Quirks kept from the original").

- **x87 floating point.** The original runs the x87 FPU at 53-bit precision. The code is built
  with `/arch:IA32` and follows where the original keeps values:
  - what stays on the FPU stack is a `double`;
  - what is stored to memory is a `float`.

  VS2022 drops `(float)` casts of intermediate values, so the roundings are done with
  assignments.
- **Float to int.** The original converts in two ways, both in `src/ftol.h`:
  - `__ftol2_sse` (cvttsd2si): truncates and gives 0x80000000 for NaN and out-of-range
    values. This is `Ftol`.
  - In a few places (software sprite scaling, parallax, the resampler), an inlined 64-bit
    `fistp` of which only the low 32 bits are used. This is `Trunc64`.
- **LTCG.** Many internal functions of the original take arguments in registers or are
  specialised for a single caller. The reconstruction uses plain C calling conventions; the
  address comments point to the original functions.
- **Initialised globals.** 54 globals have non-zero initial values in the original's `.data`
  / `.rdata`: renderer function tables, the BGM plugin tables, trigger and key name tables,
  shaders, error messages. `src/globals.c` has them. GLEW's function pointers are GLEW's own
  variables.
- **Resources.** `src/mugen.rc` rebuilds the original's error dialog (200) and its version
  information. The window icon (101) is taken from the original executable at build time when
  it is available.

## Validation

The code was checked against the original binary in two ways:
- **Function by function.** The reconstructed code was loaded into the original process and
  each function was compared with its original over the game's files and generated inputs
  (about 174 million checks).
- **Whole program.** The rebuilt executable and the original wrote the same `mugen.log` on
  every scenario listed in the README.
