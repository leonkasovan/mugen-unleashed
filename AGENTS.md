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
- **Two sanctioned exceptions**, both because the thing is not our artefact and
  cannot work anywhere else:
  - CBM keeps its own config and index under the user profile. That is the
    tool's state, not ours, and it is not ours to move — see
    [Exploring the code with CBM](#exploring-the-code-with-cbm).
  - VitaSDK lives at `C:/vitasdk` (or `VITASDK_DIR=`), installed there by
    `port/psvita/build.sh`. A toolchain has to sit at the path the environment
    variables name and other Vita tools expect; its 600 MB of unpacked SDK does
    not belong in a git-ignored build tree. What the script *downloads* still
    goes to `build/downloads/`, and its staging still happens in `build/tmp/`.

Everything *we* create still goes in the project.

## What this is

A C reconstruction of the M.U.G.E.N 1.1 Beta 1 engine, rebuilt from the original
`mugen.exe` (32-bit PE, MSVC 2008). 83 modules in `src/`, one per module of the
original. See `docs/ANALYSIS.md` for the binary analysis and the module map.

**Fidelity is the product.** The goal is the original's behaviour *including its
bugs*. Do not "fix" quirks: they are documented in each function's comment
("Quirks kept from the original"). Fixes and new features must be marked as such
in the code.

## Layout

```
src/            83 .c + 52 .h — the engine, one file per original module
  mugen.h       common header; type headers describe the original's structs
  symbols.h     every prototype and global, with the original's addresses
  globals.c     the globals, with their initial values from the original .data
  sdlmain.c     SDL 1.2's SDLmain (WinMain), rebuilt
  sdl2compat.c  SDL 1.2 entry points over SDL2 (only with SDL_BACKEND=2)
  sdl2surface.c shared SDL2 scaler, proven against SDL_gfx by test_zoomdiff
  mugen.rc      error dialog (200), version info, icon 101 from res/icons
third_party/    Lua 5.1.4, libpng 1.2.38, glad 2.1, minizip 0.15, sdk,
                libresample, glew-1.5.4 (reference only, no longer built), vitagl
cmake/          build helpers (MugenSdk.cmake, ThirdParty.cmake)
setup.bat       installs the two Windows devkits + dependency trees, then builds
res/icons/      the window/bubble icon as one .ico per size (see below)
res/shaders/    project-owned shader dialects: gles/ (staged as data/gles by the
                Android build) and vita/ (substituted into the VPK); the desktop
                shaders are the game installation's own data/*.{vert,frag}
tools/          msvc_env.bat, extract_icon.py, iconpack.py, compare_logs.py,
                make_win_sysroot.sh, test_*.sh
port/           android/, psvita/ and arm64/ — the platform ports
docs/           ANALYSIS.md, BUILD-MSVC.md, BUILD-MINGW.md, BUILD-LINUX.md, LUA.md
install/        a real M.U.G.E.N 1.1b1 installation (untracked, for testing)
build/          all build output (git-ignored) — layout below
```

### What lives under `build/`

Each entry point owns its own subtree and never writes into another's.

| Path | Written by | Contents |
|---|---|---|
| `build/mingw/` | root `Makefile` (Windows, default) | `obj/`, `lib/`, `mugen-mingw-win32-sdl2.exe`, `-stripped.exe` (SDL2 backend) |
| `build/mingw-sdl1/` | `make SDL_BACKEND=1` (Windows) | the same shape, `mugen-mingw-win32-sdl1.exe` |

| `build/mingw64-sdl2/` | plain `make` with the 64-bit kit on `PATH`, or `make MUGEN_TARGET=win64` (Windows, experimental) | the same shape, `mugen-mingw-win64-sdl2.exe` |
| `build/<linux-target>/` | root `Makefile` (Linux) | same shape, `mugen-linux-<arch>` |
| `build/<linux-target>-<cross-prefix>-<sdl>/` | root `Makefile` with `CROSS_COMPILE=`/`XSYSROOT=` (via `port/arm64/build.sh`) | same shape, `mugen-linux-<arch>-<prefix>`; the dependency tree it links against is `build/sysroots/arm64-sysroot/` |
| `build/mingw-debug/` | `make DEBUG=1 BUILD=build/mingw-debug` | an explicit `BUILD=` override |
| `build/x86-release/`, `build/x86-debug/` | root `CMakeLists.txt` via `CMakePresets.json` | MSVC `mugen-msvc-win32-sdl1.exe` + `.pdb` |
| `build/psvita*/` | `port/psvita/build.sh` (default `build/psvita/`) or `port/psvita/CMakeLists.txt` by hand (`BUILD=<dir>` picks another) | `mugen`, `mugen-psvita.self`, `mugen-psvita.vpk`, `vita-mugen.cfg` |
| `build/android/` | `port/android/build.sh` | `toolchains/` (JDK/SDK/NDK/CMake, `env.sh`), `sdl2/<abi>/`, `android-deps/<abi>/`, `android-deps-src/`, `sources/`, `build-sdl2/`, `gradle-user-home/`, `asset-list.txt`, `mugen-unleashed-<abi>.apk` |
| `build/baseline/` | hand-made, kept on purpose | SDL1 reference binary and log used by the log-comparison workflow |

Three of those are not any single entry point's private output, so they are named
by *kind* rather than by owner and every script uses the same ones:

| Path | Written by | Contents |
|---|---|---|
| `build/downloads/` | `port/arm64/build.sh`, `port/psvita/build.sh`, `setup.bat`, `tools/make_win_sysroot.sh` callers | every file fetched from the network: the Arm toolchain archive, `arm64-sysroot.zip`, `vitasdk.zip` (196 MB), the two w64devkit self-extractors, `win32-sysroot.zip`, `win64-sysroot.zip`. Purely a cache — safe to delete, and re-verifying it costs a download |
| `build/sysroots/` | `port/arm64/build.sh`, `setup.bat` | the installed dependency trees: `arm64-sysroot/`, `win32-sysroot/`, `win64-sysroot/`. Each holds `usr/{include,lib/lib/pkgconfig}` and is consumed through `XSYSROOT=` (Arm) or found automatically by the `Makefile` (Windows) |
| `build/tmp/` | anything | scratch: staging directories during an install, test output (`build/tmp/<test-name>/`), probe programs and logs. Nothing here is expected to survive; `make` never reads it, and deleting it must never break a build |

Two rules follow from the "keep temporary files inside the project" rule above:

- **Delete only what you created, or whose owner the table names.** The table
  maps the common cases; it is not a whitelist. `make` accepts any `BUILD=<dir>`,
  and `build/mingw64/` (the experimental win64 target) is a legitimate output
  too, so a directory that is absent from the table may still be someone's
  build. "Not in the table" is not evidence of garbage.
- Fair game: scratch files from your own session — probe programs, stub headers,
  one-off scripts, the `.exe`/`.log` your runs produced. Remove those when you
  are done instead of letting them accumulate; several debugging scripts and
  stray binaries once piled up in `build/` itself. If you cannot tell whether a
  directory is yours, leave it.
- `port/android/build.sh` also writes `port/android/app/src/main/assets/`
  (staging) and `port/android/app/build/`, `port/android/app/.cxx/` (Gradle's
  own output). Those are Gradle-owned, are git-ignored, and are recreated from
  `src/main/assets` and the NDK on every build.

## Building

**32-bit x86 is the reference.** Every struct in `src/` has the original's field
offsets, checked at compile time with `MUGEN_ASSERT_SIZE` / `MUGEN_ASSERT_OFFSET`.
Those assertions describe the original binary, so `MUGEN_LAYOUT_32BIT`
(`src/mugen.h`) compiles them out when pointers are not 4 bytes.

**Disabling the assertions is not evidence that a 64-bit build is correct.**
They only stop *checking*; they do not make offset-dependent code safe. After the
last audit no source reads a struct of ours through a hardcoded byte offset, but
the pattern can reappear: before claiming a 64-bit target works, grep for
`(const char *)… + 0x` / `(char *)… + 0x` and confirm none does.

The five struct reads that used to be hardcoded offsets are member accesses now:
`camera.c` `Bg.legacy` (0x64, 0x7c on 64-bit) and `BgElem.y` (0x88),
`char.c` `Projectile.afterimage` (0x504, 0x548 on 64-bit), `ai.c`
`InputBuf.keys` (0x78) and `sprite.c` `GlTex.size` (0x18). Their comments keep
the original offset and say why it must not come back; `tools/test_abi_layout.sh`
prints both layouts.

The other 64-bit trap is a `memset`/`memcpy` with a byte count that spans
pointer-bearing fields. `char.c` clears the `states` block and the `nohit_time`
block with `offsetof(Player, <end>) - offsetof(Player, <start>)`, which is the
original's 0x420 / 0x158 on 32-bit and the grown span on 64-bit.

So the 64-bit targets are **portable in progress, not audited**: the layout
assertions are compiled out there and only the win32 build is log-diffed. Before
claiming a 64-bit target works, grep for `(const char *)… + 0x` /
`(char *)… + 0x` and confirm no struct of ours is read by absolute offset, then
run the same scenario under both builds and compare the logs with
`tools/compare_logs.py`.

The CMake/MSVC build still hard-rejects anything but 32-bit MSVC. The Makefile
builds Linux x64/arm64 and an experimental `win64` target (a 64-bit static kit).
On Windows the target follows the compiler's own triple, so the kit on `PATH`
chooses it: `x86devkit`'s `i686-w64-mingw32` gcc builds win32, `w64devkit`'s
`x86_64-w64-mingw32` gcc builds win64 — no variable needed, and `MUGEN_TARGET=`
still overrides. See `PLANS.md` for what is still outstanding on the 64-bit side,
and do not describe either as verified until the offsets above are dealt with.

Output names differ by build: `mugen-msvc-win32-sdl1.exe` (MSVC),
`mugen-mingw-win32-sdl2.exe` (MinGW/SDL2), `mugen-mingw-win32-sdl1.exe` (MinGW/SDL 1.2),
`mugen-linux-<arch>` (Linux).

### MSVC 2022 (primary)

```bat
tools\msvc_env.bat cmake --preset x86-release
tools\msvc_env.bat cmake --build --preset x86-release
```

→ `build\x86-release\mugen-msvc-win32-sdl1.exe` + `.pdb`

Debug variant: same with `x86-debug`. `tools\msvc_env.bat` finds VS 2022 through
`vswhere`, so any edition or install path works. Never call `cmake` or `cl`
directly without it — the environment will be missing.

Details: `docs/BUILD-MSVC.md`.

### Linux (same Makefile, auto-detected)

The `Makefile` also builds Linux targets, chosen from `uname` + the compiler's
triple. See `docs/BUILD-LINUX.md` for the dependency list.

```sh
make -j8              # build/linux-x64/mugen-linux-x64
make install          # → install/mugen-linux-x64 (not a .exe)
```

`mugen.log` from the Linux build is identical to the Windows build's, so the
same validation method works. Windows stays the reference build; the Linux
target is additive and must not disturb it.

**`linux-arm64` defaults to `MUGEN_GLES=1`**, the GLES 3.0 backend, instead of
desktop OpenGL: the handhelds that target exists for expose OpenGL ES only, and
their `libGL.so.1` is the GLX stub, so `gladLoadGL()` resolves nothing against
the EGL context SDL2 creates. That build compiles `third_party/glad-es-3.0`,
excludes `src/glrender.c`/`src/gldetect.c` (they define the same `Gl_*` entry
points as `src/gles_render.c`), links no `-lGL -lGLU`, and `make install` copies
`res/shaders/gles/*` to `install/data/gles/`. `linux-x64` and the Windows
targets keep desktop GL — they are the reference builds. `make MUGEN_GLES=0`
(or `=1`) overrides the default. See `PROBLEMS.md` (P1) and
`docs/BUILD-LINUX.md`.

### Cross-compiling a Linux target

`CROSS_COMPILE=<prefix>` renames the toolchain and `XSYSROOT=<dir>` names the
dependency tree (SDL2, SDL2_mixer, SDL2_ttf, zlib, libpng, FreeType as shared
objects, plus headers and `.pc` files laid out as
`<dir>/usr/{include,lib,lib/pkgconfig}`). The output takes the prefix into its
name and directory, so a cross build never shares objects with a host build of
the same triple.

For arm64 the wiring lives in **`port/arm64/`** (like `port/android`), not in the
top-level `tools/`. The Makefile stays generic: it only knows `CROSS_COMPILE=` and
`XSYSROOT=`, so another target can reuse them.

```sh
port/arm64/build.sh --setup   # once: the toolchain, ~330 MB
port/arm64/build.sh           # dependency tree + build
DEVICE_PASS=ark port/arm64/build.sh --upload   # ... and copy to the device
```

`--upload` sends the binary to `ark@192.168.7.2:~/mugen/mugen-linux-arm64`,
keeping the old one as `.prev`. It uses **tar over ssh, not scp/sftp**: that
device's sshd has a dangling `sftp` subsystem path
(`/usr/local/crosware/.../dropbear/.../sftp-server`), so scp and sftp both fail
there while plain ssh works. The upload is md5-verified and only then renamed
into place. See `port/arm64/README.md`.

**The toolchain is chosen by the device's glibc**, and this is the one thing
that silently produces a binary that will not start. `ARM64_TOOLCHAIN=9.2` is
the default because it targets glibc 2.30 (binary needs `GLIBC_2.29`), which is
what an ArkOS/RG351MP has — verified on hardware there, GLES renderer included.
`ARM64_TOOLCHAIN=15.3` targets glibc 2.42 and produces a binary wanting
`GLIBC_2.38`, which that device's loader rejects outright. Always ask the device
first: `ssh ark@<device> 'ldd --version | head -1'`.

`--setup` downloads Arm's Mingw-w64 toolchain into `TOOLCHAIN_DIR`
(`C:/arm-gnu-toolchain` by default) and verifies it against the checksum Arm
publishes. The 9.2 release is a `.tar.xz` from `developer.arm.com/-/media/Files/
downloads/gnu-a/9.2-2019.12/binrel/` with a top-level version directory and an
**MD5** (in a file named `.sha256asc`); 15.3 is a `.zip` from GitLab with
SHA-256 and no version directory. The GitLab URL needs `%2F` in the project
path — a literal `/` returns 404.

The dependency tree those libraries live in is a **release asset**, not something
this repository builds: `build.sh` downloads `arm64-sysroot.zip` from the
project's `toolchains-v1.0` release into `build/sysroots/arm64-sysroot/` (cache
in `build/downloads/`) and verifies it against a SHA-256 pinned in `build.sh`; `ARM64_SYSROOT_URL`/`ARM64_SYSROOT_SHA256` override both for a fork or
a republished asset. Assembling it needs an aarch64 Ubuntu root filesystem and
produces the same tree every time, so it is published once and consumed as data;
the generator that used to do it locally is no longer in the tree. The toolchain
directory is written to by `--setup` alone, and nowhere else — see
`docs/BUILD-LINUX.md` ("The dependency tree") for what it holds and why.

