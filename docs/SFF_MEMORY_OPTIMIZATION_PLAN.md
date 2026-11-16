# SFF loading and GPU residency: memory optimization plan

Status: **partially implemented** — P0, P1 and the desktop half of P3 are in the
loader/renderer (see "Implementation record" at the end). P2, P4-P7 and the
per-platform qualification they require are still open.
Source baseline: `sdl2` at `5ef86f3cd9652bc99c3bb06a3227355805791ada`.
Research date: 2026-11-16. Applies to Windows x86/x64, Linux x64/arm64,
Android and PS Vita. Preserve the configured resolution; qualify handhelds at
640x480. No changes to game coordinates, asset dimensions, palettes or sampling
are proposed as a memory shortcut.

## 1. Recommendation

Implement **bounded image residency**, not merely a flag that postpones decoding.
Keep lightweight sprite metadata and a recoverable source for each unique image;
decode a bounded working set on demand and upload a bounded GPU working set.
Evict decoded pixels independently of GPU textures. Reconstruct lost/evicted
textures from the source instead of keeping every decoded GLES backing forever.

The first implementation should retain compressed source bytes in memory and
resolve images synchronously. Add file-backed lazy reads and bounded CPU prefetch
only after correctness and measurements justify their complexity. Frequently
used images should be warmed before their first visible frame, not repeatedly
decoded/uploaded during every draw.

Order of work:

1. Fix cache identity and validate the existing lazy decoder path.
2. Measure unique encoded, decoded, recovery and GPU allocations accurately.
3. Separate source ownership, decoding and GPU upload; preserve a legacy mode.
4. Introduce a byte-budgeted GPU cache and demand decoding over resident encoded data.
5. Replace permanent decoded recovery copies with source-based reconstruction.
6. Extend to SFF v1/PCX and seekable file sources; evaluate prefetch last.

This targets image residency, not batching, texture atlases or model textures.
`docs/BUILD-LINUX.md` already records an atlas experiment rejected for pixel
changes. Do not reintroduce it under the label of memory optimization.

## 2. What the code currently does

References below are relative to the baseline, not future implementation promises.

| Stage | Current behavior | Memory consequence |
|---|---|---|
| File opening, `src/sff.c:Sff_Load` (906) | `BufferedRead` reads the entire file into a temporary memory stream; otherwise uses stdio | Buffered mode can overlap a full file copy with the retained SFF block, decoded images and uploads |
| ZIP, `src/stream.c:Stream_ZipLoad` (141); `src/char.c` sprite load (830) | Inflates the entire SFF ZIP entry into a memory buffer; `Sff_LoadStream` then loads from it | ZIP input is not random-access image streaming; archive inflation can overlap another copy of the data block |
| SFF v1, `Sff1_Load` (387) | Decodes each non-linked PCX immediately and calls `Sprite_Init(OWN | DECODE, RAW, ...)` | Eager PCX decoding; raw pixels may immediately become textures on a GL renderer |
| SFF v2 directory, `Sff2_Load` (601) | Allocates sprite/palette node arrays and output containers; discards directory arrays after loading | Metadata indexing exists during load but is not retained as a reload source |
| SFF v2 ldata | Full load copies the entire ldata block into `SpriteSet.data`; selected load packs palettes and selected ldata payloads | Payloads and palettes can borrow pointers into that retained allocation |
| SFF v2 ldata decode policy (781) | Uses `SPRITE_INIT_DECODE` only when `g_gfxFlags & 1` | Some v2 images already remain compressed; desktop `Gl_Init` sets that bit, while GLES `Gl_Init` does not set it |
| SFF v2 tdata (758) | Allocates an encoded temporary, calls `Sprite_Init(COPY | DECODE, ...)`, then frees the temporary on success | Eager decoding even where ldata remains lazy; input and decoded/upload buffers overlap |
| `src/sprite.c:Sprite_Init` (73) | DECODE allocates a complete decoded image; eligible raw GL/OpenGLScreen images upload immediately; oversized images take the existing downscale path | Source, full decoded image, scaled temporary and driver copy can overlap |
| GLES persistent textures, `Sprite_Init` (198); `GlTex_SetBacking` (383) | Transfers an owned decoded/scaled buffer or copies borrowed pixels into a recovery backing | Correct context recovery, but a persistent decoded CPU copy remains beside the texture |
| Existing lazy pixels, `Sprite_GetPixels` (563), `src/misc.c:SpriteCache_Get` (42) | Decodes compressed sprites into global `g_spriteDecompBuffer` on demand | Bounded decoded pool, with eviction and reusable payload addresses; returned pointers are not permanent allocations per image |
| GLES non-texture sprites, `gles_stream_slot` (243) | Eight LRU texture slots, keyed by decoded address, width, height, bpp and context generation | Slot-count bound, not byte bound; a few large sprites can use substantial GPU storage |
| Desktop/Vita non-texture sprites, `Gl_DrawSprite` | Uses scratch textures when a sprite has not become a persistent texture | Residency changes must not restore the old upload-every-draw bottleneck |
| Destruction, `SpriteSet_Free` (452) | Honors set borrowing/refcounts; frees owned sprites/palettes, then `set->data`; GLES invalidates streaming keys | Ownership is intertwined; dropping the data block without detaching borrowers is unsafe |

