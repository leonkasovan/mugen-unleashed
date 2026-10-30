# Lua in mugen-unleashed

Reference for the Lua 5.1.4 embedding: how the engine drives it, and every
function it exposes to scripts.

Two audiences:

- **Script authors** — jump to [The `mugen` library](#the-mugen-library) and
  [Player objects](#player-objects-methods). The
  [Using Lua in a game](#using-lua-in-a-game-installation) section explains
  where scripts live and how to run them.
- **Porters** — the [Internals](#internals) part covers the C side
  (`src/luaapi.c`, `src/luabind.c`, `src/luautil.c`).

Line numbers are from the tree as reconstructed. Function signatures below
were extracted from the sources, not from the original's (absent)
documentation.

## Contents

| | |
|---|---|
| [What is linked](#what-is-linked) | Lua version, the three glue modules |
| [Lifecycle](#lifecycle) | start-up, `init.lua`, shutdown |
| [Using Lua in a game](#using-lua-in-a-game-installation) | where the scripts are, how to run them |
| [The `mugen` library](#the-mugen-library) | 37 functions, full reference |
| [Player objects](#player-objects-methods) | `player.*` and 88 methods |
| [Vectors](#vectors) | the `Vector` class and unit conversion |
| [Internals](#internals) | C API, calling convention, sandbox |
| [Known gaps](#known-gaps) | what does not work yet |

---

## What is linked

Lua **5.1.4**, built from its original sources in
`third_party/lua-5.1.4/src/` and linked statically (`liblua.a` in the Makefile
build, target `lua51` in CMake). Same version the original `mugen.exe` carried
(`docs/ANALYSIS.md`: `.text` 0x4C4DE0–0x4DC549).

The engine's glue is three modules, one per original module:

| File | Original range | Role |
|---|---|---|
| `src/luaapi.c` | 0x42E2F0–0x42EF4F | the `mugen` library, state lifecycle, vector interop |
| `src/luabind.c` | 0x41E880–0x4214FF | the `player` library and the `mugen.player` userdata |
| `src/luautil.c` | 0x478050–0x478606 | package path, signature-driven calls, typed table reads |

There is exactly **one** Lua state for the whole process: the global
`g_luaState`.

Lua's C API is declared by hand in `src/symbols.h` (the original binary only
imported the entry points it used), so only those symbols are available to the
engine. Each Lua-using `.c` file defines the `LUA_*` constants it needs
locally.

## Lifecycle

### Start-up

`Lua_Init` (`luaapi.c`) is the only entry point. `System_Init` calls it during
start-up (`main` → `System_Init` → `Lua_Init`):

```c
g_luaState = luaL_newstate();                        /* one global state */
lua_atpanic(g_luaState, Lua_Panic);
luaL_openlibs(g_luaState);                           /* standard libraries */
luaL_register(g_luaState, "mugen", luamugen_funcs);  /* the `mugen` table */
LuaBind_Register();                                  /* `player` + mugen.player */
Lua_SetPackagePath();                                /* package.path = data/?.lua */
Lua_RunInit();                                       /* run data/init.lua */
```

`Lua_Init` is idempotent: it returns 1 immediately when `g_luaState` is
already set. It returns 0 only when `luaL_newstate` fails.

Order matters: the C libraries are registered **before** `init.lua` runs, so a
script can use `mugen.*` and `player.*` from its first line.

### `data/init.lua`

`Lua_RunInit` (`luaapi.c:431`) loads `data/init.lua` with `luaL_loadfile` and
runs it with `lua_pcall`. On failure it logs the message from the stack via
`Log_Printf` and returns the loader/call error code — it does **not** abort
start-up. Errors here only appear in `mugen.log`.

`init.lua` is game data, not part of this repository. It is what makes the
rest work: it `require`s `vector`, `maux`, `fp`, `player` and `mugen`,
defines `print`, publishes helpers into globals, and sets `mugen.isinit`.

### Errors

| Situation | Handler | Effect |
|---|---|---|
| Error inside `lexec` / a script line | `lua_pcall` in `Con_CmdLuaExec` | caught, message printed to the console, state stays usable |
| `luaL_error` in a C function | `longjmp` to the enclosing protected call | unwinds to that `lua_pcall` |
| Error outside any protected call | `Lua_Panic` (`lua_atpanic`) | `Error_Fatal("Lua fatal error: %s")` — the process exits |

### Shutdown

`lua_close` is declared but the reconstruction never calls it: the state lives
for the whole process, and exit goes through the engine's normal shutdown.

---

## Using Lua in a game installation

The default M.U.G.E.N 1.1 Beta 1 installation ships **seven Lua scripts, all in
`data/`**:

| File | Purpose |
|---|---|
| `data/init.lua` | entry point, run automatically |
| `data/vector.lua` | the `Vector` class (1D/2D/3D, units, arithmetic) |
| `data/player.lua` | high-level player **interface**: defaults, type checks, iterators |
| `data/mugen.lua` | adds `mugen.error`, `mugen.warning`, `mugen.safecall` (sandbox) |
| `data/maux.lua` | helpers: `dir`, `dir_t`, `isinteger` |
| `data/fp.lua` | functional helpers: `map`, `mapn`, `reduce`, `filter` |
| `data/test.lua` | the example/playground |

They are in **`data/`, not `plugins/`** — `plugins/` holds only the BGM
plugins (`vorbisplug.dll`, `waveplug.dll`). The shipped documentation
(`install/docs/*.html`) and `readme.txt` never mention Lua, which is why the
feature is easy to miss.

### Running it

The Lua **console**. `lmode` toggles the prompt between `>` and `$> `; in Lua
mode every line you type is rewritten to `lexec <line>` and evaluated.
`lexec` also works directly from the command prompt:

```
> lexec mugen.gametime()
1234
> lexec i1.velset{x=10, y=-9}
> lexec print(player.getplayer(1):life())
1000
```

`data/test.lua` reads like a console session because that is what it was for:

```lua
p1 = player.getplayer(1)
i1 = player.getinterface(1)

i1.velset{x=10, y=-9}       -- push player 1 up and forward
i2.velset{}                 -- stop player 2 (defaults)
print(tostring(p1:life()))
```

### Two ways to call a player

| | `player.getplayer(idx)` | `player.getinterface(idx)` |
|---|---|---|
| returns | userdata with the raw methods | Lua table wrapping them |
| arguments | positional: `p:var(3)` | named table: `i.velset{x=1, y=-2}` |
| defaults | none — pass every argument | per `player.interface_functions_sig` |
| type checks | Lua's own (`luaL_checkinteger`) | explicit, with helpful messages |
| unit handling | raw game units | converts to the player's `unitsize` |
| use when | you want exact control | you want readable scripts |

`player.lua` builds the interface from three lists:
`player.interface_functions` (the ~70 no-argument getters),
`player.interface_functions_vector_retval` (methods returning a `Vector`, whose
unit is converted and which collapse to a number when 1D), and
`player.interface_functions_sig` (`velset`, `explod`, `changestate` — the only
three taking a parameter table).

---

## The `mugen` library

37 functions in the global table `mugen` (`luamugen_funcs`, `luaapi.c`).
Unless stated otherwise a function takes no arguments and returns one value.
Errors are raised with `luaL_error`, i.e. they abort the calling script line.

### Time, round and match

| Function | Arguments | Returns | Notes |
|---|---|---|---|
| `mugen.gametime()` | — | integer | ticks since the match started |
| `mugen.tickspersecond()` | — | integer | ticks in one second (the game speed) |
| `mugen.roundno()` | — | integer | current round number |
| `mugen.roundstate()` | — | integer | the round state machine's current state |
| `mugen.matchno()` | — | integer | current match number |
| `mugen.roundtimeleft()` | — | integer | ticks left in the round |
| `mugen.roundtimemax()` | — | integer | ticks the round started with; **-1** when there is no timer (team variants 1/3, demo mode) |
| `mugen.setroundtimeleft(n)` | integer `n` | — | sets the time left; raises `invalid round time left` when `n < -1` |
| `mugen.winningteam()` | — | integer | result of `Match_CheckOver()` |
| `mugen.gamemodecode()` | — | integer | `g_game->screen_kind` |

### Geometry

| Function | Returns | Notes |
|---|---|---|
| `mugen.gamewidth()` | number | `game_coord[0] / camera_zoom` |
| `mugen.gameheight()` | number | `game_coord[1] / camera_zoom` |
| `mugen.screenwidth()` | number | `game_coord[0]` — the **game** coordinate width (not the video mode width) |
| `mugen.screenheight()` | number | `game_coord[1]` — likewise the game coordinate height |
| `mugen.camerapos()` | `Vector` | camera position, in camera units |
| `mugen.camerazoom()` | number | current zoom factor |
| `mugen.leftedge()` | number | `camerapos.x - gamewidth/2` |
| `mugen.rightedge()` | number | `camerapos.x + gamewidth/2` |
| `mugen.topedge()` | number | stage y of screen (0,0) via `Coord_ScreenToStage` |
| `mugen.bottomedge()` | number | `Screen_GetBottomEdge()` |

Note `screenwidth`/`screenheight` are **not** the window or video-mode size:
they are the game's coordinate space (`game_coord`), which is what the paired
`gamewidth`/`gameheight` divide by the camera zoom.

### Randomness

| Function | Arguments | Returns | Notes |
|---|---|---|---|
| `mugen.random(n)` | integer `n` | integer in `[0, n-1]` | draws from the **match** RNG, so it stays in sync with the engine's own randomness; raises `negative or zero random modulus` when `n <= 0` |

### Actions

| Function | Notes |
|---|---|
| `mugen.lifereset()` | resets life, power and round time |
| `mugen.roundreset()` | resets the current round |
| `mugen.matchreload()` | reloads and resets the current match |

### Console and debug toggles

| Function | Notes |
|---|---|
| `mugen.toggleconsole()` | shows/hides the debug console |
| `mugen.togglemaxpowermode()` | unlimited power on/off |
| `mugen.togglebardisplay()` | cycles the life bar display |
| `mugen.toggleclsndisplay()` | cycles collision-box display |
| `mugen.toggletrainingmode()` | training mode on/off |
| `mugen.clearclipboards()` | clears the debug output |

### Output

| Function | Arguments | Notes |
|---|---|---|
| `mugen.log(s)` | string | appends to `mugen.log` |
| `mugen.consoleprint(s)` | string | appends to the debug console pane |
| `mugen.clipboardprint(s)` | string | appends to the debug clipboard |

These are the only output channels a script has. `init.lua` builds `print` on
top of two of them (`consoleprint` + `log`) and points `mugen.warning` at
`clipboardprint`.

### Key bindings

| Function | Arguments | Returns | Notes |
|---|---|---|---|
| `mugen.keybindadd(key, cmd)` | key string, command string | — | binds a key to a console command; raises `Bad keybind string: %s` if the key does not parse |
| `mugen.keybinddel(key)` | key string | — | removes a binding; same error |
| `mugen.keybindget(key)` | key string | string or **nil** | the command bound to the key, or nil; same error |

### Misc

| Function | Returns |
|---|---|
| `mugen.language()` | string — the current language code |

---

## Player objects (methods)

`player.getplayer(idx)` returns a `mugen.player` userdata. Its 88 methods are
called with `:` (the object is the implicit first argument). Below, "arg"
counts exclude the object itself.

### Module functions (`player.*`)

| Function | Arguments | Returns |
|---|---|---|
| `player.getplayer(idx)` | integer index | player object; raises `Invalid player at idx %d` if there is no active player there |
| `player.getplayerlist()` | — | table of indices of all used players |
| `player.indexisvalid(idx)` | integer | boolean |
| `player.indexfromid(id)` | integer id | index, or **nil** when `Player_FindIndexById` returns -1 |
| `player.enabled(idx)` | integer | boolean — the slot is active; raises `Invalid player at idx %d` when `idx` is outside `0..player_slots` |
| `player.enableset(idx, on)` | integer, boolean | — ; raises `(boolean expected)` for a non-boolean second argument |

`player.lua` adds `player.playeridexist(id)`, `player.player_iter()` and
`player.interface_iter()` (both iterators skip slots that disappear
mid-iteration), and `player.getinterface(idx)`.

### Getters (return one value, no arguments)

| Group | Methods |
|---|---|
| Identity | `name`, `authorname`, `id`, `palno`, `facing`, `alive`, `ishelper`, `unitsize`, `roundsexisted` |
| Team | `teamside`, `teammode`, `ishometeam`, `numpartner`, `numenemy` |
| Life / power | `life`, `lifemax`, `power`, `powermax` |
| Animation | `anim`, `animtime` |
| State | `stateno`, `prevstateno`, `statetype`, `movetype`, `ctrl` |
| Hits | `hitcount`, `uniqhitcount`, `movehit`, `moveguarded`, `movecontact`, `movereversed`, `inguarddist` |
| Hit state | `hitfall`, `hitover`, `hitshakeover`, `hitpausetime`, `canrecover` |
| AI | `aienabled`, `ailevel` |
| Results | `win`, `winko`, `wintime`, `winperfect`, `lose`, `loseko`, `losetime`, `drawgame`, `matchover` |
| Validity | `isvalid` |

`isvalid` is the one getter that re-checks the reference: it returns true only
when the player at the stored index is still active **and** still has the
stored id.

### Getters taking an argument

| Method | Argument | Returns |
|---|---|---|
| `anim` elem / time | `animelemno(n)`, `animelemtime(n)` | integer n-th element's number/time |
| `animexist(n)` | animation number | boolean |
| `selfanimexist(n)` | animation number | boolean |
| `var(n)` | index 0..59 (`PLAYER_VAR_COUNT` = 60) | integer; raises `var index out of range` |
| `fvar(n)` | index 0..39 (`PLAYER_FVAR_COUNT` = 40) | number; raises `fvar index out of range` |
| `sysvar(n)` | index 0..4 | integer; raises `sysvar index out of range` |
| `sysfvar(n)` | index 0..4 | number |
| `hitdefattrmatch(s)` | attr string like `"S,NA"` | boolean; raises `Invalid HitDefAttr string` |
| `gethitvar(s)` | name | value; raises `invalid gethitvar name %s` for unknown names, `illegal gethitvar` for a bad slot |
| `command(s)` | command name | boolean; raises `Undefined command label: %s` |
| `numproj([id])` | optional id | count; `id > 0` counts only that id, `id == 0` or absent counts all, negative **raises** `negative projid %d` |
| `numexplod([id])` | optional id, default -1 (all) | count; an id `< -1` is **not** an error — it is warned about and treated as -1 |
| `numtarget([id])` | optional id, default -1 (all) | count; same lenient handling as `numexplod` |
| `numhelper([id])` | optional id, default 0 (all) | count; a negative id is warned about and treated as 0 |

The four `num*` methods deliberately differ: only `numproj` raises on a
negative id; the others fall back to "all" with a player warning.

### Getters returning a `Vector`

`vel`, `pos`, `screenpos`, `backedge`, `frontedge`, `frontedgedist`,
`backedgedist`, `frontedgebodydist`, `backedgebodydist`, `hitvel`,
`parentdist`, `rootdist`. Through `player.getinterface` these are converted to
the player's unit and collapse to a plain number when 1-dimensional.

### Setters (return nothing)

| Method | Argument | Notes |
|---|---|---|
| `lifeset(n)` | integer | goes through `Player_SetLife`, so clamps and side effects apply |
| `powerset(n)` | integer | |
| `aienableset(on)` | boolean | |
| `velset{x=, y=}` | table | sets velocity directly; `x` is multiplied by `facing`, so positive `x` is always "forward" |

### Actions

| Method | Arguments | Notes |
|---|---|---|
| `changestate{value=, ctrl=, anim=, selfstate=, autoturn=}` | table; `value` required | raises `required value field is missing`; `selfstate` clears a custom state first; raises `attempted change to negative state` |
| `explod{anim=, …}` | table; `anim` required | raises `required anim field is missing`; honours `usefightfx`, `ownpal`, `id`, `pos`, `vel`, `accel`, `removetime`, `supermovetime`, `pausemovetime`, `sprpriority`, `shadow`, `removeongethit`, `hitpausemove`, `scale`, `angle`, `yangle`, `xangle` |

The full parameter list with types and defaults is `player.interface_functions_sig`
in `data/player.lua`; `player.getinterface` validates against it and raises
`<fn>: parameter <k> is incorrect type: <got> (<expected> expected)`.

### Mailbox and debug

| Method | Returns | Notes |
|---|---|---|
| `projcanceltime(n)`, `projhittime(n)`, `projguardedtime(n)`, `projcontacttime(n)` | nothing | push times into the player's mailbox (index 3 is read by `LuaBind_ProjTime`) |
| `isvalid()` | boolean | see above |
| `__tostring(v)` | string | used by `tostring(playerobject)` |
| `rc()` | table | the player's `[const]` table (raw registry table); this is what `player._meta.const` reads |

`rc()` is the debug accessor: it returns the character's whole constant table,
which is how `data/player.lua`'s `:const(name)` and `data/test.lua`'s
`printconst` work.

---

## Vectors

The `Vector` class lives in `data/vector.lua` and is required by `init.lua`.
The engine calls it through `Lua_CallFunction` (see below), so **it must
exist** — without `init.lua`, anything returning a `Vector` fails.

| Engine call | Lua target |
|---|---|
| 1D value | `Vector:vec1(x, unit)` |
| 2D value | `Vector:vec2(x, y, unit)` |
| 3D value | `Vector:new(x, y, z, unit)` |

`Vector:vec1` produces `dimensions == 1`, which `player.lua` collapses to a
bare number. Templates like `"tff>-"` mean: the table (self), `f` double, `f`
double, results ignored.

---

## Internals

### Calling Lua from C

#### `Lua_CallFunction` — signature-driven

`luautil.c:49`. Calls a global function, or `table.func`, with arguments and
results described by a format string: everything before `>` is an argument,
everything after it a result.

| Code | Argument | Result |
|---|---|---|
| `f` | `double` | `double *` |
| `i` | `int`, pushed as a number | `int *`, truncated |
| `s` | `const char *` | `const char **`, owned by Lua |
| `g` | value of a global, by name | — |
| `t` | the table itself (self) | — |
| `-` | — | skipped |

```c
Lua_CallFunction(L, "Vector", "vec1", "tff>-", x, unit);
```

Missing table or function raise `%s not a valid table` /
`%s.%s not a valid function`. Quirk kept from the original: `'t'` when there
is no table pushes nothing but is still counted as an argument.

#### Vector conversion

`Lua_PushVector`, `Lua_ReadVector`, `LuaTable_GetVector`,
`LuaTable_GetVectorNumber` convert between the engine's `Coord` and the Lua
`Vector`. A 1D value has `Coord.type == 1` (`COORD_TYPE_1D`).

### The userdata

```c
typedef struct LuaPlayerRef {
    int valid;   /* 0x00 1 */
    int id;      /* 0x04 Player.id */
    int index;   /* 0x08 Player.index */
    int flag;    /* 0x0C 0 (PlayerRef.flag) */
} LuaPlayerRef;
```

References store **id + index, never a `Player *`**. Every method calls
`LuaPlayer_Check` → `LuaBind_PlayerAt`, which re-resolves the index against
`GameVars.players` and verifies the id, so a reference taken in one frame
cannot dangle in the next. A stale reference raises a Lua error rather than
reading freed memory.

### Registration

`LuaBind_Register` (`luabind.c:1501`) creates the `mugen.player` metatable,
sets `__index` to the metatable itself so methods resolve on the userdata,
registers the method table on it, then registers the module table `player` and
points its `_meta` field back at the metatable (which is how
`player.lua` adds `warning`/`error`/`const`).

### The sandbox

`data/mugen.lua` provides `mugen.safecall(fn)`: it installs a count hook with
`debug.sethook(abortcheck, "", mugen.maxinstructions)` (default 1,000,000
instructions) and runs the function in an environment whitelisting only safe
standard functions (`assert`, `error`, `ipairs`, `pairs`, `pcall`, `select`,
`tonumber`, `tostring`, `type`, `unpack`, `xpcall`, plus subsets of
`string`, `table`, `math` and `Vector`). `os`, `io` and `debug` are not
exposed. This is a **Lua-side** sandbox, not a C-side one: the engine does not
restrict what a script can call directly.

### Character constants

Each `CharData` carries `lua_ref`, a registry reference (`luaL_ref`) to a table
of the character's `[const]` values, created during character load
(`char.c:606`). `CharLua_SetConstInt/SetConstFloat/SetConstCoord` write it;
the `Get` pair reads it. That is how a trigger such as
`const(movement.run.fwd)` resolves — the C side reads the same table the
script sees through `rc()`.

### Console integration

`Console.lua_mode` (`console.h`, offset 0x14) switches the console:

- prompt becomes `$> ` instead of `> `;
- `Console_QueueCommand` rewrites every line that does not start with
  `lmode`/`lexec` into `lexec <line>`.

`lmode` and `lexec` are registered in `Console_Create`. `lexec` tries the line
as an expression (`return <line>`) first, so a bare value prints; if that does
not parse it runs the line as a statement. Results go to the console pane;
errors are printed, not raised.

---

## Known gaps

| Gap | Detail |
|---|---|
| **`lua_close` never called** | the state lives for the process lifetime |
| **`lexec` runs one line** | no multi-line input; `dofile` from a script is the way to run a file |
| **No debugger** | only `mugen.log`, `consoleprint`, `clipboardprint` |
| **`init.lua` errors are non-fatal** | they only appear in `mugen.log`, and every `Vector`-returning call then fails |
| **`luaL_loadstring` / `lua_tostring` unavailable** | not declared in `symbols.h` because the original never imported them; `lexec` uses `luaL_loadbuffer` and `lua_tolstring` instead |

### Reconstruction status of the console commands

`lmode` and `lexec` were **missing** from this reconstruction until they were
added. The original registers both — its command strings at file offset
`0xF4450` read `… toggletrainingmode \0 lmode \0 lexec  \0` in the same run as
the other console commands. Their bodies are therefore reconstructed rather
than decompiled: behaviour follows the documented contract above, but the
original's exact output formatting has not been diff-tested.