**Pad binding.** Every SDL2 target binds controllers through SDL's
game-controller semantics (`MUGEN_GAMEPAD_POLICY`, `src/mugen.h`), configured by
`[P<n> Gamepad]` in `mugen.cfg`; a missing group falls back to the built-in
1..11 layout and is written into the file. The raw `[P<n> Joystick]` indices are
the original model and only line up with a pad whose axis/hat/button topology
matches the PC pad they were written for — on a handheld the d-pad arrives as
buttons, so those indices land on the wrong controls. The SDL 1.2 backend keeps
the original raw lifecycle, so the reference build is unchanged.

SDL's game-controller API reports only the controls its *mapping* names, and
some handheld entries in its database stop at the face buttons: the GO-Super
Gamepad (RG351MP/ArkOS) names `b0..b11` and nothing else, so SELECT, START and
the stick clicks never arrived as controller buttons at all — the raw joystick
saw them, but a controller-driven slot ignores raw codes. `pad_mapping_fixups[]`
in `src/pads.c` completes such a mapping before the controller is opened
(query the mapping SDL uses, append the missing `back:`/`start:`/stick-click
bindings, register it again), keyed by GUID so no other pad is affected and
skipped when the mapping is already complete. Its indices are raw joystick
button numbers — the same numbering the kernel reports through the `BTN_*` order
and that ArkOS's own `~/.config/retroarch/autoconfig/udev/GO-Super Gamepad.cfg`
uses (`select=12, start=13`). Read a pad's numbering with `EVIOCGBIT(EV_KEY)`
on its `/dev/input/eventN`, not from the `BTN_*` names: the vendor driver
reports SELECT/START on the codes the kernel calls `THUMBL`/`THUMBR`.