### Important distinctions

- **Lazy decode already exists**, but it is format/path dependent. The new work
  must improve and unify it rather than duplicate the decompression cache.
- Lazy decode, lazy file I/O, lazy GPU upload and GPU eviction are separate policies.
  Turning on one does not automatically reduce the other three memory pools.
- Linked sprites copy image pointers and ownership flags, but may have different
  axes and palette selection. Their physical image must be accounted once.
- `Sff_LoadStream` restores the caller's position. Its caller can close, unload
  or reuse that stream immediately afterward. A delayed decoder cannot retain a
  borrowed `Stream *` or memory pointer and assume it stays valid.
- `Sff_LoadSelected` is not a general-purpose lazy loader. It exposes a selected
  result set; v1 intentionally skips linked entries. Preserve its public behavior
  unless a separately marked compatibility fix is approved and tested.
- `Precache_ThreadMain` loads whole characters, not just sprite frames. Config
  currently disables this character precache for full OpenGL. Do not turn it back
  on as an image-prefetch implementation: `Sprite_Init` can call GL and the old
  character cache has its own synchronization/lifetime constraints.

## 3. Correctness prerequisites and research findings

### C1. Decoded-address GPU keys are unsafe under pool reuse

`DecompBuffer_Get/Alloc` (`src/spritecodec.c:1029,1150`) can evict an image and
return its payload address for another image. `gles_stream_slot` treats the same
address/dimensions/depth as the same image. A different source of equal dimensions
can therefore get a cache hit and skip its upload. The comment claiming that
SpriteCache buffers belong to one sprite for its lifetime is not a valid invariant.

Confirmed with a temporary Linux x64 host probe using the production allocator
and GLES slot lookup (no GPU mock output presented as a screenshot):

1. Create a 1024-byte decompression pool.
2. Allocate 800 payload bytes for source A; record its pointer as a valid GPU key.
3. Allocate 800 bytes for source B, forcing reuse, with the same 40x20 indexed extent.
4. The pool returns A's former address; the GLES lookup reports a hit for B.

Observed: `reused pool address=1; GLES cache reports hit=1 for a different source`.
This proves a bad cache decision, not a measurement of its frequency in real games.

**Required fix before increasing eviction:** key uploaded pixels by stable asset
identity + canonical image ID + content generation + decode/upload interpretation
+ GL context generation. Do not use a decoded address as the asset identity.
Mutable surfaces additionally need a write/content generation, or must bypass the
immutable-image cache. Capture image identity before acquiring decoded bytes.

### C2. Validation must cover demand decoding too

`Sprite_ValidateCompressed` is called from the eager DECODE path in `Sprite_Init`.
`SpriteCache_Get` dispatches directly from a four-byte decoded-size prefix and does
not run that validator. Moving more sprites to the lazy path must not bypass the
recent eager decoder bounds checks.

Check the full encoded span, codec, dimensions, bpp, declared output length and
checked allocation size before any cache allocation or decoder call. Read the
little-endian prefix without an unaligned integer dereference. Handle short
input, unsupported formats, output-size mismatch, failed PNG reads and decoder
failure explicitly; never publish a partially decoded cache entry.

`Png_ReadStream` (`src/pngio.c:246`) also uses global `g_pngStream` and lacks a
local recoverable setjmp boundary; `Png_Decode` drops the image-read result.
Refactor the sprite decoder contract to return success and isolate per-decode
state before asynchronous decoding. This is a prerequisite, not a claim that
model-only `Png_DecodeRGBA` is a drop-in replacement: converting indexed sprites
to RGBA would change palette behavior and multiply storage.

### C3. Current SFF memory logs under-report ownership

`SpriteSet_MemUse` (`src/sprite.c:803`) does not include the retained `set->data`
block, row tables, container capacity, shared decode-pool allocation or GLES
recovery backing. It uses legacy-size assumptions for some texture bookkeeping.
Do not use its `sys` number as process memory or as the proof of savings.

Use ownership-based counters and the existing `memprof` hooks. `memprof.h`
explicitly distinguishes tracked engine heap from RSS/working set and driver
allocations; libpng's custom allocator is tracked, but that does not make GPU
storage, ZIP-library internals and OS caches visible.

### C4. Releasing the bulk block requires detaching all borrowers

SFF v2 palette entries may refer directly into `set->data`; raw/compressed sprite
payloads may do the same. SFF 2.00 alpha normalization currently modifies bytes in
that block. Copy/normalize the relatively small palette data into independently
owned storage and retain/reference every required encoded span before releasing
the block. A full-file mmap is not a safe substitute for writable palette bytes.

### C5. A texture count is not a memory budget

Eight GLES streaming slots can hold eight very large images. Persistent textures
and recovery backings are outside that eight-slot limit. Introduce shared byte
budgets across all eligible SFF textures, not just a smaller slot count. Keep
render targets, depth buffers, model textures and unrelated font caches separately
accounted so SFF eviction never deletes a live output attachment.

## 4. Memory model and measurements

Track these categories separately and by unique owner:

| Symbol | Category | Lifetime/accounting rule |
|---|---|---|
| M | Metadata, directories, link tables, cache records and containers | Resident per loaded asset; include allocated capacity and ABI-dependent sizes |
| E | Owned encoded/raw source payloads and normalized palettes | One charge per canonical source owner, not one per alias |
| F | Owned full-file buffer, inflated ZIP entry and file-read windows | Temporary allocations; measure mappings/resident pages separately with OS metrics; overlapping copies matter at load |
| D | Decoded CPU pixels and their row tables | Count allocated pool capacity and used payload separately; pins prevent eviction |
| R | Decoded context-recovery backing | A separate category until source-based reconstruction replaces it; never count transferred ownership twice |
| T | Decoder internals, row buffers, premultiply/downscale scratch and pending upload jobs | Reserve before starting work; include every concurrent job |
| G | Logical GPU texture/depth allocation estimates | Include backend allocation layout where known; driver storage can differ from width x height x bytes-per-pixel |

CPU application payload at time t is approximately `M + E + F + D + R + T`.
Record its simultaneous peak, not the sum of independently observed category peaks.
Report G separately: on UMA devices, adding it to RSS as if it were separate VRAM
is misleading. Deleting a GL texture may defer physical reclamation in the driver.
The engine can enforce its CPU reservations and logical residency ledger, not an
exact cross-driver physical-GPU-memory ceiling.

Arithmetic illustration, **not a measured game**: 1000 unique 256x256 indexed
images require 62.5 MiB of decoded pixels; retaining all such pixels alongside
one-byte GPU images represents another 62.5 MiB of logical GPU texels. A working
set of 64 images is 4 MiB in each category. This is why bounding working sets can
matter much more than shaving a row-pointer allocation. Actual savings depend
on source E, backend layout, palette storage and whether the current path retains
all decoded images in the first place. RGBA pixels use four times the indexed
payload; do not expand indexed SFF images merely to simplify the cache.

### Baseline capture before changing behavior

For title/select, two-character fight, heavy effects, stage transition, character
replacement and repeated context loss, collect:

- source SFF version, file size, unique/linked image count, formats, declared
  decoded bytes, largest single image and palette count;
- M/E/F/D/R/T live and peak bytes, G logical live/peak, evictable versus pinned;
- decode count/bytes/time by codec, cache hits/misses, rereads and GPU reuploads;
- bytes read/decompressed for ZIP sources and pending-job reservations;
- load time, first-use stall p50/p95/max, frame time p50/p95/p99, `perf:`/`batch:`
  fields, and process/private memory with the OS measurement method named;
- current `BufferedRead`, `PlayerCache`, `Precache`, decompression-buffer size,
  renderer, platform heap limits and concurrent non-SFF allocations.

No representative game SFF corpus or device memory traces were available in this
research checkout. Select real assets for the baseline before choosing defaults;
there is no measured memory-reduction percentage in this proposal.

## 5. Proposed ownership and API boundaries

Add private sidecars instead of enlarging original 32-bit `Sprite`, `SpriteSet`
or `GlTex` layouts. Key a sidecar by set identity/generation and stable original
sprite slot, not `Sprite *`: container growth can move the underlying array.
Borrowing sets resolve through the real owning asset.

Suggested records (names are design proposals):

- `SffSource`: reference-counted source owner, immutable asset generation,
  source kind (owned compressed blob, owned file handle, archive entry locator,
  or an explicitly lifetime-managed memory span), source extent and base offset.
- `SffImage`: canonical image ID, codec, dimensions/depth, encoded span,
  checked decoded extent, original decode policy and reconstruction recipe.
- `SffSpriteView`: logical group/number/slot, canonical image ID, axes, dimensions,
  scale and palette linkage; aliases share pixels, not necessarily view metadata.
- `PixelLease`: decoded view, pitch/row metadata, pixel format, generation and
  pin/release token. Its pointer must remain valid until release.
- `TextureLease`: backend texture, context/content generation and a pending-draw
  reference. Residency may end only after queued work using it is flushed.

Conceptual entry points:

```c
int SpritePixels_Acquire(SpriteSet *set, int slot, PixelLease *out);
void SpritePixels_Release(PixelLease *lease);
int SpriteTexture_Acquire(SpriteSet *set, int slot, TextureLease *out);
void SpriteTexture_ReleaseAfterDraw(TextureLease *lease);
```

These are not implemented APIs. Keep existing public calls as compatibility
wrappers while migrating their consumers. Do **not** put an opaque ID into the
legacy `pixels` field or leave metadata-only sprites with NULL pixels while old
code still dispatches directly on `format`. Audit direct pixel/row access in
sprite drawers, `bg.c`, software blending/rasterizers, fonts, Lua-facing helpers,
copy/clone paths, precache and destruction before enabling a metadata-only policy.
`Sprite_GetPixels` currently exposes an unpinned pointer; it cannot provide the
new eviction guarantee without a scoped acquisition or equivalent lifetime rule.

### Source indexing and links

Index metadata and resolve links without decoding pixel planes. Validate node
counts, subheader chains, span containment and link graph depth/cycles. Preserve
original node identity even when the public sprite table contains only selected
entries; a link is not an index into the compacted selected table.

For SFF v1, determine shared/previous palette dependencies during the metadata
pass. Extract/normalize palettes without decoding every pixel plane; bounded
payload scanning may still require I/O. Preserve axes, duplicate key handling,
image order and valid legacy link behavior. A newly supported malformed/forward
link or a change to v1 selected-link behavior is a separately documented fix.

