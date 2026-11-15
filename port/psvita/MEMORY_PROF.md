# Heap memory profile — where the engine's bytes go

This is the record of the heap-accounting work: what the counters measure, how
to reproduce a profile, and what a real game's ranking looks like. It is filed
under `port/psvita` because that target is the one with a memory ceiling, but
**the numbers below were measured on the Windows builds, not on a Vita.** See
section 6 for what does and does not carry over.

The short version: **half the engine's heap is decoded audio.** SND files are
resampled to the output format in full at load time and kept resident, even
with `-nosound`. Sprites are the other large block. Nothing leaks per frame —
every big site is a deliberate cache that lives until unload.

## 1. Instrumentation

The original binary has no memory accounting at all. This tree adds two layers:
aggregate counters, which are always compiled in, and per-call-site attribution,
which is opt-in.

**Aggregate counters** (`src/memprof.c`, always compiled in). `mugen.h`
redirects `malloc`/`calloc`/`realloc`/`free` to `Memprof_*`, so every engine
translation unit — every `src/*.c`, since all of them include `mugen.h` — is
counted without touching the call sites. Vendored code that does not include
`mugen.h` is not covered (section 2). Lua is separate, via
`lua_newstate(Memprof_LuaAlloc, ...)` in `src/luaapi.c` (`Lua_Init`). Once a
second, alongside the `perf:` line, a `mem:` line reports live, peak,
allocations, frees and failures:

```
mem: heap/lua heap_live=... heap_peak=... heap_allocs=... heap_frees=...
     heap_fails=0 lua_live=... lua_peak=... lua_allocs=... lua_frees=...
```

That line needs `[Debug] Verbose = 1` (or `ShowFPS = 1`), the same gate as
`perf:`.

**Call-site attribution** (`MUGEN_MEMPROF=1`, off by default). Each allocation
records its caller PC; a 256-entry table accumulates live/peak bytes, allocs
and frees per site; the table is rewritten to `mugen.memprof` on every `mem:`
line, so a profile survives the process being killed. `Memprof_Stop` writes it
once more on a clean exit.

The PCs are absolute runtime addresses and Windows relocates the image
(DYNAMICBASE), so the dump header carries the load base:

```
# mugen memory-by-site dump
# base 0x4d0000
# format: live_bytes peak_bytes allocs frees 0x<pc>
```

`tools/memprof_symbolize.py` subtracts that base, adds the PE ImageBase and asks
addr2line — the same split as `tools/prof_symbolize.py`: cheap capture on the
target, symbolization on the workstation.

```bat
:: run
set MUGEN_MEMPROF=1
set MUGEN_MEMPROF_OUT=mugen.memprof
mugen-mingw-win32-sdl2.exe -p1 GokuZ2 -p2 GohanZ2 -p1.ai 7 -p2.ai 7 -s hdbz_ball

:: rank
python tools\memprof_symbolize.py mugen.memprof ^
    --binary build\mingw-sdl2\mugen-mingw-win32-sdl2.exe ^
    --addr2line C:\x86devkit\bin\i686-w64-mingw32-addr2line.exe --top 10
```

For a 64-bit binary also pass `--image-base 0x140000000`; the MinGW x86 PE
links at `0x400000`, which is the default.

Without `MUGEN_MEMPROF=1` the attribution is off and only the aggregate is
collected. The switch costs one extra atomic per allocation and a stack capture
per site, which is why it is not on by default.

`[Debug] Verbose = 1` in the game's `mugen.cfg` is what makes the `mem:` line
appear, and the dump is refreshed from that line — so with `Verbose = 0` the
dump is written only once, by `Memprof_Stop`, and only if the process exits
cleanly. Kill it and there is nothing to symbolize.

## 2. What is counted, and what is not

| counted | how |
|---|---|
| engine CRT heap | `mugen.h` macros → `Memprof_*` |
| Lua's arena | `Memprof_LuaAlloc` |
| libpng's decode buffers | `png_create_*_struct_2` with `Memprof_*` allocators (`src/pngio.c`) |

| not counted | why |
|---|---|
| GL textures, VBOs, framebuffers | driver memory, not the CRT heap |
| SDL / SDL2_mixer / SDL2_ttf | separate heaps, reached through their own API |
| libresample, minizip | call the CRT directly and do not include `mugen.h` |
| CRT-internal allocations (`strdup` and friends) | never pass through the wrappers |