Three things neither script can fix, all in `docs/BUILD-LINUX.md` (which also
has a troubleshooting table for the errors a first attempt produces):

- the toolchain's glibc must be no newer than the device's (see above);
- a `--sysroot` merge into the toolchain does not link against a distribution
  SDL2, because its X11/ALSA/Pulse/Wayland closure is not reachable there;
- the libraries are **shared**, so the target needs its distro's SDL2 packages.

### GNU Make + MinGW (Windows, fully static)

```sh
export PATH=/c/x86devkit/bin:$PATH      # the 32-bit kit, not w64devkit
make check-toolchain
make -j8
make install                            # strips and copies into install/
```

→ `build/mingw/mugen-mingw-win32-sdl2.exe`, and `install/mugen-mingw-win32-sdl2.exe` after
`make install`.

This build links **everything statically** (SDL2, SDL2_mixer, SDL2_ttf, zlib,
pthread from the kit; Lua, libpng, glad, minizip, libresample from source). The
result needs no `Elecbyte.MUGEN.libs` — only Windows system DLLs and
`msvcrt.dll`. The CMake/MSVC build still links the game's DLLs; the two are not
the same link step.

`SDL_BACKEND` selects the SDL stack. **2 is the default** and is the normal
target; `SDL_BACKEND=1` builds the older SDL 1.2 backend into
`build/mingw-sdl1/mugen-mingw-win32-sdl1.exe`, which exists for the differential
comparisons against the original binary. Switching the variable in an existing
build directory is safe: `$(OBJ_DIR)/.config` records the backend and target,
every object depends on it, and it is only rewritten when the configuration
changes, so a switch recompiles the tree and a repeat build recompiles nothing.