For SFF v2, treat ldata/tdata as source ranges and preserve 2.00 versus 2.01 alpha
rules. Embedded SFF streams must retain a source base. Existing tdata addressing
has a documented legacy absolute-position quirk: freeze tests before deciding
whether to preserve it or fix it in a separate compatibility change.

## 6. Decode, upload and eviction policy

Keep source, CPU decode and GPU residency states independent:

| Event | Source | CPU decoded entry | GPU entry |
|---|---|---|---|
| Load metadata | Retained or reopenable | Absent | Absent |
| First CPU read | Acquire exact source span | Validate, reserve, decode, pin | Unchanged |
| First GPU draw | Acquire source only if no resident texture | Decode/pin as needed | Reserve, upload, publish only after success |
| CPU eviction | Remains recoverable | Release only unpinned entries | May stay resident |
| GPU eviction | Remains recoverable | Independent; need not be kept | Flush queued users, delete logical ownership safely |
| Context loss | Unchanged | Unchanged | Invalidate names/generation without GL deletes on the dead context |
| Restore | Unchanged | Acquire hot images as needed | Re-upload only the required working set |
| Set/asset free | Wait/cancel jobs and pending users; drop final source ref | Evict its entries after pins release | Flush users; release live-context objects or invalidate dead ones |

### CPU decoded cache

Start by adapting the existing bounded pool, or replacing it behind the same
facade; do not run an unbounded new image cache alongside it. Its current weighted
expiry allocator need not be rewritten just to call the policy LRU. What matters
first is correct identity, pinning, a byte budget and oversized-request handling.

Reserve output plus decoder workspace before malloc. On pressure, evict unpinned
entries; never evict a destination still used by a software blit. A single image
larger than the soft cache budget may use a bounded transient lease if it fits
the hard CPU reservation, then be released after use. If it cannot fit, report an
explicit asset/admission error; do not overrun the pool or silently draw blanks.

Cache decoding failures with an asset generation and reason, not success-shaped
empty pixels; avoid retrying a corrupt image every frame. Source bytes changing
on disk must not silently mix old metadata with new payloads: pin the original
source or detect/reject the changed generation.

### GPU cache

Use a global byte-budgeted cache for immutable SFF images, canonical-image keys
and backend-specific storage estimates. Share linked images. Indexed textures
must stay independent of draw-time palette remapping/PalFX; only a backend that
bakes a palette into pixels includes that palette/content generation in its key.

A cache hit must require no CPU decode. Resolve the logical image ID before
`Sprite_GetPixels`; otherwise GPU-resident images still thrash the CPU pool.
For ordinary client-memory texture uploads, staging can be released after the
upload call returns; no unconditional `glFinish` is needed to retain a private
application copy. A future asynchronous/PBO path needs its own completion and
buffer-lifetime rules. The CPU copy retained today is needed by this engine's
recovery policy, not as a general requirement to keep drawing an uploaded texture.

Use cached `GlTex` handles where compatibility requires a stable proxy, and
validate their generation rather than treating `id != 0` as sufficient forever.

Keep render targets, writable surfaces and screen buffers out of immutable-SFF
LRU eviction. Flush the existing GLES deferred queue before deleting/reusing a
texture or changing its contents. Preserve batch ordering and sampler state.
The eight-slot streaming cache must be migrated into, or explicitly charged
against, this budget rather than remaining a hidden second residency pool.

### Recovery backing

Replace a permanent decoded `GlTex_SetBacking` allocation with a source/image
reference and reconstruction recipe for **reloadable SFF textures only**. The
recipe includes format, the existing downscale factors/rounding and backend alpha
conversion. It must reproduce the exact uploaded bytes after CPU eviction.

Do not remove recovery data for generated images, user-created surfaces, cached
TTF text, render targets or model textures without their own recovery design.
Reuse the existing context generation and invalidation hooks. Retaining compressed
source is useful precisely because uploaded client bytes and GL texture lifetime
are separate; successful upload does not establish a permanent recovery source.

## 7. Encoded data and file I/O strategy

| Strategy | Benefit | Cost/constraint | Decision |
|---|---|---|---|
| Current bulk block + deferred decode | Smallest behavioral change | Retains all source bytes; ownership/palette borrowing remains | Baseline/rollback mode |
| Compact owned encoded payloads, shared by links | Bounds decode residency; works after caller closes streams | Encoded total remains resident; copying can temporarily increase peak | First production candidate; transfer ownership only via explicit owning APIs |
| Owned seekable file handle + indexed reads | Can avoid resident full encoded file | First-use I/O, lifetime/handle limits, removable storage failures | Optional next step for large SFFs |
| mmap/file mapping | Can reduce explicit copying for immutable spans | Address-space pressure on win32, platform differences, page-fault/RSS behavior | Optional desktop experiment, not required for Vita/Android |
| ZIP entry retained once | Avoids repeated archive inflation | Still holds the inflated SFF entry | Safe ZIP fallback when admitted by budget |
| ZIP spool/indexed source | Can bound RAM used by the inflated entry | Disk space/startup I/O; sandbox/path cleanup; CRC and cancellation | Later experiment; spool only under project/platform-owned storage |
| Reinflate ZIP entry for each image | Minimal permanent entry buffer | Potential O(file size) work per miss | Reject as default |

