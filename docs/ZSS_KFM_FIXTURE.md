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
Need at least one state controller
Error in [StateDef -2]
Error in kfm.zss:2559
```

The process exited **1**, rather than reaching gameplay. Inspection of
`Char_Load` confirms that primary `st` still enters `Ini_Parse` /
`StateDefs_Load`; only explicit additional `st0`..`st9` currently dispatch
through the ZSS compiler. The CNS diagnostic at the empty negative state is
therefore not a trustworthy ZSS compiler location. This is a failed compatibility
probe, not PASS/SKIP and not a test of playable ZSS combat.

## Dependencies exposed by this corpus

| Work | Why it is needed |
|---|---|
| Primary and common-state ZSS loading | DEF uses `st = kfm.zss` and `stcommon = common1.cns.zss` |
| Functions and calls | Main source begins with `[Function EndState(type)]`; sound/effect helpers use calls |
| Block attributes | Sources use `persistent(0)` and `ignorehitpause` |
| More controller payloads | PlaySnd, HitDef, AssertSpecial and effects exceed the audited subset |
| Explicit Ikemen extensions | `aiLevelF`, `map(...)` and map controllers are not ordinary MUGEN expressions/controllers |

These are source-inspection requirements after the first observed loader
failure. They are not the results of successfully compiling later files.
Keep the corpus unchanged as an integration target. Add small isolated regression
fixtures for each missing semantic feature, then rerun this complete character.
The successful switch/loop smoke fixture uses the original CNS KFM with added
experimental states; it does not qualify this upstream ZSS character.

Fetcher validation performed here: verified-cache installation, `--check`,
idempotent installation, and refusal to overwrite a deliberately modified file
in a disposable test tree. The installed upstream files still pass all 17 hashes.
