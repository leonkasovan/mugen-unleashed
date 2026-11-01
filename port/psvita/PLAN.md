# PS Vita: SDL 1.2 to SDL2 and programmable vitaGL rendering

Date: 2026-11-02 (Asia/Jakarta).
Status: implementation plan; no migration milestone is marked complete by this document.
Repository inspected: `sdl2` at `f0ad526907378f4b6b2fe7aa4384841fa99f3dba`.
VitaSDK package recipes inspected at `de769c80482cc3e53cc16b71cb4bb27a323e5a31`.
Repository vitaGL pin: `9bf3e3f4e55efd1f4a191b179144f7f9a98cd925`.

This plan supersedes contradictory operational guidance in the Vita README for
the migration work. Historical failures remain useful evidence, but source and
new measurements take precedence. This document does not claim a successful
Vita cross-build, working on-device SDL2, pixel equivalence, audible playback,
or a particular frame rate. Those are acceptance gates below.

## 1. Goals, scope and build ownership

The deliverable is a PS Vita VPK built with SDL2 whose `[Video] RenderMode =
OpenGL` selects an actual GPU sprite renderer implemented through vitaGL.
A successful context creation followed by an automatic System fallback is
not success for this goal.

The migration must also retain a working System renderer as a diagnostic and
recovery path. OpenGLScreen is a separate compatibility mode: CPU drawing
followed by a GPU screen upload, rather than the GPU sprite renderer.

| Build entry point | Supported role | Rule for this migration |
|---|---|---|
| Root `Makefile` | Windows/MinGW and Linux engine builds | Do not add Vita targets, flags, dependencies or packaging |
| Root `CMakeLists.txt` | Existing Windows/MSVC build | Preserve its existing behavior |
| `port/psvita/CMakeLists.txt` | Vita toolchain, engine, SELF and VPK | All Vita engine build changes belong here or in its included CMake helpers |
| `port/psvita/input-mapper/CMakeLists.txt` | Separate Vita diagnostic application | Give it explicit matching SDL backend selection |
| `port/psvita/cmake/VitaGl.cmake` | Vendored vitaGL library target | Connect the selected library to the final Vita executable |

This boundary is verified in the current sources. The root Makefile detects
Windows/Linux and recognizes `win32`, `linux-x86`, `linux-x64` and
`linux-arm64` toolchains. It contains no Vita SELF/VPK build path. A CMake
generator may itself invoke a make program; that does not make the root
project Makefile a Vita build entry point. Use CMake plus Ninja in the
documented Vita workflow.

Scope constraints:

- Reuse the existing SDL2 API adaptations; do not port the whole engine a
  second time or take the Android lifecycle wholesale.
- Keep desktop and Android behavior isolated behind existing or explicit
  platform/backend conditions. Never enable `ANDROID_PORT` for Vita.
- Preserve reconstructed structures, original address annotations and
  floating-point expression order. Store new GL/window metadata in port-owned
  structures, not fields added to original `Sprite`, `GlTex` or `GlProgram`.
- Preserve configured game resolution and gameplay coordinates. Changing SDL
  version is not permission to change `GameWidth`/`GameHeight`. The current
  packaged defaults are 480x272; the physical Vita panel is 960x544.
- Do not rewrite files in the reference `install/` tree. Package generated
  configuration and Vita shader variants from the build directory.
- Keep an SDL1 Vita build and separate build directories until SDL2 passes its
  hardware gates. Do not change the default backend in the first implementation.
- Optimize after correctness. A constant-alpha no-op, dropped projective Q,
  missing palette colors, or a silent renderer fallback is a release blocker.

## 2. Verified starting point

| Area | What the inspected source actually does | Consequence |
|---|---|---|
| Vita SDL dependency | Checks `include/SDL/SDL.h`; requests `sdl SDL_gfx SDL_mixer SDL_ttf` in both normal and static pkg-config lists | No SDL2 selection exists in Vita CMake |
| Shared SDL2 bridge | `src/sdl2compat.c/.h` is enabled by `MUGEN_SDL2`; `mugen.h` derives `MUGEN_SDL2_API` | Reuse this foundation, but audit Vita interactions |
| Source glob | Vita CMake includes `src/*.c`, including the SDL2 bridge file; its implementation is disabled without `MUGEN_SDL2` | Merely finding an object/source filename does not prove SDL2 is active |
| System presentation | `video.c` requests a panel-sized surface under `MUGEN_VITA_GL`; `gfx.c` performs CPU upscale and SDL1 flip handling | Separate this SDL1 policy from target identity |
| GL initialization | The port probes vitaGL capabilities, then can fall back to System | Add functional probes and strict verification |
| Main sprite drawing | `Gl_DrawQuadIndexed` and `Gl_DrawQuadRgba` use `glBegin/glEnd` | They must submit explicit attributes through the programmable draw path |
| vitaGL immediate mode | The pinned `ffp.c` reloads fixed-function shaders in `glEnd` | Binding a custom program is insufficient |
| Shader assets | Four rewritten `port/psvita/shaders/{indexed,rgba}.{vert,frag}` files already exist and are selected for VPK substitution | Audit and finish integration; do not list the rewrite as entirely unimplemented |
| Calibration | `gldetect.c` still embeds legacy GLSL, 1D samplers and immediate-mode draws | Packaging four shaders alone does not fix startup calibration |
| GL gaps | The shim stores blend color without applying it and drops projective coordinates in its four-component texcoord wrapper | Implement correct behavior in the new renderer |
| Vendored vitaGL | The option creates `vitagl_vita`, but the executable's current library list is populated by SDK `find_library(vitaGL)` | Building the vendored target is not evidence that the game links it |
| Memory | CMake defaults are `MEMSIZE=0` and `HEAP_MIB=64` | Keep normal-app allocation; do not restore the old system-mode cap |
| Audio routing | The engine's `sdlmix` subsystem rejects WAV/MP3/OGG; Vita `LoadLibraryA` returns NULL | Installed codecs are not equivalent to usable engine BGM support |

The desktop migration document reports 19 differential scaling cases and eight
deterministic normalized log comparisons. Treat those as recorded desktop
results, not as newly rerun tests or Vita evidence. Its unresolved lifecycle,
audible audio and music-path checks still need platform-specific coverage.
See [SDL2_PLAN.md](../../SDL2_PLAN.md).

### 2.1 Current source references

- [Root Makefile](../../Makefile): platform detection and SDL backend selection.
- [Vita CMake](CMakeLists.txt): source list, compile definitions, package
  discovery, vitaGL selection and VPK construction.
- [SDL2 bridge](../../src/sdl2compat.c) and
  [header](../../src/sdl2compat.h): surfaces, presentation, key translation
  and local `zoomSurface`.
- [Video](../../src/video.c), [presentation](../../src/gfx.c) and
  [startup](../../src/system.c): target dimensions, mode setup and fallback.
- [GL renderer](../../src/glrender.c), [calibration](../../src/gldetect.c),
  [primitives](../../src/prim.c) and [Vita shim](src/vita_gl_shim.c).
- [Vita library helper](cmake/VitaGl.cmake),
  [vendoring record](../../third_party/vitagl/VENDORED.md), and shader assets.
- [Events](../../src/events.c), [pads](../../src/pads.c),
  [keyboard](../../src/keyboard.c), [config](../../src/config.c).
- [Fonts](../../src/font.c), [sound](../../src/sound.c),
  [BGM plugin routing](../../src/bgmplugins.c),
  [streams](../../src/stream.c), and
  [mixer query adapter](../../src/sdlmixercompat.c).
- [Existing Vita issue history](ISSUES.md): useful hypotheses and hardware
  observations; open issues must not be represented as solved.

