# Lazy sound decoding: research and implementation plan

Status: opt-in cache implementation added after plan approval. Source retention,
global LRU, prepared/playing leases, frame maintenance, request/lifetime tracing,
budget replay, optional warm lists, bank teardown and format invalidation are
implemented. Eager mode remains the default. The lock-free decoder/worker backend,
automatic prefetch, and cross-device rollout gates below remain future work;
the synchronous mode does not claim timing equivalence. Research baseline:
`11f0344` on `work`. See BUILD-LINUX.md for controls and validation details.

## Recommendation

Add an **opt-in, global PCM cache for character sound banks**, retaining their
encoded WAV/Ogg payloads and decoding only requested sounds. Evict the least
recently used **unreferenced** decoded sounds. Keep system, common, lifebar and
storyboard sounds eager in the first implementation.

A first-use-only cache without eviction would eventually grow back toward the
current footprint. Streaming every effect through SDL_mixer's music API is
also unsuitable: it provides only one music channel and BGM already uses the
music subsystem.

Build the cache and lifetime machinery first. Treat synchronous first-use
conversion as a **memory/behavior prototype**, not a timing-safe default.
The current Ogg decoding path has measured long cold misses and holds the
mixer audio lock during decoding. Do not promise that moving it to a worker
will eliminate audio interruptions. Default activation requires the latency
work and validation described below.

## Research findings

### Current memory and decoding behavior

The saved GohanZ2 AI 7 / GokuZ2 AI 7 / hdbz_ball profile contains:

| Item | Measured amount |
|---|---:|
| Engine tracked live heap | 252.61 MiB |
| All retained sound PCM | 133.11 MiB / 555 sounds |
| Character PCM, separately measured with the same mixer settings | 127.65 MiB / 525 sounds |
| Encoded character payloads in the converted banks | 9.44 MiB |
| Gohan encoded payloads | 5.92 MiB / 306 sounds |
| Goku encoded payloads | 3.52 MiB / 219 sounds |

The remaining roughly 5.46 MiB of audio is system/common/lifebar data. These
are heap and asset measurements, not total process RSS or GPU memory. See the
[previous conversion report](../build/ogg-snd/REPORT.md).

`Snd_Parse` currently reads each subfile, calls `Snd_DecodeWav` (which detects
WAV versus Ogg), installs a `Mix_Chunk`, and discards the encoded bytes.
`Snd_DecodeOggResampled` uses SDL2_mixer's Vorbis loader and mixer-format
conversion, then copies PCM into the engine heap for consistent ownership
and profiling. WAV uses the configured SDL/libresample path. Both formats
therefore consume decoded memory for every loaded clip, including unused ones.

Character data survives between matches in `PlayerCache_Load` /
`PlayerCache_Release`; releasing a player does not imply `Char_Free` or
`Snd_Free`. A cache per bank, or clearing PCM only in `Snd_Free`, would not
provide a useful global bound across character changes.

### Playback contracts that the cache must preserve

- `Sound_PrepareSlot` gives a slot a chunk; `Sound_PlaySlot` can start it later.
  Prepared sounds need a reference even if `Mix_Playing(channel)` is false.
- `Sound_PlayOnSlot` also reads the entry's chunk directly after preparation.
  Resolving laziness in just one of these paths is insufficient.
- More than one channel may play the same chunk. A boolean `playing` flag
  cannot represent its lifetime. Looping sounds remain referenced until stopped.
- Slots and mixer channels are different identities. Channel reuse/replacement
  must not release a new sound because an old slot or completion event refers
  to the same channel number.
- `Sound_ReleaseStopped` currently polls mixer state only during preparation.
  Cache maintenance must also run once per frame so completed sounds become
  reclaimable even when no new sound is requested.
- `Snd_Free` frees chunks (stopping their mixer users), then separately frees
  engine-owned PCM when `chunk->allocated == 0`. Normal LRU eviction must not
  use that stop-on-free behavior to interrupt playing audio.
