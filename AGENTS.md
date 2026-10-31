# AGENTS

Working notes for anyone (human or agent) building, testing or changing
mugen-unleashed. Read this first, then the doc it points at for the topic.

## The one rule that overrides everything

**Keep temporary files inside this project.** Write scratch files, logs, build
output and test artefacts under this repository — `build/` (ignored by git) or a
new ignored directory — and nowhere else. Do not write to `$HOME`, `/tmp`,
`%TEMP%`, `C:/Users/...`, or any path outside the project tree. If a tool
insists on a system temp directory, point it back inside the project
(`TMPDIR=./build/tmp`, `-B build/tmp`, …). When you are done, delete what you
created.

Corollaries learned the hard way:

- MSYS bash rewrites `/tmp/x` to `C:/Users/<user>/AppData/Local/Temp/x` when it
  hands a path to a native Windows binary. That silently puts files outside the
  project. Use project-relative paths.
- Native Windows tools do not inherit the MSYS working directory. Use absolute
  paths or `cd` inside the same command.
- **One sanctioned exception**: CBM keeps its own config and index under the
  user profile. That is the tool's state, not ours, and it is not ours to move
  — see [Exploring the code with CBM](#exploring-the-code-with-cbm). Everything
  *we* create still goes in the project.

## What this is

A C reconstruction of the M.U.G.E.N 1.1 Beta 1 engine, rebuilt from the original
`mugen.exe` (32-bit PE, MSVC 2008). 78 modules in `src/`, one per module of the
original. See `docs/ANALYSIS.md` for the binary analysis and the module map.

**Fidelity is the product.** The goal is the original's behaviour *including its
bugs*. Do not "fix" quirks: they are documented in each function's comment
("Quirks kept from the original"). Fixes and new features must be marked as such
in the code.

## Layout

```
src/            78 .c + 47 .h — the engine, one file per original module
  mugen.h       common header; type headers describe the original's structs
  symbols.h     every prototype and global, with the original's addresses
  globals.c     the globals, with their initial values from the original .data
  sdlmain.c     SDL 1.2's SDLmain (WinMain), rebuilt
  mugen.rc      error dialog (200), version info, optional icon
third_party/    Lua 5.1.4, libpng 1.2.38, glad 2.1, minizip 0.15, sdk, libresample
cmake/          build helpers (MugenSdk.cmake, ThirdParty.cmake)
tools/          msvc_env.bat, extract_icon.py, compare_logs.py
docs/           ANALYSIS.md, BUILD-MSVC.md, BUILD-MINGW.md, BUILD-LINUX.md, LUA.md
install/        a real M.U.G.E.N 1.1b1 installation (untracked, for testing)
build/          all build output (git-ignored)
```

## Building

The target is **32-bit x86 only**. Every struct in `src/` has the original's
field offsets, checked at compile time with `MUGEN_ASSERT_SIZE`. A 64-bit build
is not a supported configuration and is rejected.

Output name is `mugen_unleashed.exe` in both build systems.

### MSVC 2022 (primary)

```bat
tools\msvc_env.bat cmake --preset x86-release
tools\msvc_env.bat cmake --build --preset x86-release
```

→ `build\x86-release\mugen_unleashed.exe` + `.pdb`

Debug variant: same with `x86-debug`. `tools\msvc_env.bat` finds VS 2022 through
`vswhere`, so any edition or install path works. Never call `cmake` or `cl`
directly without it — the environment will be missing.

Details: `docs/BUILD-MSVC.md`.

### Linux (same Makefile, auto-detected)

The `Makefile` also builds Linux targets, chosen from `uname` + the compiler's
triple. See `docs/BUILD-LINUX.md` for the dependency list.

```sh
make -j8              # build/linux-x64/mugen-linux-x64
make install
```

`mugen.log` from the Linux build is identical to the Windows build's, so the
same validation method works. Windows stays the reference build; the Linux
target is additive and must not disturb it.

### GNU Make + MinGW (Windows, fully static)

```sh
export PATH=/c/x86devkit/bin:$PATH      # the 32-bit kit, not w64devkit
make check-toolchain
make -j8
make install                            # strips and copies into install/
```

→ `build/mingw/mugen-static.exe`, and `install/mugen-static.exe` after
`make install`.

This build links **everything statically** (SDL 1.2, SDL_gfx, SDL_mixer,
SDL_ttf, zlib, pthread, mpg123 from the kit; Lua, libpng, glad, minizip,
libresample from source). The result needs no `Elecbyte.MUGEN.libs` — only
Windows system DLLs and `msvcrt.dll`. The CMake/MSVC build still links the
game's DLLs; the two are not the same link step.

`make check-toolchain` refuses a non-`i686-w64-mingw32` compiler and checks the
static archives exist (not just `.pc` files). `make DEBUG=1
BUILD=build/mingw-debug` for an unoptimised build. Details:
`docs/BUILD-MINGW.md`.

Do not assume the shell's default `gcc` is the 32-bit one: this environment has
both `w64devkit` (x86_64) and `x86devkit` (i686) on `PATH`, and the x64 kit
compiles `-m32` but cannot link.

## Exploring the code with CBM

This repository is indexed in **codebase-memory-mcp** (CBM), a code knowledge
graph. Prefer it over `grep`/`find` for structural questions — where something is
defined, who calls it, what a file contains. It returns precise results in a
fraction of the tokens that grepping a 78-module tree costs.

Project name: **`C-Projects-mugen-unleashed`**. Always pass it as `--project`.

### Invoking it

When the MCP tools are available in your session, call them directly
(`search_graph`, `trace_path`, `get_code_snippet`, …). Otherwise drive the CLI:

```sh
CBM="C:/Users/dhani/AppData/Local/Programs/codebase-memory-mcp/codebase-memory-mcp.exe"
"$CBM" cli <tool> --project C-Projects-mugen-unleashed [flags]
```

Add `--json` for the raw envelope, omit it for the human summary. The server
resolves its config and index databases through `HOME`; **if `HOME` is not your
user profile (this environment has exported it as a project directory before),
every call fails with `could not load saved runtime settings`** — the server
exits while loading settings, before it even parses the tool name. Fix with
`export HOME="$USERPROFILE"`, or per call: `env -u HOME "$CBM" cli …`.

### The queries worth knowing

| Question | Command |
|---|---|
| Where is a symbol defined? | `search_graph --name-pattern '^Pal_Convert$'` |
| What is in this file? | `get_file_outline --file-path src/luautil.c` |
| Who calls this? | `trace_path --function-name Pal_Convert --direction inbound` |
| What does this call? | `trace_path --function-name Lua_Init --direction outbound` |
| Read one function | `get_code_snippet --qualified-name C-Projects-mugen-unleashed.src.palette.Pal_Convert` |
| Text search | `search_graph --query "palette conversion rgb565"` |
| Is the index stale/partial? | `index_status` / `check_index_coverage` |

`--name-pattern` takes a regex anchored how you write it; `^Name$` finds an exact
symbol. `get_code_snippet` needs the **fully qualified** name
(`C-Projects-mugen-unleashed.src.<file>.<Function>`); get it from
`search_graph`/`get_file_outline` first.

### Traps, all of them observed here

- **`--file-pattern` does not filter `search_graph`.** Passed `src/` and still got
  `third_party/` results. Use `--name-pattern`/`--qn-pattern` for narrowing, or
  `get_file_outline` when you know the file.
- **`full` mode indexes all of `third_party/`** (Lua, libpng, glad, minizip),
  which outweighs `src/` in the 21k nodes. Unfiltered searches get buried in
  vendored code — a search for "lua state" returned only `third_party/lua-5.1.4`
  hits. Qualify symbols, or filter after the fact.
- **Vector/semantic search is broken in this build.** `--semantic-query` returns
  nonsense (`Makefile.clean` for "sprite blend render target") and
  `CBM_SEMANTIC_ENABLED=1` changes nothing. Use `--query` (BM25), which is
  precise. The *stored* `SEMANTICALLY_RELATED` / `SIMILAR_TO` edges are good and
  can be walked with `query_graph`.
- **The graph is not a source of truth.** Some files are `parse_partial`; check
  `check_index_coverage` for the paths you relied on and read any flagged ranges.
  As of the last index, `src/symbols.h` parses, `src/luautil.c` is partial
  (lines 83, 91). Arguments for array-valued flags such as `--paths` must be
  JSON via `--args-file` or stdin, not repeated flags.
- **The index does not auto-update.** CLI sessions do not watch the tree. After
  editing sources, re-run `index_repository --repo-path <project> --mode full`
  or your queries describe the old code. That is the worst failure mode here:
  stale results that look authoritative.

Config and index live outside this repository, in the CBM cache under the user
profile — that is the one sanctioned exception to the temp-file rule, because it
is the tool's own state, not ours.

## Testing

The project has no unit tests. Behaviour is validated by **running both binaries
on the same scenario and diffing `mugen.log`** (`docs/ANALYSIS.md`,
"Validation").

`install/` holds a real M.U.G.E.N 1.1b1 tree, so tests can be run end to end.
`make install` puts the static executable there for you:

```sh
make install
cd install
./mugen-static.exe -nosound          # startup/mode select
```

For the MSVC build, copy `build/x86-release/mugen_unleashed.exe` into `install/`
by hand; it needs the `Elecbyte.MUGEN.libs` folder, the static one does not.

Copy the original `mugen.exe` log out of the way first, then compare:

```sh
python ../tools/compare_logs.py <original.log> <rebuilt.log>
```

Exit 0 means the logs match after numeric normalisation (timings, RNG seed,
palette ids); exit 1 lists the differing fragments. A difference is either a
regression or a deliberate change — decide which and say so.

Scenarios worth running (all with `-nosound` for reproducibility):

| Scenario | Command |
|---|---|
| startup → mode select | `./mugen-static.exe -nosound` (or `mugen-linux-x64` under Linux) |
| quick VS, one character per side | `-nosound -p1 kfm -p2 kfm` |
| AI vs AI, two rounds | `-nosound -rounds 2 -p1.ai 1 -p2.ai 1` |

Known test-environment quirks:

- Both binaries run until closed; use a timeout or the `-rounds` flag, otherwise
  the run has to be killed (exit 124).
- `-log <file>` produces no file in either binary — a faithful quirk, not a bug.
  The OpenGL draw statistics from `Gl_Deinit` therefore never print.
- The console (`stats`, `lexec`, `lmode`, and the rest) is key-binding driven and
  cannot be exercised headlessly.
- `mugen.log` is written to the working directory. Delete it between runs or you
  will compare stale logs.
- **Give both binaries the same timeout.** The original runs until closed; if
  one side is killed earlier its log is simply shorter, and
  `compare_logs.py` reports that as hundreds of differences. Every "mismatch"
  chased during this project that came from a short timeout looked exactly like
  a regression. Use a self-terminating scenario (`-p1 kfm -p2 kfm`) where you
  can.

Clean up afterwards: remove the copied executable and any logs from `install/`.
That directory is the pristine game tree; leave it as you found it.

## Changing code

1. Read the module's header comment first. It states what the module is, the
   original's address range, and which quirks are preserved.
2. Keep the address comments (`/* 0x0042E390: ... */`) and the
   `/* 0x... */` prototype comments in `symbols.h` in sync with any change.
3. Do not add new dependencies. The dependency set is fixed by what the original
   binary imported (see `docs/ANALYSIS.md`). One deliberate exception is
   recorded in `PLANS.md`: GLEW 1.5.4 was replaced by glad 2.1 as the OpenGL
   loader, which is smaller and generates exactly what the engine needs.
4. Never put `<SDL.h>` or `<GL/gl.h>` above `#include "mugen.h"` in a source
   file: glad must see the GL headers first or it refuses to load.
   `<GL/glu.h>` goes after `mugen.h`.
5. Prefer the existing helpers: `Ftol`/`Trunc64` for float→int (`src/ftol.h`),
   `Mem_Calloc`/`Mem_Malloc` for allocation, `Log_Printf`/`MatchLog_Printf` for
   output.
6. Build with all three toolchains if you touched anything shared; MSVC is the
   reference.
7. If you change behaviour on purpose, add a comment saying so. If you fix an
   original bug, mark it as a fix — silently diverging is the one thing this
   project does not accept.

### Style

Match the surrounding code. It is C11 with MSVC as the baseline: no GCC-only
extensions, declarations at the top of a block, `__cdecl` on prototypes, explicit
comparisons against `NULL`/`0`. Warnings are `/W3` (MSVC) and `-Wall` (GCC) with
`-Wno-unused-parameter`, because reconstructed functions keep every original
parameter.

### Float precision

The engine's maths reproduces x87 semantics: intermediates stay `double`, values
stored to memory are `float`, and rounding happens on assignment. Both builds
pin this (`/arch:IA32`, `-mfpmath=387 -fexcess-precision=standard`). Do not
"simplify" an expression into a different order of operations, and do not let a
compiler rearrange it.

## Notes on this file

Verify claims before writing them here. Anything in AGENTS.md that is not true of
the tree is worse than nothing.
