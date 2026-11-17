# Upstream KFM ZSS compatibility fixture

Status: **PLAYABLE in the qualified Linux x86-64 numeric/2D profile.**
Windows, Android and Vita execution remain OPEN.

The fixture preserves the upstream character and its assets byte for byte.
Fetch/install it with Python 3 (use `python` instead of `python3` on Windows
when that is the installed interpreter):

```sh
python3 tools/fetch_zss_kfm.py
python3 tools/fetch_zss_kfm.py --check
```

The default destination is `install/`: 16 files under `chars/kfm_zss/` and the
referenced `data/common1.cns.zss`. Use `--game-dir build/my-test-game` for a
separate test tree. Paths must stay inside the repository. The fetcher verifies
sizes and SHA-256 hashes, caches downloads under `build/downloads/`, and refuses
to overwrite modified fixture files. `--offline` uses already verified cache
files; `--check` reads installed files without writing/downloading. Base game
assets are provided separately by the normal build setup / fetch_game_data.sh.

Source provenance is recorded per file in
[tools/fixtures/zss_kfm.json](../tools/fixtures/zss_kfm.json):

| Content | Pinned source |
|---|---|
| Character, scripts, SFF/SND, AIR, CMD, constants and storyboards | [Ikemen-GO-Screenpack 91e9ff0cd21def4cf1fe2780e7709e2243b82bc9](https://github.com/ikemen-engine/Ikemen-GO-Screenpack/tree/91e9ff0cd21def4cf1fe2780e7709e2243b82bc9/chars/kfm_zss) |
| External common-state dependency | [Ikemen-GO b55af1a13c128c16b4d4331f8f1fe67dcd4f06df](https://github.com/ikemen-engine/Ikemen-GO/blob/b55af1a13c128c16b4d4331f8f1fe67dcd4f06df/data/common1.cns.zss) |

There are 17 files / 637958 bytes. The manifest also retains Git blob IDs;
these were checked against the downloaded bytes when adding the fixture.
The original `readme.txt`, including KFM's noncommercial license statement,
is included. The binaries/assets remain downloaded test data rather than
being embedded into this source repository.

## Running the compatibility test

From a complete game installation, select the original character explicitly:

```sh
# Linux, after building:
cd install
../build/linux-x64-sdl2/mugen-linux-x64 -nosound -p1 kfm_zss -p2 kfm
```

Use the actual Makefile BUILD path if overridden. On Windows the equivalent
executable is `mugen-mingw-win32-sdl2.exe` or `mugen-mingw-win64-sdl2.exe` in
its build directory. The character definition remains upstream's
`chars/kfm_zss/kfm_zss.def`; tests must not silently replace its primary state
file or remove unsupported controllers to report a pass.

Qualified on Linux x86-64 with MUGEN_MODEL3D=0, an offscreen SDL2 System
renderer, copied game assets and sound disabled. The original primary/additional/
common scripts and CMD all load. Keyboard events produce walking, jumping,
crouching, standing punches and kicks, and actual opponent damage. AI KFM versus
CNS KFM and AI KFM versus ZSS KFM both reach a real KO and round completion before
normal shutdown. AI execution includes special/super states, guard/hitpause,
fall/recovery, throws/custom states and the common function library. This is
playability evidence for this pinned character, not complete Ikemen compatibility.

Run the reproducible native gate on a Linux development machine with the normal
engine dependencies and complete game/fixture installation:

```sh
sh tools/test_zss_kfm.sh -j4
# Optional normal Makefile dependency overrides can follow the script name.
```

The gate links `test_zss_kfm_runtime.c` with `--wrap=Round_Tick` into a separate
test executable. It observes real state IDs, animation IDs, positions, life and
hitpause, injects SDL keyboard events, requests screenshots and supplies a frame
deadline through the engine's existing exit mechanism. It does not force state
changes, damage, power, control, or scripted results. Python rejects missing
movement/attack states, missing damage, missing AI specials, a missing KO/round
finish, errors and unclean exits. An ordinary production executable cannot pass
this instrumented gate because it does not emit the required runtime CSV.
Results/logs/screenshots stay under `build/tmp/zss-kfm-runtime/results/`.
Production engine sources, installation dimensions and pinned fixture files
remain unchanged by the gate. Only disposable copies select System/windowed,
Precache=0 and explicit P1 bindings.

The implemented profile has explicit limits:

- The MUGEN motif has no enabled Ikemen dialogue subsystem. Validated literal
  `Dialogue` text takes the reference's disabled-dialogue path; no dialogue UI
  is claimed. `ForceFeedback` follows the existing engine's no-rumble policy.
- Stages are 2D. ZSS `pos z` reads zero. An Explod third position component may
  only be literal zero or that pure zero-depth read; nonzero/effectful depth
  expressions are rejected. No depth gameplay is claimed.
- Positive duplicate states resolve first-wins across ZSS files; negative ZSS
  bodies append in file order. Duplicate states within a file are errors.
  Mixed CNS/ZSS negative-state composition is still rejected explicitly.
- Persistence currently accepts 0..255 with up to 500 positive-state block
  counters; loop/switch persistence attributes, first-class strings and other
  unsupported Ikemen facilities remain errors. Original fatal allocation
  behavior in dynamic Expr_Clone/MulFloat defaults is still inherited.
- Audible output, hardware controllers, Windows 32/64, Android and Vita builds/
  device execution are not qualified by this Linux/no-sound combat test.

Earlier sections of the implementation plan retain the historical failed probes;
they are superseded by its latest playability qualification entry.

Independent native loader fixtures cover primary, character-local common and
File_Find-resolved common `.zss` files, plus malformed primary/common sources and
the unchanged CNS route. A seventh native fixture covers functions, recursive
numeric calls, result binding, first-wins cross-file resolution, function-only
additional files and controller/state-change dispatch inside functions. Run
those bounded Linux smoke tests against a built
binary and a complete source game tree:

```sh
python3 tools/test_zss_native_loading.py \
  --binary build/linux-x64-sdl2/mugen-linux-x64 --game-dir install
```

The runner changes only disposable copies of the game and preserves display
dimensions. An eighth, sound-enabled scenario generates a small PCM SND effect,
plays it from a function using local arguments and the real controller adapter,
and checks the existing sound-cache CSV for decode/prepare/successful mixer-start
events. It uses SDL's dummy audio driver; audible output is not qualified.
A ninth scenario calls all three PalFX variants from a function and checks
parameter side effects before reaching success state 65001. The host suite
checks exact player/background/global definitions and untouched destinations;
the native case does not assert screenshot pixels or OpenGL/GLES equivalence.
Three more native cases bring the suite to twelve: human and CPU map execution
through function locals and the real controller runner, plus rejection of a
dynamic DEF default. Success requires reset/default restoration, a single value
side effect, case-folded lookup, redirected opponent isolation and state 65001.
The CNS-only case also proves that nonnumeric `[Map]` data remains ignored.
Host cases cover typed storage, integer wrap, helper/default isolation, generation
reuse, bounded storage, cloned key ownership and allocation-failure cleanup.
Full-round map persistence, real helper lifecycle and concurrent precache remain
unqualified. See the latest implementation record for the exact numeric subset.
A thirteenth scenario calls AssertSpecial from a function, verifies that
noautoturn suppresses automatic turning for a tick, and requires turning to
resume when the flag expires. Both a missing-write mutant and a sticky-flag
mutant fail. Its synthetic state 0 replaces only the duplicate in a copied
common file; the production collision guard and upstream fixture are intact.
Host tests check all twenty player/global flag destinations and safe rejection
of long flag names, unsupported flags and unsupported parameters.
A fourteenth scenario injects a real SDL Down event after state 0 starts, checks
that holddown stays visible while nocrouch blocks automatic crouch, and requires
automatic crouch after expiry plus a scripted crouch while the flag is set.
Both removing the input consumer and making the flag sticky fail this case.
The Linux-only LD_PRELOAD event companion uses public SDL calls and the existing
trace and requires dynamically linked SDL2; it adds no production test hook.
A separate sanitizer harness exercises
Player_UpdateInput with recording command/state-entry boundaries. Native paused-
match, hitpause, helper lifecycle and other platform execution remain open.
F/common-bank selection has host boundary coverage rather than an independently
traced native common-sound playback. ZIP stream lookup/ownership is checked at the host harness boundary;
an end-to-end zipped character run remains open (QuickVS accepts loose DEF paths,
not the roster's ZIP-entry notation).

## Dependencies exposed by this corpus

| Work | Why it is needed |
|---|---|
| Primary and common-state ZSS loading | Implemented; all primary, additional and common files in this pinned corpus load |
| Functions and calls | Numeric functions/calls, frames, result bindings and first-wins cross-file lookup implemented |
| Block attributes | Implemented for the corpus, with the profile bounds above |
| More controller payloads | Implemented numeric combat/effect payloads used by this corpus; other controller schemas remain explicit errors |
| Explicit Ikemen extensions | Numeric `aiLevelF`, static `map(...)`, self MapSet/MapAdd/MapReset and numeric defaults implemented; dynamic/indexed/string maps and other extensions remain rejected |

Keep this corpus unchanged as an integration target. Continue adding isolated
regression fixtures for new semantic features and rerun the complete combat gate.
The earlier synthetic CNS-KFM switch/loop cases remain independent regression
coverage rather than substitutes for this upstream character.

Fetcher validation performed here: verified-cache installation, `--check`,
idempotent installation, and refusal to overwrite a deliberately modified file
in a disposable test tree. The installed upstream files still pass all 17 hashes.