Because SDL2_mixer carries mpg123 and vorbisfile, WAV, MP3 and Ogg Vorbis play
with no external decoder DLL (`MUGEN_SDLMIX_NATIVE_CODECS`, `src/mugen.h`). A
`mugen.cfg` that still lists `mpg123` or `vorbisplug` is harmless: a plugin whose
DLL is absent is skipped rather than aborting BGM start-up. Android advertises a
narrower list, matching what its SDL2_mixer is built with.

`make check-toolchain` (Windows) refuses a compiler whose triple does not match
the target (unless `MUGEN_TARGET` is given explicitly),
verifies `pkg-config` resolves every package in `STATIC_PKGS`, and checks the
static archives exist (not just `.pc` files). Under a Linux target it checks
that triple instead and skips the archive/`windres` checks. `make DEBUG=1
BUILD=build/mingw-debug` for an unoptimised build. Details:
`docs/BUILD-MINGW.md`.

Do not assume the shell's default `gcc` is the 32-bit one: this environment has
both `w64devkit` (x86_64) and `x86devkit` (i686) on `PATH`, and the x64 kit
compiles `-m32` but cannot link. The Makefile therefore asks the compiler for
its own triple (`gcc -dumpmachine`, falling back to the `Target:` line of
`gcc -v`) and builds the target that triple belongs to: with `w64devkit` first
on `PATH` a plain `make` produces the experimental win64 build, not win32. For
the 32-bit reference build, put `x86devkit` first or pass the compiler
(`CC=/c/x86devkit/bin/gcc make`).