## 3. Dependency and architecture decisions

### 3.1 Use an SDL2 backend that actually exposes vitaGL

The inspected official VitaSDK recipes include two mutually conflicting
providers of SDL2:

| Package/provider | Inspected recipe | Intended use |
|---|---|---|
| `sdl2` | 2.32.8-1, ordinary SDL2 recipe | Optional GXM/System bring-up reference; not proof of vitaGL context support |
| `sdl2_vitagl` | 2.32.8-1, SDL2 plus the recipe's vitaGL backend patch | Preferred provider for the required OpenGL implementation |
| `sdl2_mixer` | 2.8.2-1 | SDL2 mixer, with its own current codec dependency graph |
| `sdl2_ttf` | 2.24.0-1 | SDL2 font API, FreeType and HarfBuzz dependencies |
| `sdl2_gfx` | 1.0.4-2 | Available in VitaSDK; optional alternative scaler, not needed with the existing local implementation |

The `sdl2_vitagl` recipe provides `sdl2` and conflicts with `sdl2`. Do not
install/link both providers into one dependency prefix. The package manager
name is not necessarily the pkg-config module name: inspect the selected
prefix's `.pc` files. Expected module candidates are `sdl2`, `SDL2_mixer` and
`SDL2_ttf`; verify them instead of silently substituting SDL1 modules.

Prefer a pinned dependency prefix or a reproducible source build over
overwriting the developer's baseline SDK. Record SDL release, patch SHA256,
package recipe commit, float ABI, compiler version and selected vitaGL SHA.
The currently inspected package patch enables `VIDEO_VITA_VGL`. This is an
SDL dependency build option; passing it to the engine project alone does
not rebuild or change the SDL archive.

Upstream SDL's PVR/gl4es/PIB alternatives are different implementations.
They are not the selected architecture and must not be confused with
the vitaGL package patch.

### 3.2 Select exactly one owner of the graphics device

Preferred ownership:

1. SDL2's VitaGL driver initializes the native graphics device and exposes its
   singleton through SDL's window/context API.
2. The System mode uses SDL2's renderer/streaming texture path.
3. OpenGL mode uses the port's programmable vitaGL renderer directly.
4. SDL owns native swap and driver shutdown; the engine owns its GL resources.

The inspected package patch initializes vitaGL at panel size, exposes a
singleton context token, routes swaps to vitaGL, and has limited context
deletion/rebinding behavior. A non-NULL `SDL_GLContext` does not prove all
operations work. Audit error returns and same-context behavior in the pinned
patch. Do not call `vglInitExtended`, `vglSwapBuffers` or native GXM init again
from engine code when SDL owns them.

Never use `SDL_Renderer` and engine GL drawing concurrently on the same window.
Destroy the old presentation objects before switching kind; mode-transition
tests must establish whether the pinned singleton driver needs an additional
scoped lifecycle patch. Keep that patch in the Vita dependency recipe, not as
untracked edits in a global SDK.

### 3.3 Keep the three renderer meanings distinct

| `mugen.cfg` value | CPU work | GPU work | Successful evidence |
|---|---|---|---|
| `System` | Existing software sprite rasterizer | Texture upload, scaling and presentation | Correct frame, no SDL1 CPU panel upscale |
| `OpenGL` | Existing game logic, transforms and asset preparation | Indexed/RGBA sprite shaders, palette lookup, compositing and presentation | Actual programmable draw counters and validated pixels |
| `OpenGLScreen` | Existing software frame generation | Screen upload and textured quad | Correct screen upload; do not claim GPU sprite rasterization |

All modes retain the configured game dimensions. The panel/output size and
framebuffer/game size are separate values. Aspect handling follows
`KeepAspect`; use letterboxing or an explicit configured stretch policy,
not a silent change of logical resolution.

### 3.4 Proposed CMake interface

These are planned engine options, not commands supported by the baseline.

| Option | Planned meaning / rollout default |
|---|---|
| `MUGEN_VITA_SDL_BACKEND=1|2` | SDL major selection; initially default 1, change to 2 only at the release gate |
| `MUGEN_VITA_SDL2_PROVIDER=vitagl|gxm` | SDL2 provider; vitaGL for the target OpenGL path |
| `MUGEN_VITA_GL_RENDERER=legacy|programmable` | Existing reference path versus new port renderer; programmable for qualified SDL2/OpenGL |
| `MUGEN_VITA_DEPS_PREFIX` | Optional isolated target dependency prefix, with no host-library leakage |
| `MUGEN_VITA_RENDERER_STRICT=ON|OFF` | ON makes an unfulfilled requested renderer fail visibly; use ON for qualification |
| `MUGEN_VITA_BUILD_TESTS=ON|OFF` | Build on-device probes/test VPKs through Vita CMake |
| Existing `MUGEN_VITA_BUILD_VITAGL` | Must select the vendored target in the final link, not merely create it |
| Existing `MUGEN_VITA_RENDERMODE` | Pack the requested cfg value; preserve the existing empty-value meaning of skipping only that rewrite |
| Existing dimensions, fullscreen, heap and title options | Keep their current semantics; backend-specific output directories prevent object reuse |

Reject unsupported combinations during configuration. A GXM-only SDL2 build
must not claim it can satisfy `RenderMode=OpenGL`. Keep automatic System
fallback available for ordinary recovery, but log requested and actual modes
and forbid it in strict test profiles.

Use `MUGEN_VITA` for target identity, `MUGEN_SDL2` for shared SDL2 adaptation,
and `MUGEN_VITA_GL` only for the appropriate GL declaration/provider role.
Add a narrowly scoped programmable-renderer definition if needed. Move Vita
logging, input and error handling away from GL-provider guards so they remain
active in System mode. Never use a renderer macro as the only proof of SDL
major version.

## 4. Implementation milestones

Each milestone is a separate reviewable change with its own evidence.
The status boxes below remain unchecked until the named gate is run.

### M0 — Freeze the baseline and dependency manifest

- [ ] Record the engine commit, VitaSDK compiler version, target float ABI,
  installed package versions, SDL provider, vitaGL source/flags and console
  firmware in `build/psvita-validation/manifest.json`.
- [ ] Preserve one known SDL1/System VPK, its unstripped ELF, packed config and
  hashes. Use a separate directory and test title ID for candidate VPKs; do not
  overwrite the baseline installation while collecting evidence.
- [ ] Capture the same game-data hashes, KFM/stage fixtures, fonts, locale,
  resolution, sound settings, controller topology and screen-pack files.
- [ ] Obtain baseline screenshots and logs for startup, menus, character select,
  the standing preview, quick VS, pause, round end, storyboard and exit.
- [ ] Record the existing preview crash as an unresolved baseline failure;
  investigate lifetime and clipping with a core dump. Plausible surface fields
  do not establish object liveness. Neither absent guard logs nor reaching the
  gameflow loop rules out a port defect.
- [ ] Record frame times and memory at both the configured render size and actual
  output size. Capture CPU drawing, scale/copy, texture upload and flip wait
  separately; `tick=60` alone does not identify the bottleneck.
- [ ] Inventory all compiled SDL1-specific calls and all `glBegin/glEnd` uses,
  including calibration and primitives, rather than only the main sprite code.

Gate: the baseline can be reinstalled, its assets identified, and the same
scenario rerun. An unavailable capture is an open item, not an inferred pass.

### M1 — Add the SDL2 CMake profile and prove the final link

Primary files: `port/psvita/CMakeLists.txt`, new
`port/psvita/cmake/SdlBackend.cmake`, existing `cmake/VitaGl.cmake`,
`src/mugen.h` and the input-mapper's separate CMake project.