`BufferedRead=0` removes one temporary file-sized allocation; it does not remove
`SpriteSet.data` or automatically make decoding lazy. Treat it as an A/B baseline,
not as the complete fix.

When compacting encoded data, copy normalized palettes first and release old
bulk storage only after every payload borrower is moved or referenced safely.
Prefer a new owning load path to stealing memory from the existing borrowed
`Sff_LoadStream` API. Measure compaction peak; on a tight heap, reading ranges
straight into final source storage is safer than full-file-plus-full-compaction.

Use checked 64-bit source offsets and size arithmetic internally. Existing
`Stream_Tell/Seek` use `long`; Windows x64 still has 32-bit long. Do not claim
large-file support just because a new sidecar uses uint64_t. Add a real portable
wide-offset reader or reject spans beyond the actual backend's representable
range. Never pack pointers into int IDs or persist process pointers as source keys.

## 8. Codec-specific policy

| Input | First implementation | Later memory reduction |
|---|---|---|
| RAW indexed/RGB/RGBA | Borrow a pinned source span where safe; respect pitch/channel order | Avoid a redundant owned decoded copy; GPU still needs residency accounting |
| RLE8/RLE5/LZ5 | Validate and decode one complete admitted image into a lease | Reuse a bounded scratch/pool; no requirement to invent a random-access LZ decoder |
| PNG8 | Preserve palette indices and exact depth expansion | Row/tile decoding only after interlace and error-path tests |
| PNG24/32 | Decode to the existing packed representation | Non-interlaced row staging may reduce temporary CPU peak |
| PCX/SFF v1 | Metadata/palette scan, then on-demand full-image decode | Bounded row staging and exact legacy downscale reproduction if worth the I/O |

SFF RLE/LZ and PNG are source encodings, not GPU block-compression formats.
They cannot simply be passed to a compressed-texture upload API. Hardware
compression or palette expansion is a separate, platform-specific fidelity and
memory experiment, not part of the first candidate.

Do not substitute model `Png_DecodeRGBA` for all SFF PNG formats. PNG decoder
instances must be per-job or serialized until global stream state is removed.
A bounded worker count does not make a non-reentrant decoder safe.

Row-wise PNG decoding can reduce staging for non-interlaced uploads, but does
not reduce the destination GPU allocation. It can increase GL call overhead;
interlacing needs multiple passes/retained partial state, not a simplistic
one-row-at-a-time claim. Keep full-image fallback inside the admission budget.
Use existing library versions; no new image dependency is required by this plan.

Do not automatically resize all large images to fit a memory budget. Existing
sprite downscale behavior and scale fields must remain pixel-equivalent; new
quality-loss policies would require separate user-visible opt-in work.

## 9. Warming and threading

Phase one is synchronous, with admission before work and no GL calls from workers.
Warm portraits, loading screens, the initial stage and the immediately needed
animation frames during existing loading transitions. Preserve arbitrary runtime
sprite access: AIR analysis is a prefetch hint, never a proof that other images
are unused. Helpers/explods, alternate states and scripts can select more images.

Only later add a small cancellable CPU I/O/decode queue. Reserve encoded input,
output and workspace for each job; queued jobs count against the budget. A full
queue must apply backpressure rather than allocate another full image. Prioritize
visible/next-use images; cold optional warming yields to an actual demand miss.

Publish completed decoded entries atomically. Give each job an asset-generation
reference and cancel/join it before final source destruction. Deduplicate
simultaneous requests for the same canonical image. Never hold the cache lock
across file I/O, PNG longjmp cleanup or GL submission. Keep upload, deletion,
context generation and draw-queue ownership on the renderer thread.

Do not enable the existing whole-character `Precache` thread as a shortcut.
Loading characters also consumes CNS/state/sound memory; SFF budgets cannot
promise to cap the entire player cache. Keep reference characters pinned and
consider eviction of inactive whole characters separately from image LRU.

## 10. Platform qualification and budget selection

| Target | Main concern | Required qualification |
|---|---|---|
| Windows x86 | Address-space fragmentation and original struct ABI | Checked sizes/offsets, no pointer truncation, repeated allocate/evict/reload, compare with reference pixels/logs |
| Windows x64 | LLP64: long is not pointer-sized | Same image/alias ownership tests; wide source offsets and uint64_t accounting |
| Linux x64 GL | Existing eager decode/upload behavior changes | Explicit policy A/B; exact image output and no recurring hot-image upload |
| Linux arm64 GLES | UMA, storage latency, reusable decoded pool and upload stalls | Stable cache keys, byte budgets, RGB/indexed correctness, context recovery, five alternating release-build pairs |
| Android GLES | Lifecycle resets and memory-pressure interruptions | Suspend/resume, repeated loss, cancellation, no stale GL names, hot-set-only reupload and native APK/device qualification |
| Android System/OpenGLScreen | CPU pixels may be required by the software stage | Preserve row/lease lifetime for software paths, screen-copy semantics and renderer switches |
| PS Vita/VitaGL | C heap competes with GXM/texture allocations | Respect `MUGEN_VITA_HEAP_MIB` (currently 64 MiB default); measure actual GPU layout, driver staging, linked lifetimes and low-memory behavior on device |