`setup.bat` (repository root) installs both devkits and both dependency trees
from nothing, verifies them and can build: `setup.bat /build`,
`/build32`, `/build64`, `/check`, `/force`. The kits go to `C:\w64devkit` and
`C:\x86devkit`; the trees go to `build/sysroots/win64-sysroot` and
`build/sysroots/win32-sysroot`.

A kit from skeeto's w64devkit ships **no SDL, no libpng and no `.pc` files**, so
the tree is what supplies the libraries — and because it sits at a fixed path
inside the repository, the `Makefile` finds it by itself and points pkg-config
at it (in Windows `C:/...` spelling, since `sh` does not convert POSIX paths for
a native `pkg-config.exe`). So the whole Windows build is `PATH=/c/w64devkit/bin:$PATH
make`; `WINSYSROOT=<dir>` picks another tree and `WINSYSROOT=` disables the
automatic one. `make check-toolchain` prints which tree it resolved. See
`docs/BUILD-MINGW.md`.

Both kits ship their own `pkg-config`, and MSYS `sh` does not convert POSIX
paths for a native executable, so `PKG_CONFIG_PATH=/c/...` in a make variable
silently resolves nothing while the same value typed into bash works — use
`C:/...` spellings. A kit without the SDL add-ons fails `check-pkgconfig`;
`tools/make_win_sysroot.sh <kit> win32|win64` packages that kit's dependency set
as a relocatable tree (published as `win32-sysroot.zip`/`win64-sysroot.zip`,
same shape as `arm64-sysroot.zip`). Both traps: `docs/BUILD-MINGW64.md`.

