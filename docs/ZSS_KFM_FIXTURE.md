# Upstream KFM ZSS compatibility fixture

Status: **BLOCKED — not playable with the experimental compiler yet.**

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

Observed on Linux x86-64, MUGEN_MODEL3D=0, an isolated offscreen System-renderer
game copy, with no sound:

```text
Loading character chars/kfm_zss/kfm_zss.def...
Loading cmd command set kfm.cmd...OK
Loading cns kfm.const...
Character kfm_zss.def failed to load
ZSS kfm.zss:63:1: Controller not yet enabled in the ZSS bridge
```

The latest probe (2026-11-17) exited **1**, rather than reaching gameplay.
Primary `st`, additional `st0`..`st9` and `stcommon` dispatch explicit `.zss`
suffixes through the ZSS compiler. Functions/calls and PlaySnd's numeric/local
parameters and static F/S sound prefixes are implemented. PalFX, BGPalFX and
AllPalFX now support the shared numeric payload and local frame. EndState,
SysSnd, CSnd and EXFX compile. Numeric aiLevelF and static map lookup/mutation/
reset/defaults are implemented; the walk-state conditions at line 41 now compile.
The first stop is line 63, `AssertSpecial{flag:nowalk;flag2:noautoturn}`.
This replaces the aiLevelF failure at line 41, PalFX at line 34, PlaySnd at
line 24, unsupported functions at line 16 and the earlier misleading CNS error
at line 2559. No upstream script was edited to get past these failures.
This remains a failed compatibility probe,
not PASS/SKIP or a test of playable ZSS combat. The upstream character's additional
and common files are not reached in this run.

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
F/common-bank selection has host boundary coverage rather than an independently
traced native common-sound playback. ZIP stream lookup/ownership is checked at the host harness boundary;
an end-to-end zipped character run remains open (QuickVS accepts loose DEF paths,
not the roster's ZIP-entry notation).

## Dependencies exposed by this corpus

| Work | Why it is needed |
|---|---|
| Primary and common-state ZSS loading | Implemented and covered by isolated fixtures; compiling this character's common file remains blocked by its earlier primary-source failure |
| Functions and calls | Numeric functions/calls, frames, result bindings and first-wins cross-file lookup implemented |
| Block attributes | Sources use `persistent(0)` and `ignorehitpause` |
| More controller payloads | PlaySnd and the three PalFX variants implemented; combat/effect controller coverage remains incomplete |
| Explicit Ikemen extensions | Numeric `aiLevelF`, static `map(...)`, self MapSet/MapAdd/MapReset and numeric defaults implemented; dynamic/indexed/string maps and other extensions remain rejected |

The remaining unsupported rows are source-inspection requirements. AssertSpecial
is the first observed compiler blocker; later files have not compiled successfully.
Keep the corpus unchanged as an integration target. Add small isolated regression
fixtures for each missing semantic feature, then rerun this complete character.
The successful switch/loop smoke fixture uses the original CNS KFM with added
experimental states; it does not qualify this upstream ZSS character.

Fetcher validation performed here: verified-cache installation, `--check`,
idempotent installation, and refusal to overwrite a deliberately modified file
in a disposable test tree. The installed upstream files still pass all 17 hashes.