So the reported total is a **lower bound** on process memory. On the measured
run the heap accounted for 248.8 MB of a 316.3 MB working set; the remaining
~67 MB is the graphics and audio stacks above.

## 3. Measurement

Game: Hyper DBZ 5.0d (`C:\PortableApps\Hyper DBZ 5.0d`), a 1.1-era tree of
about 1.27 GB with large SND/SFF assets.

Arguments, identical for every run:

```
-p1 GokuZ2 -p2 GohanZ2 -p1.ai 7 -p2.ai 7 -s hdbz_ball
```

Each run: start, let the match load and run 45 s, then stop the process. The
site table is rewritten every second, so the last dump reflects the steady
state rather than wherever the kill landed. Working set is sampled at 2 Hz for
the process-level comparison.

| build | heap live (last) | heap peak | per-site live sum | process peak WS | sites |
|---|---:|---:|---:|---:|---:|
| `mugen-mingw-win32-sdl2.exe` | 249.3 MB | 250.5 MB | 248.8 MB | 316.0 MB | 125 |
| `mugen-mingw-win64-sdl2.exe` | 253.7 MB | 255.0 MB | 253.2 MB | 340.3 MB | 125 |
| `Hyper DBZ.exe` (original) | — | — | — | 278.7 MB | — |

Two columns look redundant but are not. `heap_live`/`heap_peak` are the global
counters from the `mem:` line; the per-site sum comes from the dump, which is
written just after that line. The totals agree to within about 0.5 MB, and the
sum of the per-site *peaks* is allowed to exceed the global peak because
different sites peak at different moments.

The original cannot appear in the heap columns: it has no counters, and under
these arguments it writes no log at all, so working set is the only signal it
can give. The +37 MB (win32) and +62 MB (win64) over the original are therefore
**unattributed** — they are most likely in the parts this instrument cannot see
(section 2), but that is an inference, not a measurement.

## 4. Top 10 sites — 32-bit

Ranked by live bytes, 125 sites, 248.8 MB live in total.

| # | live | peak | allocs | frees | function | source |
|---:|---:|---:|---:|---:|---|---|
| 1 | 133.8 MB | 133.8 MB | 555 | 0 | `Snd_DecodeWavResampled` | `src/sound.c:705`, allocation at `:748` |
| 2 | 34.2 MB | 34.2 MB | 2054 | 0 | `Pcx_Decode` | `src/sff.c:172` |
| 3 | 31.7 MB | 31.7 MB | 2198 | 158 | `Sprite_Init` | `src/sprite.c:73`, allocation at `:90` |
| 4 | 16.0 MB | 16.0 MB | 1 | 0 | `DecompBuffer_Create` | `src/spritecodec.c:956` |
| 5 | 12.6 MB | 12.7 MB | 14489 | 44 | `KeyedArray_Create` | `src/keyedarray.c:17` |
| 6 | 6.6 MB | 6.6 MB | 6 | 2 | `Sff2_Load` | `src/sff.c:592` |
| 7 | 3.4 MB | 3.4 MB | 14489 | 23 | `KeyedArray_Create` | second site, same function |
| 8 | 2.2 MB | 2.2 MB | 41811 | 50 | `Trigger_Parse` | `src/trigger.c:500` |
| 9 | 2.1 MB | 2.3 MB | 59848 | 2295 | `Mem_Calloc` | `src/util.c:10` |
| 10 | 1.6 MB | 1.6 MB | 4847 | 158 | `Sprite_InitRows` | `src/sprite.c:320` |

The 64-bit build produces the same ranking. Sites 1–6 are byte-identical; the
`KeyedArray_Create` and `Trigger_Parse` entries grow about 12% from doubled
pointer width.

## 5. Reading the numbers

**Audio is over half the heap — 133.8 MB, 54%.** `Snd_DecodeWavResampled`
allocates the entire resampled output buffer up front
(`src/sound.c:748`: `calloc(capacity, out_frame)`), then decodes into it. The
555 allocations average about 247 KB each, matching fragments of the game's
SND banks resampled to the mixer's 44.1 kHz. This happens during asset load
regardless of `-nosound`, because the flag suppresses output, not decoding.

This is the clearest target on the list and the one a Vita port would feel
most: nothing in the profile is larger, and the cost is paid to produce audio
that a muted run never plays.