Budgets are proposed policy controls, not new supported `mugen.cfg` keys yet.
Start with explicit experimental CPU-decoded, encoded-source, pending-work and
GPU-image budgets plus one total CPU admission ceiling. Tune from measured
working sets and the available heap after engine/audio/models/screens, not from
physical RAM size alone. Test multiple budgets; do not publish a universal
"safe Vita cache size" without measuring a full match.

Keep screen/depth and generated resources reserved outside evictable SFF space.
Report budget refusals by category and largest request. Limit outstanding GPU
uploads/retired resources; deletion can leave driver allocations pending. Avoid
adding unconditional `glFinish` merely to make memory graphs look flat.

## 11. Implementation phases and exit gates

| Phase | Files/components | Deliverable | Exit gate |
|---|---|---|---|
| P0: freeze behavior and repair identity | `misc.c`, `spritecodec.c`, `gles_render.c`, fixtures | Stable source/content keys; validation/error propagation on lazy decode; pool-reuse regression | Different sources reusing one address never share stale uploaded pixels; malformed inputs never publish entries |
| P1: accounting | `sff.c`, `sprite.c`, `gles_render.c`, `memprof.*`, `batchprof.*` | Unique-owner ledger and snapshots at load/unload/pressure/loss | Alias counts reconcile; failure/cancel returns counters to baseline; memprof versus logical GPU distinctions documented |
| P2: source/index ownership | `sff.c`, `sff.h`, new private source/asset module, `stream.c`, character/selected loaders | Indexed canonical sources and independently owned palettes; legacy path retained | Stream-close/reuse, embedded base, v1/v2 links and palette output match baseline |
| P3: synchronous lazy residency | pixel facade, software consumers, desktop/GLES/Vita upload paths | Byte-budgeted decoded/GPU caches, leases, one-image admission | Hot draws hit GPU without decode; visible output identical under forced eviction; no upload-per-draw regression |
| P4: source-based recovery | GLES backing registry/context hooks; source refs | Recoverable SFF textures without permanent decoded backings | Evict CPU and lose context, then render original pixels; generated resources retain valid independent backing |
| P5: format/source expansion | PCX/v1 scanner, selected loads, seekable reader/ZIP strategy | v1 coverage and optional file-backed reads | Source/palette dependencies correct; measured heap-peak reduction with acceptable first-use latency |
| P6: bounded warming | loader transitions, new CPU job queue, reentrant codecs | Deduplicated cancellable prefetch; renderer-thread uploads | Peak includes all jobs; cancellation/free safe; latency benefit shown on target hardware |
| P7: rollout | policy/config documentation, CI/platform gates | Per-platform qualified defaults and one-switch rollback | Numerical comparison below passes; no platform enabled only on host evidence |

Do not couple every phase into a single port-wide rewrite. Each phase should be
reviewable with synthetic fixtures and an independently recorded before/after.
Every intentional fidelity change must be marked as a fix; keep original address
comments and public layout checks intact. New engine modules must be wired into
Makefile and both platform CMake projects and their host harness link lists.

## 12. Test and performance acceptance

### Permanent synthetic tests to add

- SFF 1.01, 2.00 and 2.01; RAW, RLE8, RLE5, LZ5, PNG8/24/32; PCX indexed/RGB.
- Full and selected loading, embedded stream at nonzero base, caller memory/file
  closed immediately, ZIP entry unloaded/reused, and borrowed SpriteSet lifetime.
- Linked sprites/palettes, long link chains/cycles, invalid links, alias axes,
  different palettes over shared indices, duplicate keys and selected dependencies.
- Exact eager-versus-lazy decoded bytes, palettes, row offsets, render pixels and
  destruction counts; compare alpha, masks, straight/premultiplied upload policy,
  odd-width RGB pitch and the existing oversized-sprite downscale recipe.
- A 1024-byte pool with two 800-byte equal-extent images: force pointer reuse and
  assert different source keys cause a real upload/new image, not a stale hit.
- Decode only one requested image from a many-image file; assert load itself
  performs zero pixel decodes in lazy mode. Count canonical images once.
- Acquire source pixels and destination pixels simultaneously under a tiny CPU
  budget; pins prevent aliasing/eviction until the software operation finishes.
- More than eight textures with widely different extents; byte-limit eviction,
  hot-set protection, draw-queue references, failed uploads and ID/pointer reuse.
- Fault injection at every ownership transfer/allocation/I/O/decode step; truncated
  PNG/RLE/LZ, bad decoded prefix, bad CRC, invalid dimensions, arithmetic overflow,
  short read, absent/replaced source, oversized single image and empty data block.
- Context loss after CPU eviction, failed restore, successful restore, renderer
  switch, queued draw followed by set free, and cancellation during decode.
- x86/x64/arm64 alignment, high-bit pointers, full-width byte totals and the actual
  backend's file-offset limit. Distinguish an unsupported large source from wrap.

Existing `test_asset_errors.sh`, `test_gl_errors.sh`, `test_gles_smoke.sh`,
`test_memprof_host.sh`, SDL2 helpers and render fixtures remain gates, but they
are not substitutes for a lazy-source/residency integration suite. Run ASan/UBSan
on host; document sanitizer limitations and real device gaps.