1. Add the proposed major/provider selectors. Branch header checks, package
   discovery, include paths, compile definitions and static dependencies together.
   SDL1 uses only SDL1 headers/libraries; SDL2 uses only SDL2.
2. For SDL2 define `MUGEN_SDL2` and `SDL_MAIN_HANDLED` on the engine target.
   Reuse `src/sdl2compat.c`, which is already in the glob, rather than adding a
   duplicate source entry or a second implementation of its symbols.
3. Filter package-provided main-renaming/startup flags only where necessary;
   choose one Vita entry point and call `SDL_SetMainReady` before SDL2 init
   when bypassing its startup wrapper. Do not import the Windows `SDL2main`
   startup assumption.
4. Keep `compat_linux.h` force-inclusion, 32-bit ARM/newlib ABI, existing
   reconstructed structure assertions and engine floating-point flags.
   Do not propagate vitaGL's `-ffast-math` to engine code.
5. Use target-scoped library/include settings. Resolve static private
   dependencies from the selected packages' `STATIC_` data or equivalent
   imported targets. Verify FreeType/HarfBuzz, compression and mixer codecs
   rather than copying the old `libmikmod/libmpg123` list.
6. Honor the existing Windows-host pkg-config handling and libpng shim where
   still required. Restrict discovery to Vita target prefixes; reject a host
   SDL archive even if pkg-config successfully finds it.
7. When vendored vitaGL is selected, replace the SDK vitaGL link entry with the
   `vitagl_vita` CMake target. Remove/normalize transitive `-lvitaGL` entries
   from the SDL2 provider as necessary. Preserve the rest of its dependencies.
   Never link two vitaGL implementations or two SDL major versions.
8. Replace the blanket `--allow-multiple-definition` concession with an audited
   resolution. Inspect new decoder archives for `Player_Free` and related
   symbol collisions. Do not let archive ordering select an unrelated function.
9. Add compile/link probes for SDL major, mixer/TTF headers, key/RWops types,
   texture formats and the chosen GL provider. Use cross-compilation-friendly
   static try-compiles; do not execute ARM programs on the host.
10. Print a configure summary of provider, header/library paths, flags, renderer
    profile and dependency pins. Emit a link map and retain the unstripped ELF.
11. Apply matching backend discovery to the standalone input mapper and add
    CMake-built `vita_sdl2_probe` / `vita_gl_probe` test targets. The mapper's
    current SDL1 surface/display code also needs an SDL2 presentation adapter.
12. Track generated cfg and shader inputs in packaging dependencies so changing
    one actually rebuilds the VPK without deleting the entire build directory.

Gate: both isolated SDL1 and SDL2 profiles cross-compile; the link map contains
one selected SDL2 and one selected vitaGL implementation; no SDL1 archive is
present in the SDL2 profile. Root Makefile remains unchanged. Compile success
is not the runtime gate.

### M2 — Correct platform startup, input and diagnostics

Primary files: `src/main.c`, `system.c`, `events.c`, `keyboard.c`, `pads.c`,
`config.c`, `error.c`, `log.c` and new port-owned input/platform helpers.

#### Startup and failure handling

- [ ] Check SDL initialization results. Keep video usable when optional audio
  initialization fails; diagnose the failing subsystem explicitly.
- [ ] Retain Vita stdout/stderr redirection and writable log fallback under
  target-identity guards. Report exact log paths and initialization stage.
- [ ] Replace the Vita SDL1-only `SDL_VITA_ShowMessageBox` use for SDL2.
  Use an audited SDL2 message-box path or a port error overlay after graphics
  initialization; before that, retain file logging. Message-box setup must not
  create a second GXM/vitaGL owner or invalidate the active render context.
- [ ] Define cleanup order for partial initialization: stop mixer callbacks,
  release streams/chunks/fonts, free renderer objects while graphics is alive,
  destroy SDL presentation/window objects, then quit the subsystem owner.
- [ ] Handle suspend/resume and app termination on the console. Release held
  input and resume audio/presentation coherently; verify actual Vita events,
  not Android-only lifecycle assumptions.

#### SDL2 keys and the Vita menu bridge

The normal SDL2 key-event path already translates through `Sdl2_EngineKey`.
The Vita bridge calls `Keyboard_HandleKey` directly, including for arrow keys.
Enabling `MUGEN_SDL2` without adapting that bridge can feed large SDL2 keycodes
to the engine's legacy key arrays.

- [ ] Introduce one bridge boundary that accepts SDL2 keys and translates them
  exactly once before calling legacy handlers, or use explicitly named engine
  key constants there. Apply it to button and analog press/release paths.
- [ ] Preserve menu semantics per screen: confirm, cancel, Start/pause,
  character-select overrides and all six attack actions. Check press and
  release; the keyboard bridge must not create duplicate attack/pause events.
- [ ] Reset bridge axis/held state on focus loss, suspend and shutdown.
- [ ] Exercise F1/info, arrows, Return, Escape and any synthetic F2 bindings with
  array bounds instrumentation on a host harness. Intentionally bypassing the
  conversion must fail a test.
- [ ] Use the actual parser keys: `P1.UseKeyboard`, `P1.Joystick.Type`,
  `[P1 Keys]` and `[P1 Joystick]`. The README's old `P1.Joy.B1` example is not
  an accepted configuration format.

#### Joystick numbering and instance identity

SDL joystick button indexes are not engine binding codes. The current engine
uses `2 * axes + 4 * hats + 1 + raw_button` for a slot-relative button code,
then applies its slot/translated-input conventions.

The inspected SDL 2.32.8 Vita driver declares 16 buttons, six axes and zero
hats. This is a source fact to verify on the selected device/build, not an
on-device observation from this planning work.

| First-pad topology | Raw Triangle index in the inspected mapping | Corresponding slot-relative engine button |
|---|---:|---:|
| SDL1 topology recorded by the existing port: 4 axes, 0 hats | 0 | 9 |
| Inspected SDL2 Vita source: 6 axes, 0 hats | 0 | 13 |

- [ ] Query topology after opening the actual device. Derive binding codes from
  the same topology and slot policy as `Pads_HandleEvent`. Do not hardcode
  either offset, and do not store raw zero as a binding: zero is unassigned.
- [ ] Reuse the SDL2 instance-ID-to-slot lookup. Test an instance ID different
  from its enumeration index, held buttons during removal, and repeated init.
- [ ] Distinguish defaults from explicit user remaps. Establish precedence and
  config versioning; stop unconditionally overwriting a saved map at startup.
  Provide a documented migration/reset rule for existing incorrect raw maps.
- [ ] Record raw event, instance ID, slot, derived engine code and resulting
  action in the diagnostic mapper. A correct raw-index display alone is not
  proof of working menu or gameplay bindings.
- [ ] Check built-in Vita controls and any supported external controller/PSTV
  topology. Do not advertise hot-plug support until implemented and tested;
  if reconnect requires restart, state that limit.

Gate: all menu/fight actions work on hardware, no synthetic SDL2 key reaches a
legacy array unconverted, remaps survive restart, and logs distinguish raw
button IDs from engine action codes.

### M3 — Bring up SDL2 System presentation first

Primary files: `src/sdl2compat.c/.h`, `video.c`, `gfx.c` and a small
`port/psvita/src/vita_video.c/.h` adapter if separation needs a new interface.

- [ ] Keep CPU rasterization unchanged. Allocate its software surface at the
  configured game dimensions and honor actual pitch/format.
- [ ] Separate logical framebuffer size from SDL window/output size in bridge
  state. The desktop defaults must retain their behavior. Vita's fixed output
  does not justify making the CPU draw or upload a panel-sized frame.