### PS Vita (VitaSDK)

```sh
port/psvita/build.sh              # installs VitaSDK if absent, then builds
port/psvita/build.sh --check-tools
```

VitaSDK is a release asset (`vitasdk.zip`, SHA-256 pinned in the script) and is
unpacked to `C:/vitasdk` — one of the two sanctioned exceptions to the
temp-file rule above. It is a complete SDK: `sdl`, `sdl_gfx`, `sdl_mixer`,
`sdl_ttf`, `zlib`, `libpng`, `vitaGL` and vitaGL's helpers are already in it, so
a clean machine needs no `vdpm` run; the script checks the artefacts and only
calls `vdpm` for something genuinely missing. Output is
`build/psvita/mugen-psvita.{self,vpk}`, and the VPK packs `install/` as `/data`
unless `--no-data` is given. The build needs Windows (the SDK's compiler is
`arm-vita-eabi-gcc.exe`); `--offline` forbids every download. Details:
`port/psvita/README.md`.

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

## The icon

`res/icons/` holds the project's own artwork as one single-size `.ico` per size:
`1.ico` 64x64, `2.ico` 48, `8.ico` 32, `3.ico` 24, `4.ico` 16 (all 32-bit with
alpha); `5`, `6`, `7` and `9` are the same drawing as 8-bit palette images.
All three platforms use it, and nothing else: the artwork is edited in one place.

- The Windows Makefile merges the 32-bit set into `build/mingw/icon/mugen.ico`
  with `tools/iconpack.py` and `windres` embeds it as icon group 101. Windows
  picks the size it needs per context, so the file must carry all of them; a
  single-size icon is scaled and looks soft. Without `python3` the first icon is
  copied as-is and the build still works. `MUGEN_ICONS=` picks other files,
  `MUGEN_GAME_DIR=` reverts to the original `mugen.exe`'s icon.