### Performance and release gate

Use the current eager/legacy policy and the candidate against the same binaries'
asset set, game settings and deterministic game state. Preserve 640x480 handheld
settings and existing nearest/filter/clamp behavior. Compare stable simulation
ticks and repeat screenshots, not wall-time-dependent transitions. Alternate at
least five release-build runs per side on the real device with clocks named.

Acceptance criteria:

1. Exact decoded/palette fixtures and approved pixel comparisons remain identical.
2. After warm-up, an unchanged resident GPU image causes zero decodes and zero
   pixel uploads; repeated drawing of a tiled sprite does not multiply uploads.
3. CPU reservations stay within configured hard admission limits, with pins and
   all pending work counted; GPU logical residency follows its separate budget.
4. For each memory-heavy test asset, report absolute and percentage changes in
   simultaneous CPU peak, steady heap, process memory and logical GPU bytes.
   Set numerical reduction targets only after P1 establishes the baseline.
5. Record first-visible-frame and p99 frame stalls. Lower heap usage alone is not
   a pass if cold I/O/decode makes fights visibly hitch or worsens steady cadence.
6. Repeated load/unload/evict/restore reaches a stable baseline with no growth,
   stale cache hits, missing sprites or leaked source/file/job references.
7. Keep legacy rollback until every enabled platform has native runtime evidence.

## 13. Evidence and remaining work

Completed for this document:

- Inspected the baseline loader, sprite ownership, palettes, decode pool, PNG,
  stream/ZIP, desktop/Vita and GLES upload/recovery code, existing memory profiler
  and prior atlas/batching performance notes.
- Ran `tools/test_asset_errors.sh` on Linux x64: eight asset/error cases passed.
- Ran the production allocator + GLES slot-lookup reuse probe described in C1.
  It used CRT allocation directly to isolate cache mechanics, not to measure
  memprof totals; no GPU was needed and no rendering result is claimed.
- Checked the public sources below for the format's intended lazy-decode tradeoff,
  texture ownership, context loss, threading and row decoding.

Not completed: real-game SFF inventory, end-to-end process/GPU memory captures,
new lazy-loader implementation, performance comparison, Windows native tests,
Android APK/device checks, or Vita hardware qualification. No engine/configuration
behavior was changed to write this plan.

## 14. Sources

Implementation references (pinned to the reviewed baseline):