- [ ] In SDL2/Vita mode bypass the SDL1-only forced panel surface, CPU
  `Vita_ScaleToVideo` path and double-buffer-flag workaround.
- [ ] Create the renderer and streaming texture once, update using the surface's
  real pitch, and present exactly once for each intended drawn frame. Recreate
  them only when dimensions/format/backend change.
- [ ] Query actual renderer output size. Apply nearest-neighbor scaling and
  `KeepAspect` policy in presentation. Do not rely on a requested window size
  being equal to the physical drawable.
- [ ] Force finished-frame uploads to be opaque; do not blend the completed
  software framebuffer a second time. Validate RGB565 and any required
  32-bit fallback with color ramps and alpha fixtures.
- [ ] Check supported texture formats at runtime. Use a measured, reusable
  conversion buffer only if the selected backend cannot upload the required
  format. No per-frame allocation or `zoomSurface` in the normal present path.
- [ ] Validate System mode using the chosen `sdl2_vitagl` provider as well as
  any optional ordinary-SDL2 diagnostic profile. The same package must be able
  to reach the System recovery path without a second native graphics owner.

Gate: startup, menus and a deterministic match present correct frames;
per-frame tracing shows one upload/present and no CPU panel upscale. An
improved FPS reading without matching pixels is insufficient.

### M4 — Qualify fonts, scaling, streams and audio

#### SDL_gfx / local scaling

- [ ] Reuse the bridge's local `zoomSurface` for SDL2 initially. Do not link
  SDL1 `SDL_gfx`, and do not also link SDL2_gfx's same exported function.
- [ ] Port the differential fixtures to the chosen ARM/SDL dependency set:
  indexed palettes, nonzero color keys, real alpha, RGB565/RGB555, 32-bit
  sources, integer and fractional ratios, padded pitches and blend/alpha mods.
- [ ] Decode comparisons with `SDL_GetRGBA` and actual masks. Compare actual
  transparency and state, not a forced alpha of 255 or only key presence.
- [ ] Retain the distinct indexed and 16.16 RGBA sampling rules. The earlier
  claim that SDL_gfx inherently swaps/loses 16-bit color was withdrawn.
- [ ] Treat existing desktop results as a baseline; run ARM fixtures before
  claiming Vita equivalence. If upstream SDL2_gfx is chosen later, repeat the
  same comparisons before replacing the local function.

#### SDL_ttf

- [ ] Compile all consumers against SDL2_ttf headers and link only its matching
  library. Record FreeType/HarfBuzz versions and font-file hashes.
- [ ] Verify initialization, font close/quit ordering, ascent, measurement and
  placement for every shipped font size/style/outline used by the fixture.
- [ ] Preserve foreground alpha initialization, palette padding, color key,
  alpha modulation and blend mode through scaled text and surface conversion.
- [ ] Cover solid and blended text, UTF-8 strings, newlines and fractional
  scaling in System and OpenGL modes. Check the OpenGL font-upload path too.
- [ ] Record glyph raster differences caused by dependencies separately from
  incorrect metrics/placement or opaque transparency. No broad visual exemption.

#### SDL_mixer, RWops and BGM

- [ ] Link SDL2_mixer and the in-tree `Mix_QuerySpecEx` adapter. Record the
  obtained rate, channel count, sample format and the requested chunk frames.
- [ ] Measure callback `len` separately. Expected bytes are
  `frames * channels * (SDL_AUDIO_BITSIZE(format) / 8)`. For 1024 stereo S16
  frames the expected 4096 bytes are ordinary unit conversion, not a 4x bug.
- [ ] Validate the whole callback output and a guard region: payload, partial
  buffers, underrun tail silence, pause, mute and resume. Test U8 silence as
  well as zero-valued signed-PCM silence where the engine supports the format.
- [ ] Reuse SDL2 RWops size/seek/read/write adapters with correct widths.
  `size` must not move the stream cursor. Cover EOF, failed seek, large/bounded
  offsets and archives, as well as ownership and errors.
- [ ] Audit `Mix_LoadMUS_RW(rw, 0)` lifetime for each actual music decoder:
  keep both RWops and underlying Stream alive for as long as decoding can
  use them; close each exactly once after music stops/frees. Do not assume a
  tracker always consumes all data synchronously.
- [ ] Test actual tracker/MIDI music independently from the custom ring-buffer
  hook. Supply required MIDI patch/config assets and report unavailable decoders.
- [ ] Exercise SND effects, mixing with BGM, channel volume/pan, loops, mute,
  pause, stop/replace and shutdown. Confirm audible output on Vita, not merely
  a successfully opened device.
- [ ] Audit callback/main-thread ownership and locking against the selected
  SDL2_mixer implementation. Quiesce callbacks before freeing shared storage;
  avoid file I/O, allocation and joins inside real-time callbacks.

BGM format policy must be explicit. The current built-in `sdlmix` subsystem
accepts tracker/MIDI files, not WAV/MP3/OGG. The current Vita dynamic-library
adapter cannot satisfy `mpg123` DLL loading, and a configured plugin init
failure can abort initialization of the BGM registry.

For initial compatibility testing, use a separately generated Vita fixture
config with usable subsystems and preserve the original config as a reference.
If WAV/OGG/MP3 BGM is a release requirement, add an explicitly Vita-only native
subsystem/adapter with extension registration, loop/seek/volume behavior and
ownership tests. Possible implementations are a properly integrated SDL2_mixer
music adapter or direct native mpg123 binding for the existing mpg123 API.
Codec availability alone does not select either engine path. Keep this
deliberate behavior addition separate from the SDL API conversion and never
change desktop plugin routing silently.

Gate: font/scaling fixtures pass their stated checks; audio callbacks pass
byte/guard tests; effects and at least one supported music path play audibly
on hardware with tested teardown. Any remaining format limitation is listed
by format and engine route, not concealed by a general “audio works” claim.

### M5 — Establish one working SDL2/vitaGL context and honest capabilities

Primary files: `src/sdl2compat.c`, `src/glrender.c`,
`port/psvita/src/vita_gl_shim.c/.h` and a CMake-built standalone GL probe.
Use the SDL2 provider selected in M1; do not build a competing context system.

- [ ] Create one fullscreen GL window, set a compatible GL 2.x profile before
  creation where supported, and query actual drawable dimensions afterwards.
  Do not request desktop GL 3.3, core-only GL or GLES3 as a substitute.
- [ ] Verify return values and actual hardware state, not only the singleton
  context token. Clear/read distinct RGBA colors, compile/link a minimal
  attribute shader, draw a gradient/checkerboard, and present multiple frames.
- [ ] Record GL version/renderer/vendor strings, shader compiler availability,
  maximum texture size, texture units, FBO/format checks and selected SDL driver
  in `mugen.log`. Detect a missing on-device shader compiler prerequisite.
- [ ] Parse both numeric-prefix and `OpenGL ES N.M ...` version strings in the
  Vita capability adapter. Test NPOT sampling with the actual repeat/filter
  combinations needed, not just a similarly named extension.
- [ ] Remove the irrelevant `ARB_fragment_program` gate only for the Vita
  programmable path, replacing it with successful GLSL compile/link/draw tests.
  Do not invent extension flags to bypass checks. Preserve desktop gates.
- [ ] Prove FBO completeness, attachment rendering, readback orientation and
  texture-unit independence before enabling engine OpenGL mode.
- [ ] Connect the selected vendored library's flags to the executable. Keep
  required compatibility features initially; disable risky speed hacks during
  correctness qualification and measure before re-enabling any.
- [ ] Audit the package driver's initialization return handling and singleton
  lifecycle. Its inspected context rebinding clears graphics state and deletion
  does not implement desktop-style independent contexts. Avoid redundant
  per-frame make-current calls and test mode changes, dialogs and resume.