- `Char_LoadDef` calls `Snd_Parse` directly for both loose files and ZIP streams,
  then closes or reuses the input stream. Changing only `Snd_Load` misses the
  character loading path; retaining its borrowed `Stream *` is unsafe.
- The public `Snd`, `SndEntry`, `SoundSlot` layouts have reference 32-bit size
  assertions. Do not repurpose their unused integers as pointers or introduce
  another 32/64-bit layout-dependent cast.

### Decoder latency and the audio lock

I built a standalone `-O2` benchmark from the real sound/stream/resampler code,
without sanitizer overhead or engine source changes. It decodes all 525
character entries in bank order, frees each result, and repeats both banks
three times. Settings match the game: 44100 Hz, stereo signed 16-bit, 2048-frame
mixer buffer, libresample quality 0 for WAV. SDL uses the dummy audio driver.
There is no concurrent match or BGM in this benchmark.

| Entry format | Samples across 3 runs | Median | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|---:|
| Ogg | 1338 | 2.58 ms | 10.12 ms | 17.04 ms | 62.79 ms |
| WAV | 237 | 3.72 ms | 8.77 ms | 11.14 ms | 12.72 ms |

Each run's maximum was 57.49–62.79 ms. The largest decoded clip was about
2.60 MiB (Goku sound 3100,0). A 60 Hz frame is 16.67 ms: some misses can consume
multiple frames even on this Cloud host. Timings are provisional host results,
not portable handheld performance guarantees, cold-disk measurements, exact
lock-hold timings, or evidence of audible dropout.

There is a separate, confirmed locking issue: in the local SDL2_mixer 2.8.1
source, `Mix_LoadMusic_RW` in `src/mixer.c:700–749` locks audio across the full
`GetAudio` decode loop. `Mix_LoadWAV_RW` routes Ogg through this function.
Thus, running this same call on a worker still potentially blocks the mixer.
The source evidence and elapsed API times justify a rollout gate; actual
callback gaps and real-device audio must be measured separately.

Raw measurements and method: [research artifacts](../build/lazy-sound-research/README.md).

## Memory model and initial budgets

For these two characters, the expected replacement is:

`new heap ≈ old heap − character PCM + retained encoded payloads + cached PCM`

| Global character PCM budget to test | Projected tracked heap | Projected reduction |
|---|---:|---:|
| 8 MiB | ~142 MiB | ~110 MiB |
| 16 MiB | ~150 MiB | ~102 MiB |
| 32 MiB | ~166 MiB | ~86 MiB |

Start experiments at **16 MiB globally**, then use request traces to choose
between 8/16/32 MiB. These are capacity projections, not implemented results.
They exclude new metadata, decoder scratch/copies, pinned excess and additional
cached characters. The small eager non-character banks remain in the model.

The target includes ready character PCM, whether playing, prepared or idle.
Evict idle entries until it fits. If active/prepared sounds alone exceed the
target, allow and report that excess rather than cutting sounds off. An
oversized clip can play and be discarded immediately when its last reference
ends. Do not present the budget as a hard process-memory limit.

Encoded storage is separate. Retaining payloads of inactive cached characters
also costs memory, and unconverted WAV banks retain much larger source data.
Track this independently and test long character rotations. Reopening indexed
files or adding a bounded source cache is a later optimization if source
retention becomes material; do not silently drop borrowed memory/ZIP data.

## Proposed architecture

### Source retention and compatibility

1. Add `Snd_ParseWithPolicy(s, status, policy)`; keep `Snd_Parse` as the eager
   compatibility wrapper. Initially only the character loader opts into lazy
   banks. `Snd_Load`, standalone WAV, UI/fight/common/storyboard banks stay eager.
2. Store owned encoded bytes and validated metadata for each lazy subfile.
   Reuse the parser's per-subfile allocation by transferring ownership; do not
   retain both a whole-bank copy and duplicate entry payloads. Opening a small
   borrowing memory stream at decode time reuses the current WAV/Ogg dispatcher.