- [SFF full/selected/file loaders](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/sff.c).
- [Sprite initialization, ownership, pixel access and estimates](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/sprite.c).
- [Legacy sprite cache](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/misc.c) and [codec/cache allocator](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/spritecodec.c).
- [GLES source keys, recovery backings and upload](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/gles_render.c).
- [Desktop/Vita texture upload](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/glrender.c), [pinned VitaGL texture implementation](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/third_party/vitagl/src/source/textures.c), and [Vita heap configuration](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/port/psvita/CMakeLists.txt).
- [PNG codec](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/pngio.c), [streams](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/stream.c), [character source lifetime](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/char.c).
- [Profiler coverage](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/memprof.h), [precache](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/src/playerstate.c), and [previous renderer measurements](https://github.com/leonkasovan/mugen-unleashed/blob/5ef86f3cd9652bc99c3bb06a3227355805791ada/docs/BUILD-LINUX.md).

External primary references (recommendations above are project-specific inferences):

- [Elecbyte Sprmake2 documentation](https://elecbyte.com/mugendocs/sprmake2.html): describes `sprite.decompressonload` and the memory/draw-cost tradeoff, as well as linking duplicate images/palettes. Retrieved via search index; direct site access was unreliable. Not treated as a complete SFF 2.01 binary specification.
- [Khronos OpenGL ES specification, texture image specification section](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf): client pixel data is copied into GL-managed image storage; distinguish upload source lifetime from application reconstruction needs.
- [Android renderer lifecycle reference](https://developer.android.com/reference/android/opengl/GLSurfaceView.Renderer#onSurfaceCreated(android.opengl.GL10,%20android.opengl.EGLConfig)): resources associated with a lost EGL context need reconstruction. This SDL application uses its own lifecycle hooks; the Java callback is not a proposed integration point.
- [SDL2 development FAQ](https://wiki.libsdl.org/SDL2/FAQDevelopment): video/render ownership constrains worker design; use CPU-only work and renderer-thread uploads here.
- [libpng manual](https://www.libpng.org/pub/png/libpng-1.2.5-manual.html) and the pinned `third_party/libpng-1.2.38/pngread.c`: full-image/row reading and interlace handling inform the optional staging investigation. Validate against the actually linked libpng  version on each target; the repository and distro builds do not all use the same version.

## 15. Implementation record

Implemented on 2026-11-16. Scope: the parts of P0 and P1, and the desktop half
of P3, that can be built and measured on Windows x86 without new platform work.

### What changed

| Plan item | Change | File |
|---|---|---|
| C1 identity | The desktop and GLES stream texture caches key an entry on the image's own source span, codec and extent (`SpriteTexKey_Matches`), never on the decoded pool address. Linked sprites therefore share one texture. | `src/sprite.c`, `src/glrender.c`, `src/gles_render.c` |
| C2 validation | `SpriteCache_Acquire` checks the encoded span, codec and declared extent before taking pool space, including cache hits; `Sprite_GetPixels` passes the extent derived from the sprite's own dimensions. `Png_Decode` now returns the image read result, so a failed PNG is evicted rather than published. | `src/misc.c`, `src/sprite.c`, `src/pngio.c` |
| P1 accounting | `SpriteSet_MemUse` includes the encoded `SpriteSet.data` block (tracked in a sidecar keyed by the set allocation) and each owned row table. | `src/sprite.c`, `src/sff.c` |
| P3 CPU residency | SFF v2 ldata and tdata images stay encoded at load in a build whose `Gl_Init` set `g_gfxFlags` bit 1. `MUGEN_SFF_EAGER=1` restores the original decode-at-load policy. | `src/sff.c` |
| P3 GPU residency | A byte-budgeted LRU stream texture cache serves sprites that are not `SPRITE_FORMAT_TEXTURE`, so a lazy image is decoded and uploaded once instead of per draw. `MUGEN_SFFTEXMB` sets the budget (default 64 MiB). Entries are invalidated per freed set, not globally, because `Sprite_DrawGL` frees a temporary render-target set every frame while paletted sprites are scaled. `MUGEN_SFFTEXLOG=1` prints hit/miss/eviction/peak counters at shutdown; off by default so a normal `mugen.log` stays comparable. | `src/glrender.c` |

SFF v1.01 (PCX) loading is unchanged and still eager.

### Measured result (real game)

`Hyper DBZ 5.0d`, Windows x86, `-nosound -p1 chars/GokuZ2/GokuZ2.def` (SFF 1.01)
`-p2 chars/VegetaZ2/Normal_VegetaZ2.def` (SFF 2.00) `-p1.ai 1 -p2.ai 1 -rounds 2`.
Peak working set sampled every 200 ms with `tasklist`. Same binary on both
sides; only `MUGEN_SFF_EAGER` differs, and the pairs alternate.

| Run | Eager (original policy) | Lazy (new policy) |
|---|---|---|
| 1 | 183.8 MB | 152.8 MB |
| 2 | 180.4 MB | 151.1 MB |
| 3 | 178.8 MB | 150.1 MB |
| mean | 181.0 MB | 151.3 MB |

**29.7 MB (16.4%) less peak working set on average.** The difference matches the
assets: `armor_vegeta.sff` is 7.0 MB encoded and 30.2 MB decoded (2310 unique
images, 196 linked), and that decoded set used to be allocated for every sprite
at load. The largest single image in the character set is 3.98 MB, inside the
16 MB `SpriteDecompressionBufferSize` this game configures, so no image exceeds
the decoded pool.

Fidelity: both sides reach "Successful program termination".
`tools/compare_logs.py` reports one fragment difference — an extra "Resetting
round" in the eager run, i.e. round timing inside the sampling window — and no
difference in sprite, palette or decode lines. Frame cadence is unchanged at
60.1 fps on both sides.

### Not verified, and why

- **Desktop GL stream cache.** The test machine cannot create a GL context at
  all: `RenderMode = OpenGL` fails at start-up (`trying 640x480x32 mode
  0x0...failed`), which is why this game ships `RenderMode = OpenGLScreen`
  (render mode 1, software rasterizer feeding a GL screen quad). The lazy path
  was therefore exercised through `Sprite_GetPixels` by the software drawers,
  not through `Gl_DrawSprite`; the cache compiles and links but has no runtime
  evidence yet. Criterion 2 of section 12 (zero decodes/uploads for a resident
  image) still needs a GL-capable machine.
- **GLES key fix** (`src/gles_render.c`) is compiled only in a GLES target
  (Android, Linux arm64, Vita). No device was run.
- **Oversized admission (section 6)** is not implemented: an image whose decoded
  extent exceeds the whole pool now retains eager loading (review correction
  below), rather than using a transient lease. Hyper DBZ's largest image fits; a game with a sprite larger than its
  decompression buffer keeps the original eager memory cost for that sprite.
- P2 (independently owned encoded sources and detached palettes), P4
  (source-based context recovery instead of decoded GLES backings), P5 (v1/PCX
  lazy loading, file-backed sources), P6 (warming) and P7 (per-platform rollout)
  are untouched.

### Review corrections (2026-11-16)

- PNG success now combines zero-success read/close results; closing is unconditional.
- Every CPU cache acquisition validates the caller's codec, encoded span and
  decoded extent, including pointer hits. This intentionally repeats validation
  until immutable validation records can safely key the full metadata tuple.
- Lazy SFF loading retains eager decoding for images that exceed pool capacity,
  accounting for pointer alignment, block header and end marker. Thus oversized
  admission no longer silently loses images on the formerly eager desktop path.
  A bounded transient lease remains future work; these images retain the old
  full decoded allocation/load-time upload and its memory cost.
- Desktop/Vita GL cache eviction reserves logical image bytes before upload.
  Native driver padding/staging and physical GPU allocation remain device tests.
- Added Linux-host real PNG/cache/admission regressions and constrained-driver
  cache replacement coverage in the desktop/Vita GL mock suite. These do not
  establish native Android/Vita device qualification.