- [ ] Make the probe fail on any shader or draw mismatch. Unsupported context,
  missing compiler and insufficient memory must produce distinct diagnostics.

The patched driver's initial legacy vertex pool is zero in the inspected
source. Do not assume legacy immediate-mode drawing is safe in this profile.
The new renderer must cover calibration, primitives and screen quads as well
as sprites before an entire game frame is considered supported.

Gate: the actual selected SDL2/vitaGL stack draws validated programmable
geometry on the console without a second initialization or hidden fallback.
The same probe built against ordinary SDL2 must reject an unavailable GL path
clearly rather than “pass” on a dummy surface.

### M6 — Implement the programmable MUGEN sprite renderer

Primary new files: `port/psvita/src/vita_gl_renderer.c/.h` and private
renderer state/texture metadata. Shared entry points stay in `glrender.c`,
`gldetect.c` and `prim.c` behind a Vita programmable-path condition.

#### A. Vertex submission and projective coordinates

1. Preserve the existing quad construction, triangle-strip order, sprite origin,
   top/bottom stretch, width anchor, shear, flips and angle expression order.
   Replace submission rather than algebraically rewriting the calculations.
2. Build four explicit positions and four-component texture coordinates.
   A suitable starting vertex is `position[4] + texcoord[4]` as floats.
   Position uses the original x/y/z and homogeneous w; ordinary UVs use Q=1.
3. For stretched quads retain the computed S/T/Q exactly. In particular,
   `Glr_QuadVertices`' bottom-edge Q values must reach the shader rather than
   the old two-component wrapper that discards them.
4. Bind/query `aPosition` and `aTexCoord` consistently before/after linking.
   Set explicit attribute sizes, offsets and strides and validate them against
   the pinned vitaGL implementation.
5. Submit `GL_TRIANGLE_STRIP` with `glVertexAttribPointer` and `glDrawArrays`
   through the custom-program path. Do not route shader geometry through
   `glBegin/glEnd` or fixed-function array submission.
6. Start with one submission per original draw. Avoid per-draw allocations.
   Use a bounded VBO/ring or other verified vitaGL-owned upload mechanism;
   do not reuse memory while queued GPU work can still read it. Establish the
   pinned library's buffer lifetime/orphaning rules instead of assuming desktop
   persistent mapping or unsupported fences.
7. Retain existing matrix calls initially and use the matrices supported by
   `HAVE_FFP_SHADER_SUPPORT`. Verify their upload to custom programs. If an
   explicit MVP uniform is necessary, obtain equivalent matrix values and
   preserve multiplication/order semantics; test before replacing anything.
8. Keep attribute/program caches outside `GlProgram`. Invalidate metadata on
   shader deletion, relink, context generation change and handle reuse.

#### B. Make both palette routes genuinely 2D

Preferred design for the new Vita renderer: store palette colors in a
256-by-1 RGBA `GL_TEXTURE_2D` and bind it on unit 1. Keep the normalized index
texture on unit 0, with verified red-channel sampling and upload semantics.

- [ ] Adapt both palette texture creation/upload and draw binding. Cover
  pre-existing texture palettes and the streamed `g_glSysTex` palette route.
  Changing only the fragment sampler declaration is insufficient.
- [ ] Preserve format conversion, palette alpha, unused-entry handling,
  nonzero transparent indexes and palette mutation behavior. Test SFF v1
  indexed sprites and the indexed/RGBA paths of supported SFF v2 content.
- [ ] Preserve index lookup corrections and calibration. Do not arbitrarily
  replace `INDEX_STANDARD/GEFORCE7/SAFE` selection with a new formula.
- [ ] Maintain alignment and real upload pitch. Use nearest palette filtering
  and appropriate edge clamping; preserve the engine's intended sprite filtering.
- [ ] Attach per-texture role/target metadata outside reconstructed structs.
  Never reuse one texture name with incompatible targets without re-creation.
  Invalidate metadata when an ID is deleted/reused.
- [ ] Avoid using `vglOverrideTexFormat(GL_TEXTURE_1D)` as a blanket fix:
  the inspected implementation changes global sampler-target selection, not
  only palette unit 1. Ordinary 2D index/RGBA textures must remain independent.
- [ ] Keep the old 1D path isolated for the SDL1 reference. The new Vita backend
  should not rely on 1D stubs, even if the vendored build enables compatibility.

#### C. Finish shader integration, including failure behavior

The existing four Vita shader assets are the starting point. They already
declare explicit attributes/varyings and a 2D palette sampler.

- [ ] Preserve the `////*EB_DEFINES*////` marker and the PalFX operation order,
  source-alpha multiplication, masking/discard behavior and PREMUL variants.
- [ ] Compile and link all variants actually selected by `GL_PROGRAM_COUNT`
  for indexed and RGBA programs. Log the shader name, defines and complete
  diagnostics to the ordinary Vita log, not only the optional match log.
- [ ] Check `GL_COMPILE_STATUS` and `GL_LINK_STATUS` explicitly. Current
  `Gl_LoadShader/Gl_LoadShaders` success returns do not establish that every
  required shader linked. Make failures fatal in the new strict renderer.
- [ ] Validate optimized-out uniform/attribute locations versus required
  bindings; do not silently draw with program zero or stale uniform state.
- [ ] Keep original desktop shader assets unchanged. Hash the actual shader
  files inside the VPK and log their version/hash at startup.
- [ ] Add dedicated simple textured/color shaders for presentation, calibration
  and untextured primitives where required by the full draw-path audit.

#### D. Convert every shader-dependent or legacy-pool-dependent draw

| Existing entry point / role | Required Vita implementation |
|---|---|
| `Gl_DrawQuadIndexed` | Explicit attributes, unit-0 indices and unit-1 2D palette |
| `Gl_DrawQuadRgba` | Explicit attributes, RGBA sampler and existing PalFX variants |
| `GlDetect_DrawQuad` / `GlDetect_TestIndexShader` | Same programmable submission and Vita-compatible calibration shaders |
| Definition of `g_glDetectVertexShader` and embedded `gld_*_shader` strings | Vita-specific attribute/varying and 2D-palette variants; four packaged shaders do not cover these |
| `GlScreen_Draw` | Explicit screen quad with correct UV extent and orientation |
| `prim.c` points, lines, loops and filled rectangles | Explicit color-program geometry, blend/scissor and measured pixel offsets |
| Final present/copy and render-target callbacks | Explicit textured/color draws, no reliance on a bound program being ignored |

Initialize the vertex submission and calibration programs before
`Gl_DetectOffsets` runs. Calibration occurs before normal shader loading in
the current startup order. Suppressing the calibration call or hardcoding
its result would hide the very defects it is intended to reveal.

Test all 256 palette indexes with distinct channel values, then flips,
fractional scaling, rotation, trapezoid/projective UVs, ortho/perspective
projection and clipped subregions. Use actual channel masks/readback formats;
a grayscale-only test cannot detect red/blue channel swaps.

Gate: indexed and RGBA sprites, text, calibration and primitives use valid
programs/attributes; a nontrivial transform/PalFX fixture fails if the custom
program is deliberately ignored or Q is forced to 1. No new-backend draw
depends on the legacy immediate-mode pool.

### M7 — Implement correct compositing and render-target behavior

#### Constant-alpha blending is mandatory

The old `MugenVita_BlendColor` wrapper only records its arguments. The pinned
vitaGL blend implementation does not implement the engine's constant-alpha
factor. Do not describe that as a completed compatibility feature or accept
an additive approximation.