- The PS Vita build derives all of `sce_sys/` from `res/icons/1.ico` at
  configure time (`port/psvita/cmake/Icon.cmake`, run through
  `cmake -P` by `tools/test_vita_icon.sh`): the 128x128 bubble icon, the 840x500
  LiveArea background and the 280x158 start-up image. They are written into the
  build tree; the committed copies under `port/psvita/sce_sys/` are the same
  derivation and are packed only when the conversion cannot run.

  **These three PNGs must be 8-bit indexed (colour type 3), not RGBA.** The Vita's
  package validation refuses RGBA artwork at install time with `0x8010113D` — the
  file packs and copies fine, only the console says no, with a code that names
  neither the file nor the reason. `tools/iconpack.py --indexed` writes the
  palette PNG (`--flatten` drops the alpha, which `icon0`/`bg` require; the
  start-up image keeps transparency as a `tRNS` chunk) and `Icon.cmake` reads
  each packed asset's IHDR afterwards, failing the configure unless it is type 3,
  bit depth 8 (or 1, as the VitaSDK sample backgrounds are) and non-interlaced.
  Every VitaSDK sample ships that format under
  `$VITASDK/share/gcc-arm-vita-eabi/samples/*/sce_sys`; treat it as the
  reference. The committed fallbacks are regenerated with the same commands, so
  a machine without Python still packs a VPK the console accepts.
- The Android build runs `tools/make_android_icons.sh` from `port/android/build.sh`
  before Gradle, deriving the five density buckets (`ic_launcher.png`,
  48/72/96/144/192) and the adaptive icon's foreground (108dp canvas,
  108/162/216/324/432, artwork confined to the 66% safe zone). It writes into
  `app/build/generated/res/mugen-icons`, which `build.gradle` adds to the debug
  and release source sets so it overrides — rather than collides with — the
  committed copies in `app/src/main/res/mipmap-*`, which are that same
  generation and serve a build without Python. `port/android/tests/test_icons.py`
  keeps the two from drifting.

`tools/iconpack.py` is standard-library only, so no build needs an imaging
package. It is the only place that decodes `.ico` (BMP/DIB entries with an AND
mask) and writes PNG; keep it that way rather than duplicating the decoder. Its
`png` subcommand makes a square icon, `canvas` composes the artwork onto a
larger frame (Vita LiveArea, Android adaptive foreground).

## Testing

There are two kinds of check, and they answer different questions.

**1. Differential tests — does the port match the original?**

`tools/test_zoomdiff.sh` builds the same 21 scaling cases against the SDL 1.2
SDL_gfx (`SDL_BACKEND=1`) and the SDL2 replacement, and requires every result to
be byte-identical. It is the guard against silent divergence in software
rendering and it has caught real regressions, so run it after touching
`src/sdl2surface.c` or anything that feeds it:

```sh
bash tools/test_zoomdiff.sh
```

When a case legitimately cannot match — SDL2 discarded a piece of state SDL 1.2
had, such as the real palette length — narrow what the dump records *and say why
in the comment*. Do not weaken the comparison wholesale; the pixel data must stay
fully compared.

The Android port has its own suite under `port/android/tests/` (native SDL2,
present host, manifest hashes, plus `test_build.py`); the Vita port has
`port/psvita/validation/`. Those cover pieces the desktop build cannot reach.

**2. Log comparison — does it behave like the original?**

Behaviour is validated by **running both binaries on the same scenario and
diffing `mugen.log`** (`docs/ANALYSIS.md`, "Validation").

`install/` holds a real M.U.G.E.N 1.1b1 tree, so tests can be run end to end.
`make install` puts the static executable there for you:

```sh
make install
cd install
./mugen-mingw-win32-sdl2.exe -nosound          # startup/mode select
```

For the MSVC build, copy `build/x86-release/mugen-msvc-win32-sdl1.exe` into `install/`
by hand; it needs the `Elecbyte.MUGEN.libs` folder, the static one does not. An
`SDL_BACKEND=1` static build installs as `mugen-mingw-win32-sdl1.exe` and does not
overwrite the default one.

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
| startup → mode select | `./mugen-mingw-win32-sdl2.exe -nosound` (or `mugen-linux-x64` under Linux) |
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