**Sprites are the next block, about 85 MB** across `Pcx_Decode` (34.2),
`Sprite_Init` (31.7), `Sprite_InitRows` (1.6), `Sff2_Load` (6.6) and the
one-off `DecompBuffer_Create` (16.0). Each is the decoded pixel data for a
sprite set; together they are the resident working set of the two fighters and
the stage.

**`frees = 0` on sites 1–4 is not a leak.** These are caches: decoded audio and
sprites live until the character or stage is unloaded, which does not happen
inside a single match. The alloc-heavy sites are retained for the same reason —
`Trigger_Parse` (41,811 allocs, 50 frees) keeps the parsed expression arrays for
the life of the loaded characters, and they are small per entry, which is why
2.2 MB covers that many allocations.

The evidence that nothing grows unbounded is the `mem:` line itself. Over the
45 s run, sampled once a second (41 samples): `heap_live` moved by only **60 KB**
end to end (249.33 → 249.27 MB), and `heap_fails` was 0 in every sample.
`heap_peak` (250.5 MB) was already at its final value in the first sample, so it
was set before the first logged second — during start-up and asset load — and
never approached again during play.

## 6. Applicability to the Vita

The counters and the attribution are portable — they are plain C over the CRT,
and every Vita build compiles the same `src/memprof.c`. But **no Vita
measurement exists yet**, and the transferable part is narrower than it looks:

- The **ranking** should carry over: the Vita build decodes the same SND, SFF
  and PCX data through the same functions, at the same 32-bit pointer width as
  the win32 numbers above.
- The **absolute megabytes will not**. The Vita's audio path is SDL2_mixer
  rather than the desktop mixer, its `g_sndFormat` may differ, and it packs a
  different game tree.
- **Working-set figures are meaningless there**: `GetProcessMemoryInfo` does not
  exist, and the dump's load base is written only for PE images. A Vita run
  would need its own process-memory query, which is not implemented.

To measure on hardware, the same two environment variables apply —
`MUGEN_MEMPROF=1` and `MUGEN_MEMPROF_OUT` — but the PCs are unrelocated ELF
addresses, so the symbolizer needs the Vita build's own binary and the
`arm-vita-eabi-addr2line`.

## 7. Bugs found while validating

The attribution was wrong three times before the numbers above were trustworthy;
all three are fixed in the committed code and are recorded here because each one
produced a plausible-looking but false ranking.

1. **Inlining shifted every attribution by one frame.** `Memprof_CallerPC` was
   folded into `Memprof_Malloc` at `-O2`, so the captured PC belonged to the
   wrapper rather than the call site. It is `noinline` now, and the frame index
   depends on that.
2. **`Memprof_Realloc` subtracted `old_size` twice** — once directly and again
   inside `size - old_size` — so live counts drifted and sites went negative.
   Each path now applies exactly one delta.
3. **Per-site realloc credited only the delta.** The old site never released
   the block it had allocated and the new site never received it, leaving
   `18446744073709544852`-byte entries in the dump (a negative `uint64`). A
   realloc now moves the whole block: the old site loses `old_size`, the new
   site gains `size`.

A symlink-level fourth: the symbolizer was originally passing the RVA to
addr2line instead of `ImageBase + RVA`, which resolved every address to `??`.
Both are needed because MinGW links at `0x400000` while Windows loads elsewhere.

## 8. Where this leaves the memory work

- The instrument is done for the desktop targets and reproducible from the
  commands in section 1.
- The one large, actionable finding is **audio: 134 MB decoded eagerly and kept
  resident**. Streaming or lazily decoding SND, or skipping decode entirely when
  sound is disabled, is the change with the most headroom.
- Everything else is ordinary resident cache; there is no per-frame leak to chase.
- A Vita figure still needs a platform memory query (section 6). Until then,
  treat the ranking as the transferable result and the megabytes as desktop-only.

## 9. Commits

- `1f44181` — heap and Lua counters, `mem:` log line.
- `06bcd42` — libpng decode allocations counted.
- `d131cdf` — libpng callback types for the 64-bit kit (`png_alloc_size_t`).
- `dac7217` — call-site attribution, `MUGEN_MEMPROF`, the dump, and
  `tools/memprof_symbolize.py`, including the three accounting fixes above.