For the engine's `GL_ONE, GL_CONSTANT_ALPHA` path, after the existing source
shader/PalFX/premultiplication, the relevant component operations are:

- Add: `out = source + destination * alpha_dst`.
- Reverse subtract: `out = destination * alpha_dst - source`.

Match the original blend equation, clamp/format conversion, masks, coverage
and alpha-channel handling; do not multiply source alpha twice. The existing
`ONE_MINUS_SRC_ALPHA` optimization when its precondition holds remains a
separate ordinary blending case.

Implementation decision:

1. First verify whether the pinned native stack has an exact usable facility
   for these factors. Do not assume an unimplemented GL constant becomes
   supported because its enum is declared.
2. If not, implement a shader-compositing path with a destination snapshot in
   a distinct texture/FBO. Preserve the correct pre-draw destination for every
   affected draw, including render-to-texture targets.
3. Render into offscreen color targets when required to obtain that snapshot;
   copy/ping-pong without sampling the texture currently attached for writing.
   Preserve pixels outside the affected geometry and honor scissor/coverage.
4. Use the actual target dimensions/orientation when sampling destination
   coordinates. Restore texture units, program, framebuffer, viewport,
   attributes, blend and color/depth masks afterwards.
5. Validate correctness before optimizing snapshots to dirty rectangles or
   specialized passes. CPU readback is acceptable for a diagnostic reference,
   not as the normal per-sprite solution for the shipping GPU renderer.

Test source/destination alpha values 0, intermediate and 1; transparent-index
discard; premultiplied RGBA; PalFX; overlapping draws; add/subtract; clipping;
FBO targets; and alpha values reused after ordinary blending. Keep independent
expected images and full-pixel comparisons. Making the blend-color handler
a no-op must cause a test failure.

#### FBOs, copies and nested render targets

- [ ] Cover `Gl_AttachFramebuffer` and the render-target creation/deletion
  paths, including `Sprite_DrawGL`'s intermediate sprite buffer.
- [ ] Check color/depth attachment compatibility and framebuffer completeness.
  Distinguish the logical game target from the physical default framebuffer.
- [ ] Preserve orthographic/perspective orientation and the existing difference
  between window and offscreen target coordinates. Test non-square targets.
- [ ] Test nested target restoration: dimensions, viewport, projection/modelview,
  clip rectangle, program and blend state must all be restored.
- [ ] Qualify `glReadPixels` for capture/calibration, padded rows and channel
  order. Keep readback out of normal frame submission.
- [ ] Test deletion of currently/recently bound resources, shader/texture ID
  reuse, failed allocations and repeated scene transitions.
- [ ] Make failed FBO or shader construction unwind safely. Do not leave stale
  metadata, flags indicating a valid target, or an incomplete target bound.

Gate: every supported sprite blend operation has a correct test result;
intermediate targets and screenshots are correct; no texture feedback hazard
or resource lifetime failure occurs in repeated on-device scenarios.

### M8 — Integrate cfg selection and measure the real renderer

- [ ] `RenderMode=OpenGL` must select the programmable Vita renderer, complete
  calibration/shader creation and remain OpenGL during the scenario.
- [ ] Package the requested `MUGEN_VITA_RENDERMODE` value and correct shader
  assets. Log requested renderer, selected renderer, SDL provider, dimensions
  and fallback reason. With strict mode ON, fallback is an explicit failure.
- [ ] Qualify `OpenGLScreen` separately using its new screen quad. It must not
  be reported as the accelerated indexed-sprite path.
- [ ] Exercise System -> OpenGL -> System and supported same-mode changes.
  If the pinned singleton driver cannot safely change modes, fix its lifecycle
  or explicitly require restart for that transition; do not claim it tested.
- [ ] Verify in-place video option changes, dialogs, suspend/resume, missing
  shader files and allocation failure. Preserve the selected user's config.
- [ ] Measure the same deterministic scenes and settings as the baseline.

Performance instrumentation must separate:

| Metric | Interpretation |
|---|---|
| Logic tick and rendered/presented frame counts | Whether logic and rendering are keeping pace; neither proves the bottleneck alone |
| CPU sprite/raster/transform time | Remaining CPU renderer work and engine work |
| Vertex upload and texture upload bytes/time | Streaming bandwidth and avoidable repeated asset upload |
| Draw submissions and state/program changes | Initial per-draw implementation cost and later batching opportunities |
| Native present/swap wait | Includes synchronization/vsync; not automatically pure rendering cost |
| Median/p95/p99 frame duration | Stutter and sustained frame pacing rather than one FPS sample |
| Heap, GPU/shared allocations and resource counts | Memory headroom and leaks across scenes/resume |

The 60 Hz frame budget is about 16.67 ms; 45 fps is about 22.22 ms per frame.
Do not infer a 20 ms deficit or “presentation is the only problem” from tick
and draw counters. Use measured stage timings and GPU synchronization evidence.

After correctness, optimize persistent texture/palette caching, dirty palette
updates, VBO reuse and contiguous state-compatible draw groups. Do not reorder
transparent sprites. Flush before any matrix, uniform, target or clip change
that would alter queued geometry. Start without speculative speed hacks.

Gate: report measured frame-time distributions and memory at unchanged game
settings. No fixed performance multiplier or guaranteed 60 fps is promised.
The OpenGL result must show actual GPU sprite draws and correct pixels.

### M9 — Qualify, document and switch the Vita default

- [ ] Pass the matrix below on the target hardware and retain the matching ELF
  and core dumps for any failure.
- [ ] Run existing host SDL2 shim/input/audio and differential-scaling checks
  after shared-source changes. Use their supported Windows/Linux harnesses;
  do not execute cross-compiled Vita binaries on the host.
- [ ] Run the relevant Windows/MSVC, MinGW and Linux regressions when shared
  code changes, as required by AGENTS. Root Makefile is used only for those
  existing host targets. Add no Vita build logic to it.
- [ ] Refresh README instructions and ISSUES statuses from measured results.
  Remove contradictory current/default renderer and memory descriptions.
  Do not classify every post-gameflow crash as game-data-only.
- [ ] Document actual package pins, shader compiler requirements, supported
  BGM routes/formats, remapping, logs, fallback and any restart requirement.
- [ ] Preserve dependency notices and the source/patch/build information needed
  to reproduce the statically linked libraries. Do not package reference test
  executables or unrelated game-tree files into the release VPK.
- [ ] Only after the OpenGL hardware gate, change the Vita backend default to
  SDL2 and the qualified Vita renderer/config default as a separate commit.
  Keep explicit System recovery and SDL1 reference instructions.
- [ ] Remove obsolete SDL1 Vita dependencies/workarounds only in a later cleanup
  once their replacement coverage is established. Do not remove desktop SDL1
  support as part of this Vita-specific migration.

Gate: the release manifest identifies what was actually tested, OpenGL stays
active with correct output, regressions are accounted for and the previous VPK
can be restored. Mark unsupported/unverified scenarios individually.

## 5. Validation matrix and evidence

### 5.1 Required scenarios