3. Validate fixed signature, count, offsets, full reads, cycles in the subheader
   chain, and payload extents with checked arithmetic before installing entries.
   Respect `Stream_OpenMemory`'s supported length range. Preserve entry ordering
   and existing duplicate `(group,number)` lookup semantics. Cache identity is
   bank generation plus entry slot, not just filename or group/number.
4. Support file-backed, buffered-memory and ZIP-loaded inputs by owning the
   payload after parse. The caller must still be free to close/reuse its stream.
5. Keep a stable sidecar registry for banks, entries and slot/channel leases.
   Preserve legacy struct layouts and the existing `Snd_Add` eager API. The
   sidecar must not retain raw pointers into relocatable `KeyedArray.data`.
   Entry lookup resolves a stable slot/index; `e->chunk` mirrors ready PCM only.
6. Use checked insertion in the new managed loader. The current `Snd_Add`
   always-success quirk cannot serve as an ownership-transfer check. All partial
   initialization paths must release sidecars, buffers, streams and chunks.

### Entry state and cache ownership

Use explicit states: `ENCODED`, `DECODING`, `READY`, `FAILED_PERMANENT`, `DEAD`.
In the first prototype `DECODING` is synchronous. Each sidecar records encoded
length/codec, decoded length, chunk/PCM ownership, last-use serial, lease count,
bank generation and mixer-format epoch. Record size_t/64-bit counts explicitly.

One acquisition function resolves a sound and returns a **lease**. A cache hit
reuses the chunk and updates recency. On a miss, decode to a private result,
check success, publish atomically on the game thread, acquire the lease, then
trim other unleased entries. Never expose a half-created chunk or evict the
entry being acquired. Keep expensive decoding outside any application audio
lock; note the remaining library-internal Ogg lock described above.

Both `Sound_PrepareSlot` and the direct play path must use the resolver. Keep a
lease through `Sound_PlaySlot`, and release it on replacement, explicit stop,
confirmed natural finish, or bank destruction. A preparation failure must not
leave a dangling slot pointer or stop an unrelated current sound.
`Sound_PlayOnSlot` reuses the lease acquired by preparation; it must not acquire
a second lease merely because it also looks up the chunk.

### Playback lifetime and thread rules

- **Initial implementation:** all cache mutation, decoding and destruction is
  on the game thread; SDL's callback only mixes already-pinned PCM.
- Track prepared and playing uses separately. A prepared lease survives a
  maintenance tick even when its channel is not yet playing. Track channel
  ownership/generation so two logical slots and channel reuse cannot unpin one
  another incorrectly. Exercise the existing free-channel/preparation quirk.
- Add once-per-frame maintenance near `Frame_Update` / `Memprof_Tick`; reuse or
  refactor `Sound_ReleaseStopped` with explicit prepared/playing states. Poll
  `Mix_Playing` and, when needed, `Mix_GetChunk` to confirm the expected user.
  Pin before starting playback and retain the lease until stopping is confirmed.
- A reference count covers overlap: finishing one of two users does not make
  their shared chunk evictable. Explicit stops/failed starts unbind exactly once.
- Prefer polling in the first version. If a completion callback is later used,
  it may run on the audio or calling thread; emit only a bounded notification.
  Do not decode, allocate/free cache objects, log or take SDL audio locks there.
  Use generation checks and overflow recovery; a channel number alone is not
  a sufficient completion token.
- LRU eviction only destroys entries with no leases and no mixer user. Free
  the chunk before engine PCM, preserving the existing ownership order.
- Bank destruction marks it dead, halts its users, clears slot/channel bindings,
  removes LRU entries, then frees PCM and encoded data. It must work when a
  character cache is purged while prepared sounds still exist.
- `Sound_Init` / `Sound_Deinit` owns cache initialization/shutdown. Invalidate
  decoded entries on audio-format/resampling-policy changes; PCM from an old
  device format must never be reused. Keep BGM lifetime and cache independent.
- Permanent decode errors log once per entry and return the existing playback
  failure result. Transient allocation failures clean up, trim idle PCM and
  permit a bounded later retry; they must not permanently poison a valid clip.
  Lazy decode necessarily delays some errors previously reported at bank load.

