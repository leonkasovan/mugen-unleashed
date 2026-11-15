# Tracked-heap memory profile — where the engine's bytes go

> **This is a tracked-engine-heap profiler, not process-memory accounting.**
> It reports what the engine and Lua asked the C runtime for. It cannot see
> resident code, shared pages, heap metadata, thread stacks, or the graphics
> and audio drivers. Never subtract its totals from a working-set figure and
> present the remainder as "other" — that remainder is unmeasured, not
> attributed. Section 2 lists the gaps explicitly.

This is the record of the heap-accounting work: what the counters measure, how
to reproduce a profile, and what a real game's ranking looks like. It is filed
under `port/psvita` because that target is the one with a memory ceiling, but
**the numbers below were measured on the Windows builds, not on a Vita.** See
section 6 for what does and does not carry over.

The short version: **half the tracked heap is decoded audio.** SND files are
resampled to the output format in full at load time and kept resident, even
with `-nosound`. Sprites are the other large block. Across the measured
45-second run the tracked heap did not grow; that is evidence about this run,
not a proof that nothing leaks anywhere (section 5).

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
     sites=... sites_dropped=... unattributed=... tracking=...
```

`tracking=` counts live, dynamically allocated pointer records and site-table
storage (not transient snapshot buffers or the static table of buckets).
`sites_dropped` counts attribution attempts that could not be recorded;
`unattributed` counts **currently live** payload bytes without a recorded site.
Neither counter silently wraps when an untracked allocation is freed.

That line needs `[Debug] Verbose = 1` (or `ShowFPS = 1`), the same gate as
`perf:`.

**Call-site attribution** (`MUGEN_MEMPROF=1`, off by default). Each allocation
records its caller PC; the site table accumulates live/peak bytes, allocs and
frees per site and grows as needed. The dump is refreshed by `Memprof_Tick`
once per second from the frame loop — independent of `Verbose` — and written to
a temporary file. POSIX `rename` and Windows `MoveFileExA` replace the previous
snapshot without an explicit deletion, so a failure leaves the last good
snapshot intact. `Memprof_Stop` writes it once more on a
clean exit.

The PCs are runtime addresses, so the header says which address space they are
in and what base they are relative to:

```
# mugen memory-by-site dump
# abi pe
# base 0x4d0000
# format: live_bytes peak_bytes allocs frees 0x<pc>
# dropped_sites 0
# unattributed_bytes 0
```

`tools/memprof_symbolize.py` reads the ABI and does the right conversion for
each: for **PE** it adds the file's link-time ImageBase to `pc - base`, because
Windows relocated the image at load; for **ELF** `pc - base` is already the
link-time address, since `base` is the load bias (0 for a non-PIE executable,
the relocation bias for a PIE). The same split as `tools/prof_symbolize.py`:
cheap capture on the target, symbolization on the workstation.

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
collected. **No stack unwind occurs when it is off**. When on, a call-site PC
is captured for each engine allocation; the Windows stack walk has real cost,
which is why this remains a profiling mode rather than a default.

`[Debug] Verbose = 1` in the game's `mugen.cfg` controls only the `mem:` **log
line**. The dump is independent of it: `Memprof_Tick` refreshes the file once a
second from the frame loop, so a run killed with `Verbose = 0` still leaves a
current snapshot. `Memprof_Stop` writes one more on a clean exit.

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

These tracked payload figures are not directly comparable to a working-set
total, which measures resident physical pages and includes shared mappings,
code, thread stacks and more. The historical Windows run measured 248.8 MB of
tracked payload against a 316.3 MB peak working set, but **the difference is
not an allocation category or measured graphics/audio usage**.

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
| `mugen-mingw-win32-sdl2.exe` | 249.3 MB | 250.5 MB | 248.8 MB | 316 MB | 125 |
| `mugen-mingw-win64-sdl2.exe` | 253.7 MB | 255.0 MB | 253.2 MB | 340–353 MB | 125 |
| `Hyper DBZ.exe` (original) | — | — | — | 279 MB | — |

The heap columns are stable to a tenth of a megabyte across runs; the win64
working set is not, and is given as a range over two runs. Working-set figures
are the least repeatable number here and should not be read too finely.

Two columns look redundant but are not. `heap_live`/`heap_peak` are the global
counters from the `mem:` line; the per-site sum comes from the dump, which is
written just after that line. The totals agree to within about 0.5 MB, and the
sum of the per-site *peaks* is allowed to exceed the global peak because
different sites peak at different moments.

The original cannot appear in the heap columns: it has no counters, and under
these arguments it writes no log at all, so working set is the only signal it
can give. **The difference between a working set and this tracked heap is not
an attribution.** A 316 MB working set against 249 MB of tracked payload leaves
roughly 67 MB that this instrument simply does not see — resident code, shared
pages, per-allocation metadata, thread stacks, and the GL/mixer drivers among
them. That remainder is unmeasured, not "graphics and audio". Getting a real
number there needs a different tool (section 8).

## 4. Top 10 sites — 32-bit

Ranked by live bytes, 125 sites, 248.8 MB live in total.

| # | live | peak | allocs | frees | function | source |
|---:|---:|---:|---:|---:|---|---|
| 1 | 133.8 MB | 133.8 MB | 555 | 0 | `Snd_DecodeWavResampled` | `src/sound.c:705`, allocation at `:748` |
| 2 | 34.2 MB | 34.2 MB | 2054 | 0 | `Pcx_Decode` | `src/sff.c:172` |
| 3 | 31.7 MB | 31.7 MB | 2198 | 158 | `Sprite_Init` | `src/sprite.c:73`, allocation at `:90` |
| 4 | 16.0 MB | 16.0 MB | 1 | 0 | `DecompBuffer_Create` | `src/spritecodec.c:956` |
| 5 | 12.6 MB | 12.7 MB | 14489 | 46 | `KeyedArray_Create` | `src/keyedarray.c:17` |
| 6 | 6.6 MB | 6.6 MB | 6 | 2 | `Sff2_Load` | `src/sff.c:592` |
| 7 | 3.4 MB | 3.4 MB | 14489 | 25 | `KeyedArray_Create` | second site, same function |
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

What the data supports: over the measured 45 s the tracked heap did not grow.
`heap_live` moved by **73 KB** end to end across 42 one-second samples
(249.33 → 249.26 MB), and `heap_fails` was 0 in every sample. `heap_peak`
(250.5 MB) was already final in the first sample, so it was set during start-up
and asset load and never approached again during play.

What it does **not** support: a claim that nothing leaks anywhere. This covers
one scenario, one game, one 45-second window, and only the allocations the
wrappers can see. A leak in the GL driver, in a library that bypasses the
`mugen.h` macros (section 2), or in a code path this scenario never exercises
would not appear here. Longer sessions and repeated stage transitions would be
needed before saying more.

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
  exist on the Vita, and this profiler never measured process memory on any
  target. A Vita figure would need its own query, which is not implemented.

To measure on hardware the same two environment variables apply —
`MUGEN_MEMPROF=1` and `MUGEN_MEMPROF_OUT`. The Vita is an ELF target, so its
dump reports `# abi elf` and the PCs are runtime addresses relative to the
load bias; `tools/memprof_symbolize.py` handles that path now (it was PE-only
before the review). Symbolize against the Vita build's own unstripped binary
with `arm-vita-eabi-addr2line`. That ELF path was validated on Linux x86-64
with both a non-PIE and a PIE executable, not on Vita hardware — there is still
no Vita measurement.