| ID | Scenario | Required check |
|---|---|---|
| B01 | Clean SDL1 and SDL2 configurations | Separate caches/objects; reproducible SELF/VPK; expected provider in link map |
| B02 | Missing/wrong SDL header, host archive or mixed SDL majors | Clear configure/link rejection, not an accidental ABI-compatible build |
| B03 | Vendored vitaGL selected | Final map uses the selected implementation once; selected flags/source recorded |
| V01 | SDL2/System startup and mode select | Correct surface/pitch/colors, GPU presentation, no CPU panel upscale |
| V02 | Explicit OpenGL startup with strict mode | Correct context, shaders, calibration and actual renderer; no fallback |
| V03 | OpenGLScreen | CPU frame uploaded once; correct UVs/orientation and separate performance label |
| V04 | Configured game sizes and aspect settings | Logical coordinates unchanged, correct panel fit/letterbox/stretch |
| V05 | Palette indexes 0..255 and live palette updates | Correct RGB/alpha/index correction; distinguish colored channels and transparency |
| V06 | RGBA, PalFX and transparency | Correct PREMUL, masking, add/mul/invert/color balance and alpha |
| V07 | Flips, rotations, shear, width ratio and perspective | Four-component UV/Q retained; correct geometry and interpolation |
| V08 | Primitive lines/points/rectangles and text | Measured pixel offsets, clipping and state restored between shader kinds |
| V09 | Constant-alpha add/reverse-subtract | Full independent image comparisons, including destination alpha and overlap |
| V10 | FBO sprite buffering, capture and nested target restore | Complete attachments, correct orientation and no feedback/lifetime defect |
| I01 | Physical buttons/sticks in menus and fights | Raw -> instance/slot -> engine code -> action verified, no key-array overrun |
| I02 | Character-select confirm/back and standing preview | Correct per-screen actions; baseline preview crash investigated and candidate stable |
| I03 | Saved remaps, focus loss and reconnect policy | Settings survive restart; no stuck key; reconnect behavior documented/tested |
| F01 | Solid/blended/scaled UTF-8 text | Metrics, placement, actual alpha and scaling-state comparisons |
| A01 | Custom callback and actual audio device | All output/guard bytes, units, requested vs obtained spec and measured callback length |
| A02 | Supported tracker/MIDI BGM plus SND effects | Audible music/effects, loops, volumes/pan, pause/mute and no clipping/teardown crash |
| A03 | Configured unsupported/missing BGM route | Accurate diagnostic; no claim that codec installation fixes routing |
| L01 | Startup failure at each initialization boundary | Clear error/log, cleanup once and no second graphics owner |
| L02 | Repeated scene changes and mode changes | Resource counts stabilize; singleton lifecycle limits handled explicitly |
| L03 | Suspend/resume, dialog, exit and reinstall | No lost input, stale GL objects, audio-after-free or corrupted saved config |
| P01 | Fixed scenes at unchanged settings | Frame-time distribution, upload/draw data and memory; not just average FPS |

### 5.2 Compare the right reference

Use three references with distinct purposes:

1. SDL1 Vita/System versus SDL2 Vita/System for presentation/input/audio
   migration behavior at the same ARM/game settings.
2. Desktop reference OpenGL and independent synthetic expected images for the
   new Vita OpenGL renderer's palette, transforms, PalFX and blending.
   A CPU RGB565 System image is not the same precision oracle as RGBA GL.
3. Repeated runs of the same binary/scenario to identify timing/RNG variability
   before attributing a difference to SDL2.

Use deterministic inputs/scenes and identical durations. Where possible,
capture controlled input and RNG state through a test harness without changing
shipping gameplay behavior. AI/attract runs are useful stability tests; do not
claim exact log equivalence for uncontrolled nondeterministic runs.

Normalized logs check initialization/game-flow behavior, not visual or audible
equivalence. Compare framebuffer pixels, resource state, input actions and
actual sound separately. Exact comparisons are required for integer palette
selection, transparent masks and CPU scaling fixtures. Any GL precision
tolerance must name the fixture, channels, differing pixels and reason; no
blanket exemption for an entire format, frame or renderer. Check that images
are not empty/constant and geometry headers match.

### 5.3 Tests must be capable of failing

Introduce focused mutations while developing the tests, then restore them:

| Mutation | Expected failure |
|---|---|
| Bypass synthetic key translation | Bounds/key-identity assertion for Vita bridge input |
| Store raw button index instead of derived engine code | Wrong-action/unbound-action assertion for first and nonzero pad slots |
| Force texture Q=1 or ignore custom program | Projective geometry/PalFX pixel mismatch |
| Select a palette as the wrong texture target/unit | Distinctive palette color/index mismatch |
| Make blend color ineffective | Constant-alpha image mismatch |
| Shorten underrun silence or write beyond callback len | Whole-buffer or guard-region assertion |
| Remove/change RWops size or close a streaming RWops early | Size/position or lifetime test failure |
| Force System fallback in strict OpenGL test | Renderer identity failure even if menus remain visible |
| Package an old shader/config after a source change | VPK hash/content assertion |

These are tests of actual externally visible behavior and resource contracts,
not tests that merely reproduce an implementation formula.

### 5.4 Evidence layout

Keep generated files inside the repository, under a directory such as:

```text
build/psvita-validation/<engine-sha>/<profile>/
  manifest.json
  configure.log
  link.map
  packed-config.cfg
  shader-hashes.txt
  mugen.log
  frames/
  audio/
  frame-times.csv
  memory.csv
  core-dumps/
  results.md
```

Record whether each result is source inspection, host test, cross-build,
emulator observation or actual Vita hardware. Identify console model,
controller topology, firmware, power/clock conditions and test duration.
Keep unstripped ELF files matching any crash dump and rebase addresses using
the module's actual load base. Delete disposable scratch outputs when finished;
retain reproducible evidence needed for a review.

## 6. Memory, lifetime and performance budgets

Keep `MUGEN_VITA_MEMSIZE=0` (normal application) and the existing 64 MiB newlib
heap as the starting profile. SDL2, shader compilation, decoded assets and GPU
allocations still share finite resources; do not claim that normal-app memory
is unlimited or raise the heap to hide an unexplained allocation failure.

At 960x544 an unaligned RGBA8 color image is 2,088,960 bytes (about 1.99 MiB).
Two such color images need about 3.98 MiB before depth, staging, alignment and
driver allocations. A 256-entry RGBA palette is 1024 bytes before overhead.
Use actual allocation sizes and peak decoder/shader-compiler usage in the
manifest; do not equate texture payload bytes with total process memory.

- Track logical game targets, destination snapshots, depth buffers, VBO rings,
  texture/palette caches, font surfaces and decoder buffers independently.
- Cap caches and handle OOM without use-after-free or half-valid objects.
- Do not allocate full-frame temporary surfaces each present or full copies of
  an entire SFF asset set merely to change SDL version.
- Verify asynchronous GPU resource lifetime and audio callback lifetime before
  introducing pooling/reuse.
- Stress repeated character loads, previews, round transitions, shader changes
  and suspend/resume. A stable menu memory count is not a leak test for fights.
- Change heap/GXM reservations only after measurements explain the tradeoff.

## 7. File/change map

Paths marked “new” are proposed; they do not exist in the inspected baseline.