### Latency policy and later decoder work

For a controlled prototype, synchronous acquisition preserves request ordering
and starts a successfully decoded sound immediately, but can stall. Keep it
configurable through `[Sound]` (`Snd.LazyDecode`, `Snd.Cache.AutoGrow`,
`Snd.Cache.InitialMB`, `Snd.Cache.MaxMB`). The ARM64, Android and PS Vita
overlays now explicitly enable lazy loading with automatic growth from 8 to
20 MiB. Configs without these keys retain eager loading. Environment controls
have been removed; the latency-safe decoder backend remains pending.

Warm known hot sounds during loading, within the same budget, using measured
request traces. Warmup is a performance aid, not proof that all dynamic/random
PlaySnd expressions will hit. Inactive character banks should lose idle PCM
before active banks, without discarding needed source bytes.

If cold misses remain too slow, the next milestone must provide a decoder
adapter with independent per-job codec/resampler state that does **not** hold
the mixer lock across full conversion. Audit the existing linked codecs on
MinGW, Linux, Android and Vita before selecting a portable backend. A new codec
dependency or an SDL_mixer fork/upgrade is a separate design decision under the
repository's dependency constraints, not an assumed part of this cache.

Only after that audit should worker jobs, prefetch and completion publication
be added. Bound in-flight jobs/bytes, coalesce duplicate requests, and retain
bank-generation/mixer-epoch tokens so cancellation/unload cannot publish stale
chunks. Asynchronous misses introduce delayed-start/stop/priority semantics;
do not silently drop or defer first-use sounds as a supposed transparent fix.

True per-effect streaming would solve a different latency/memory problem but
requires a multi-effect mixer/decoder design. Keep it outside this first plan;
SDL_mixer's single music stream cannot substitute for concurrent SFX channels.

## Implementation sequence and acceptance criteria

1. **Trace sound use and establish baselines.** Add an opt-in request/lifetime
   trace with bank/entry, frame, owner/channel generation, prepare/start/finish/
   stop/free, loop, encoded/PCM bytes and decode latency. Capture varied seeded
   AI matches, character voices/supers, helpers, character rotations, menus and
   storyboards. Simulate 8/16/32 MiB LRU with actual pin lifetimes to estimate
   hit rates and warm sets. Preserve eager PCM hashes and output lengths.
2. **Implement cache ownership independently.** Add `soundcache.c/.h` (proposed)
   with leases, global LRU/accounting, generations and explicit failure cleanup.
   Unit-test fake chunks for overlap, preparation, loops, replacement, stop-all,
   unload, oversize, OOM and eviction. Check 32/64-bit layout assertions.
3. **Integrate lazy character parsing and playback.** Add owned encoded records
   and the eager compatibility wrapper; route every prepare/play access through
   acquisition. Preserve all existing decoder choices and chunk ownership.
   Add frame maintenance and bank/audio shutdown. Keep the feature opt-in.
4. **Measure memory and timing; solve the cold path.** Reproduce the same match
   with eager/lazy modes and identical seed/settings. Add bounded warmup, choose
   the budget from traces, and address the Ogg lock/decoder adapter if needed.
   The current 17 ms p99 / 63 ms maximum already rules out claiming a general
   stutter-free synchronous cold path. Do not enable it by default on this data.
5. **Qualify supported targets and document the mode.** Run sanitizer and replay
   comparisons, then real audio-device tests under active BGM and heavy SFX.
   Build/test MinGW win32/win64 and Linux; compile and run available Android/
   Vita paths, recording unavailable devices honestly. Promote configuration
   settings/defaults only after the following gates pass.

Proposed gates (targets for implementation, not achieved claims):

- All 5379 converted-bank entries remain loadable; compare each lazy decoded
  clip to its eager result from the same decoder, mixer format and quality.
- No sanitizer leak/use-after-free/double-release under delayed playback,
  shared chunks, bank unload, format reset, malformed data and forced failures.