## 7. Bugs found while validating

The attribution was wrong three times before the numbers above were trustworthy;
all three are fixed in the committed code and are recorded here because each one
produced a plausible-looking but false ranking.

1. **Inlining shifted every attribution by one frame.** The original helper
   was folded into `Memprof_Malloc` at `-O2`, moving the captured frame. On ELF
   the current code now captures level 0 **inside** each public allocator; on
   Windows the `noinline` unwinder helper keeps the intended frame index.
2. **`Memprof_Realloc` subtracted `old_size` twice** — once directly and again
   inside `size - old_size` — so live counts drifted and sites went negative.
   Each path now applies exactly one delta.
3. **Per-site realloc credited only the delta.** The old site never released
   the block it had allocated and the new site never received it, leaving
   `18446744073709544852`-byte entries in the dump (a negative `uint64`). A
   realloc now moves the whole block: the old site loses `old_size`, the new
   site gains `size`.

A fourth, in the symbolizer: it passed the RVA to addr2line instead of
`ImageBase + RVA`, which resolved every address to `??`. Both are needed because
MinGW links at `0x400000` while Windows loads elsewhere.

### Fixes from the first review round

That review found two correctness defects and four coverage/robustness gaps.
All are fixed, and each was reproduced before and after:

| # | Defect | Fix | Evidence |
|---|---|---|---|
| 1 | `__builtin_return_address(1)` on ELF: a nonzero level can fault in an optimised PIE build | capture at level 0 from inside `Memprof_*` (safe by GCC's rules); Windows keeps the unwinder-based `RtlCaptureStackBackTrace` | level 0 passes at `-O0`, `-O2` and `-O2 -fPIE -pie`; level 1 returns addresses outside the image on the same builds |
| 2 | realloc freed the caller's block when metadata insert failed after a successful CRT realloc | the metadata node is detached and **reused**, so a resize never allocates metadata and cannot fail | fault-injected test: old code returns NULL after consuming the pointer; new code injects zero metadata allocations and succeeds |
| 3 | ELF dumps recorded `base 0` but the symbolizer always added the PE ImageBase | dumps carry `# abi pe` or `# abi elf`; the symbolizer subtracts the bias for ELF and only adds ImageBase for PE | non-PIE and PIE RVAs both resolve to the right function through addr2line |
| 4 | fixed 256-site table dropped sites silently | table grows on demand; `sites_dropped` and `unattributed_bytes` are reported in the `mem:` line and the dump header | real run reports `sites=125 sites_dropped=0` |
| 5 | stats read 64-bit counters non-atomically; printed through 32-bit `unsigned long` on win64 | atomic loads; counters and all `%llu` formatting are `uint64_t` throughout | `tracking=` now reports 2.6 MB of metadata overhead that was previously invisible |
| 6 | dump written in place with `fopen(path,"w")`, so a kill mid-write lost the last good snapshot | written to `path.tmp`, flushed, closed, then renamed over the previous file; refresh is driven by `Memprof_Tick` per frame, not by the `mem:` log line | dump appears with `Verbose = 0`, independent of the log gate |
| 7 | the report implied process-memory accounting | retitled and rescoped (see the note at the top and section 2) | — |

The site table's growth and the metadata overhead are now visible rather than
assumed. The 2026-11-15 follow-up additionally fixed six edge cases:
Windows stack unwinding is opt-in; failed site growth cannot invalidate an
existing entry; each block records whether attribution succeeded; missed
live bytes are decremented on free/realloc; site lookups no longer scan the
whole capacity on each allocation; both site-table storage and pointer records
contribute to `tracking=`. Windows snapshots use `MoveFileExA` rather than
deleting the previous file before rename, and Linux detects ELF executable
type instead of guessing PIE from `-fPIC`/`-fPIE` compiler flags.

Host regression source: `tools/test_memprof_host.c`, executable by
`sh tools/test_memprof_host.sh` on Linux with GCC and binutils (no SDL needed).
This isolates the real profiler implementation with an engine-header stub;
full-game Windows and Vita runtime qualification are separate checks.

## 8. Complete process memory is a different tool

`GetProcessMemoryInfo` gives private committed bytes and working set for the
process, which is the honest denominator this profiler deliberately does not
provide. On Windows, UMDH (`gflags /i <exe> +ust`, then before/after snapshots)
captures allocations with stacks across process heaps and diffs them; WPR's
Heap Usage and VirtualAlloc Usage profiles do the same through ETW. Any of
those is a better cross-check than subtracting this profiler's total from a
working set.

If this is extended, report the categories separately instead of summing them:

| Category | Measurement |
|---|---|
| Engine heap | this profiler, per-site and full-width |
| Lua | `Memprof_LuaAlloc` totals; call-site needs `lua_getallocf` hooks |
| Third-party libraries | explicit hooks where the library offers them (libpng does) |
| Process memory | `GetProcessMemoryInfo` — private bytes and working set |
| Other heap / virtual | UMDH or ETW/WPR snapshots |
| GPU resources | engine-created textures, buffers, FBOs; driver under its own heading |

## 9. Where this leaves the memory work

- The instrument is done for the desktop targets and reproducible from the
  commands in section 1.
- The one large, actionable finding is **audio: 134 MB decoded eagerly and kept
  resident**. Streaming or lazily decoding SND, or skipping decode entirely when
  sound is disabled, is the change with the most headroom. It survives the
  fixes above unchanged: the ranking was re-derived after them and the top ten
  is identical.
- Everything else is ordinary resident cache; there is no per-frame leak to
  chase **within this scenario** (section 5 covers the limits of that claim).
- A Vita figure still needs a platform memory query (section 6). Until then,
  treat the ranking as the transferable result and the megabytes as desktop-only.

## 10. Commits

- `1f44181` — heap and Lua counters, `mem:` log line.
- `06bcd42` — libpng decode allocations counted.
- `d131cdf` — libpng callback types for the 64-bit kit (`png_alloc_size_t`).
- `dac7217` — call-site attribution, `MUGEN_MEMPROF`, the dump, and
  `tools/memprof_symbolize.py`.
- `219fa99`, `1316e3d` — original review fixes and tracked-heap scope.
- follow-up on `sdl2` — correct per-entry attribution under allocation failure,
  opt-in Windows stack capture, atomic Windows snapshot replacement, efficient
  site lookups, site-table metadata tracking, ELF executable-type detection,
  and a standalone Linux regression harness.