| File/area | Planned change |
|---|---|
| `port/psvita/CMakeLists.txt` | Backend/provider selectors, target flags/deps, source selection, tests, cfg/shader packaging and evidence |
| `port/psvita/cmake/SdlBackend.cmake` (new) | Isolated SDL discovery, ABI checks, static dependency normalization and provider summary |
| `port/psvita/cmake/VitaGl.cmake` | Reproducible target selection and build flags; final link ownership |
| `port/psvita/src/vita_video.c/.h` (new if needed) | Vita output/logical geometry and presentation lifecycle policy |
| `port/psvita/src/vita_gl_renderer.c/.h` (new) | Vertex/program/texture metadata, draw submission and compositor |
| `port/psvita/src/vita_input.c/.h` (new if extracted) | Target-specific menu bridge and default binding policy |
| `port/psvita/src/vita_gl_shim.c/.h` | Honest capabilities; replace incomplete blend/texcoord behavior for the new path |
| `port/psvita/shaders/*` | Existing asset qualification plus calibration, color/copy/compositor programs |
| `port/psvita/tests/` (new) | CMake-built device probes, render/input/audio fixtures and result capture |
| `port/psvita/input-mapper/*` | Matching SDL2 build/presentation and raw-to-action diagnostics |
| `src/mugen.h`, `sdl2compat.c/.h` | Reused SDL2 API bridge with narrow hooks for Vita policy |
| `src/video.c`, `gfx.c`, `system.c` | Split platform/SDL1 policy, lifecycle and strict renderer selection |
| `src/glrender.c`, `gldetect.c`, `prim.c` | Guarded programmable dispatch and real compile/link/calibration results |
| `src/events.c`, `pads.c`, `keyboard.c`, `config.c` | Correct key/binding domains, topology, remap persistence and release handling |
| `src/error.c`, `main.c`, `log.c` | SDL2 startup/diagnostics and reliable target-specific cleanup/log paths |
| `src/font.c`, `stream.c`, `sound.c`, `bgmplugins.c`, `sdlmixercompat.c` | API/format/ownership verification and scoped fixes where tests demonstrate a gap |
| `third_party/vitagl/VENDORED.md` and dependency recipe record | Exact source/flags/patch provenance, including any scoped SDL driver patch |
| `port/psvita/README.md`, `ISSUES.md`, this plan | Current build instructions and evidence-based status |
| Root `Makefile` / desktop build entry points | No Vita implementation changes |

Do not perform a broad refactor merely to move files. Extract port helpers
where it clarifies ownership, while retaining original entry points and
address comments in shared modules.

## 8. Build/run instructions and rollback

### 8.1 Existing SDL1 reference build — supported now

From the repository root in a VitaSDK-configured shell:

```sh
cmake -S port/psvita -B build/psvita-sdl1-reference -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DMUGEN_VITA_RENDERMODE=system \
  -DMUGEN_VITA_DATA_DIR="/path/to/mugen-1.1b1"

cmake --build build/psvita-sdl1-reference
```

Retain the resulting VPK/ELF and its manifest. Existing SDK dependency
requirements still apply. No root `make` command builds this Vita target.

### 8.2 Planned SDL2/OpenGL profile — only after M1 implements these options

```sh
cmake -S port/psvita -B build/psvita-sdl2-gl -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DMUGEN_VITA_SDL_BACKEND=2 \
  -DMUGEN_VITA_SDL2_PROVIDER=vitagl \
  -DMUGEN_VITA_GL_RENDERER=programmable \
  -DMUGEN_VITA_BUILD_VITAGL=ON \
  -DMUGEN_VITA_RENDERER_STRICT=ON \
  -DMUGEN_VITA_BUILD_TESTS=ON \
  -DMUGEN_VITA_DEPS_PREFIX="/path/to/vita-sdl2-vitagl-prefix" \
  -DMUGEN_VITA_RENDERMODE=opengl \
  -DMUGEN_VITA_DATA_DIR="/path/to/mugen-1.1b1"

cmake --build build/psvita-sdl2-gl
```

The planned packed configuration must contain:

```ini
[Video]
RenderMode = OpenGL
FullScreen = 1
```

Keep the chosen `[Config] GameWidth/GameHeight` intact. Verify the VPK's
actual configuration and shader files, not just CMake cache values. These new
selectors are not available merely because they appear in this plan.

For a separate SDL2 System test, use another build directory and request
`MUGEN_VITA_RENDERMODE=system` with the same chosen SDL2 provider. The cfg
renderer and compiled renderer capability must be consistent.

### 8.3 Rollback

Reinstall the preserved SDL1 reference VPK or rebuild its recorded commit and
dependency prefix. Restore a saved user cfg only when the test changed it,
without deleting unrelated game data or remaps. A failed strict OpenGL test
does not justify silently changing the user's requested mode to System.

## 9. Suggested implementation/review sequence

| Change | Content | Required evidence before proceeding |
|---|---|---|
| 1 | M0 manifest and dependency/context probes | Reproducible baseline and correct provider identity |
| 2 | M1 CMake backend isolation and final link selection | Clean SDL1/SDL2 cross-builds and audited link maps |
| 3 | M2 startup, diagnostic and input-domain fixes | Host bounds tests and Vita raw-to-action mapping |
| 4 | M3 System presentation | Correct on-device System frames without CPU upscale |
| 5 | M4 font/scaling/audio/RWops qualification | Fixture results plus audible supported paths |
| 6 | M5 context and capability checks | Functional GL probe, no false capability/fallback pass |
| 7 | M6 vertex/palette/shader/calibration/primitives implementation | Synthetic pixel results and engine GL startup |
| 8 | M7 exact blending and FBO/lifetime handling | Compositing, capture and target-restoration results |
| 9 | M8 integration/performance and transition tests | Complete scenario matrix and measured budgets |
| 10 | M9 default switch and documentation | Qualified VPK, manifest and tested rollback |

M4 can be developed alongside later graphics work once the platform foundation
is stable, but it remains a release gate. All shared-source changes need host
regressions; a Vita-only CMake edit does not require rebuilding unrelated
platforms just to validate documentation.

## 10. Primary external references

Inspected recipes and their backend patch are pinned here rather than relying
only on package names or mutable screenshots of an installed SDK:

1. [VitaSDK SDL2 recipe](https://github.com/vitasdk/packages/blob/de769c80482cc3e53cc16b71cb4bb27a323e5a31/sdl2/VITABUILD).
2. [VitaSDK SDL2/vitaGL recipe](https://github.com/vitasdk/packages/blob/de769c80482cc3e53cc16b71cb4bb27a323e5a31/sdl2_vitagl/VITABUILD)
   and [backend patch](https://github.com/vitasdk/packages/blob/de769c80482cc3e53cc16b71cb4bb27a323e5a31/sdl2_vitagl/vitagl-backend.patch).
3. [SDL 2.32.8 Vita joystick implementation](https://github.com/libsdl-org/SDL/blob/release-2.32.8/src/joystick/vita/SDL_sysjoystick.c):
   declared topology and raw mapping; validate the selected binary on hardware.
4. [VitaSDK SDL2_mixer recipe](https://github.com/vitasdk/packages/blob/de769c80482cc3e53cc16b71cb4bb27a323e5a31/sdl2_mixer/VITABUILD),
   [SDL2_ttf recipe](https://github.com/vitasdk/packages/blob/de769c80482cc3e53cc16b71cb4bb27a323e5a31/sdl2_ttf/VITABUILD)
   and [SDL2_gfx recipe](https://github.com/vitasdk/packages/blob/de769c80482cc3e53cc16b71cb4bb27a323e5a31/sdl2_gfx/VITABUILD).
5. [Pinned vitaGL immediate-mode implementation](https://github.com/Rinnegatamante/vitaGL/blob/9bf3e3f4e55efd1f4a191b179144f7f9a98cd925/source/ffp.c),
   [custom shaders](https://github.com/Rinnegatamante/vitaGL/blob/9bf3e3f4e55efd1f4a191b179144f7f9a98cd925/source/custom_shaders.c),
   [blending](https://github.com/Rinnegatamante/vitaGL/blob/9bf3e3f4e55efd1f4a191b179144f7f9a98cd925/source/blending.c)
   and [runtime prerequisites](https://github.com/Rinnegatamante/vitaGL/blob/9bf3e3f4e55efd1f4a191b179144f7f9a98cd925/README.md).
6. [SDL2_mixer RWops music ownership](https://wiki.libsdl.org/SDL2_mixer/Mix_LoadMUS_RW).

Recheck pins and installed binaries before implementation. Availability of a
newer package is not evidence that its ABI, driver lifecycle, shader support
or output is equivalent to the stack inspected for this plan.