- Idle cache bytes return under budget after users finish; pinned overflow is
  accounted for and reclaimed later. Stable memory after repeated character
  rotation; no hidden untracked PCM ownership.
- For the Gohan/Goku case, seek at least ~80 MiB measured live-heap reduction
  while testing a 16–32 MiB PCM budget. Report encoded bytes, cached/pinned PCM,
  scratch peaks, library allocation/RSS behavior and other heap separately.
- At 60 Hz, target p99 added sound-request work below 2 ms after warmup, no
  full-frame spikes caused by unexpected decoding, no missing/truncated/late
  cues or audible gaps under BGM. Also record cold-miss maxima, audio callback
  gaps/underruns and loading time. If a gate fails, keep eager as the default
  and describe the tradeoff rather than calling the mode timing-equivalent.

## Expected files and test coverage

| Area | Proposed changes |
|---|---|
| `src/sound.c`, `src/sound.h` | Parse policy, decode resolver integration, leases on all play/stop paths, teardown |
| `src/soundcache.c/.h` | New internal ownership/LRU/source registry; no legacy field reinterpretation |
| `src/char.c` | Opt-in policy for both loose-file and ZIP sound banks |
| `src/frame.c` | Once-per-frame completion reconciliation and trimming |
| `src/config.c` / docs | Configuration only if the prototype passes rollout gates |
| `src/memprof.c` or cache diagnostics | Separate source/PCM/pinned/temporary accounting; retain heap visibility |
| `tools/test_soundcache.*` | Deterministic cache/lifetime/failure tests |
| `tools/test_snd_ogg.*`, `tools/test_asset_errors.*` | Reuse real decoder/error-path coverage; extend for lazy source ownership |
| New replay/profiling helper | Access traces, LRU simulation and fixed-scenario comparisons |

Also test RAW engine-owned versus SDL-owned chunk destruction, empty sounds,
duplicate keys, overlapping same-sound channels, prepared-but-not-started slots,
channel replacement/failure, mute/unmute, panning, volume, loops and stop-all.
Load from files, BufferedRead, caller-owned memory and ZIP entries; close/reuse
inputs before first playback to prove ownership. Retain current lookup/order
semantics. Never make the audio callback perform cache work.

## Sources

Primary code reviewed in this checkout:

- `src/sound.c:170–255,291–378,452–599,609–755,934–995` — bank/slot ownership,
  decoder dispatch, eager parse and both playback paths.
- `src/sound.h:170–186,212–230` — legacy layouts and slot identity.
- `src/char.c:395–480,884–930,1934–1963` — player cache, direct parse and freeing.
- `src/stream.c:124–167,194–242` — borrowing memory streams, ZIP inflation,
  and buffer/stream destruction.
- `src/sctrl.c:3335–3400`, `src/storyboard.c:637–655`, `src/round.c:51–74` —
  actual preparation/play usage.
- `src/frame.c:322–325` — existing frame maintenance/profiler hook.
- Local SDL2_mixer 2.8.1 `src/mixer.c:640–779,993–1020` — Ogg full decode under
  audio lock and free-chunk behavior.

Official SDL2_mixer API contracts, consulted during research:

- [Mix_LoadWAV_RW](https://wiki.libsdl.org/SDL2_mixer/Mix_LoadWAV_RW): chunks
  are fully decoded; the music API streams but has one music channel.
- [Mix_QuickLoad_RAW](https://wiki.libsdl.org/SDL2_mixer/Mix_QuickLoad_RAW): exact
  mixer format and backing-buffer lifetime requirements.
- [Mix_FreeChunk](https://wiki.libsdl.org/SDL2_mixer/Mix_FreeChunk): freeing a
  chunk stops its channel users and can invoke completion callbacks.
- [Mix_ChannelFinished](https://wiki.libsdl.org/SDL2_mixer/Mix_ChannelFinished):
  callback may run on the mixer or calling thread; audio is already locked.
- [Mix_GetChunk](https://wiki.libsdl.org/SDL2_mixer/Mix_GetChunk): inspect the
  chunk associated with a physical mixer channel.
