/* Port addition: encoded sources + global soft PCM budget. All mutations run
 * on the game thread; the mixer sees only leased, completely decoded chunks.
 * Cold misses are synchronous and opt-in: the existing Ogg loader can hold
 * the mixer lock for a whole clip. This is not a streaming audio backend. */
#include "mugen.h"
#include "soundcache.h"
#include <SDL_mixer.h>
#include <errno.h>
#include <string.h>

typedef struct CachedSound {
    struct CachedSound *next;
    Snd *bank;
    int index, size, failed;
    unsigned int refs;
    unsigned long long used, bank_id;
    Uint32 retry_after;
    unsigned char *encoded;
    Mix_Chunk *chunk;
} CachedSound;
static CachedSound *sounds;
static struct { CachedSound *sound; int started; } leases[SOUND_SLOT_COUNT];
static size_t pcm_bytes, source_bytes;
static unsigned long long serial, hits, misses, evictions, next_bank_id;
static double max_decode_ms;
static FILE *trace;
static Uint32 last_report;
/* Port addition: optional bounded growth; reset when the last bank is freed. */
static size_t current_budget, configured_initial, configured_max;
static int configured_auto, budget_initialized;
static int config_lazy, config_auto;
static size_t config_initial = 8 * 1024 * 1024, config_maximum = 20 * 1024 * 1024;
static char config_prewarm[4096], config_trace[4096];

static SndEntry *entry(CachedSound *s)
{
    return (SndEntry *)(s->bank->map->data + s->bank->map->elem_size * s->index);
}
static CachedSound *find(Snd *bank, int index)
{
    CachedSound *s;
    for (s = sounds; s != NULL; s = s->next)
        if (s->bank == bank && s->index == index) return s;
    return NULL;
}
void SoundCache_Configure(int lazy, int automatic, int initial_mb, int maximum_mb,
                          const char *prewarm, const char *trace_path)
{
    config_lazy = lazy == 1; config_auto = automatic == 1;
    if (initial_mb < 0 || initial_mb > 1024) initial_mb = 8;
    if (maximum_mb < 0 || maximum_mb > 1024) maximum_mb = 20;
    config_initial = (size_t)initial_mb * 1024 * 1024;
    config_maximum = (size_t)maximum_mb * 1024 * 1024;
    snprintf(config_prewarm, sizeof(config_prewarm), "%s", prewarm != NULL ? prewarm : "");
    if (strcmp(config_trace, trace_path != NULL ? trace_path : "") != 0 && trace != NULL) {
        fclose(trace); trace = NULL;
    }
    snprintf(config_trace, sizeof(config_trace), "%s", trace_path != NULL ? trace_path : "");
}
int SoundCache_Enabled(void)
{
    return config_lazy;
}
static size_t budget(void)
{
    size_t initial = config_initial, maximum = config_maximum;
    int automatic = config_auto;
    if (maximum < initial) maximum = initial;
    if (!budget_initialized || initial != configured_initial ||
        maximum != configured_max || automatic != configured_auto) {
        configured_initial = initial; configured_max = maximum;
        configured_auto = automatic; current_budget = initial;
        budget_initialized = 1;
    }
    return current_budget;
}
static size_t grow_budget(void)
{
    size_t limit = budget(), previous = limit;
    const size_t mib = 1024 * 1024;
    /* Zero retains its existing meaning: keep only leased PCM. Grow in
     * whole-MiB steps of about 25%, without reserving memory in advance. */
    if (configured_auto && configured_initial != 0) {
        while (pcm_bytes > limit && limit < configured_max) {
            size_t step = ((limit / 4 + mib - 1) / mib) * mib;
            if (step < mib) step = mib;
            limit += step;
            if (limit > configured_max) limit = configured_max;
        }
        current_budget = limit;
        if (limit != previous)
            Log_Printf("soundcache: budget grew from %zu to %zu MiB (max=%zu MiB, pcm=%zu bytes)\n",
                       previous / mib, limit / mib, configured_max / mib, pcm_bytes);
    }
    return limit;
}
static void destroy_pcm(CachedSound *s)
{
    if (s->chunk != NULL) {
        void *owned = s->chunk->allocated == 0 ? s->chunk->abuf : NULL;
        pcm_bytes -= s->chunk->alen;
        Mix_FreeChunk(s->chunk);
        free(owned);
        s->chunk = NULL;
        entry(s)->chunk = NULL;
    }
}
int SoundCache_Add(Snd *bank, int index, void *encoded, int size)
{
    CachedSound *s = calloc(1, sizeof(*s));
    const char *path;
    if (s == NULL) return 0;
    {
        CachedSound *other;
        for (other = sounds; other != NULL && other->bank != bank; other = other->next) {}
        s->bank_id = other != NULL ? other->bank_id : ++next_bank_id;
    }
    s->bank = bank; s->index = index; s->encoded = encoded; s->size = size;
    s->next = sounds; sounds = s; source_bytes += size;
    path = config_trace;
    if (trace == NULL && path != NULL && *path) trace = fopen(path, "a");
    return 1;
}
int SoundCache_Resolve(Snd *bank, int index)
{
    CachedSound *s = find(bank, index);
    Stream *input;
    int status = 0;
    Uint64 start;
    double elapsed = 0;
    if (s == NULL) return 1; /* eager bank */
    if (s->failed || (s->retry_after && (Sint32)(SDL_GetTicks() - s->retry_after) < 0)) return 0;
    if (s->chunk != NULL) ++hits;
    else {
        ++misses;
        input = Stream_OpenMemory(s->encoded, s->size);
        if (input == NULL) { s->retry_after = SDL_GetTicks() + 250; return 0; }
        errno = 0;
        SDL_ClearError();
        start = SDL_GetPerformanceCounter();
        s->chunk = s->size >= 4 && memcmp(s->encoded, "OggS", 4) == 0
                       ? Snd_DecodeOggResampled(input, &status)
                       : Snd_DecodeWav(input, &status);
        elapsed = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
        Stream_Close(input);
        if (elapsed > max_decode_ms) max_decode_ms = elapsed;
        if (s->chunk == NULL) {
            /* OOM is retryable. Bad data is logged once per bank entry. */
            if (status != WAV_ERR_NOMEM && !g_outOfMemory && errno != ENOMEM &&
                strcmp(SDL_GetError(), "Out of memory") != 0) {
                s->failed = 1;
                Log_Printf("soundcache: invalid entry %d (status %d)\n", index, status);
            }
            else s->retry_after = SDL_GetTicks() + 250;
            return 0;
        }
        s->retry_after = 0;
        pcm_bytes += s->chunk->alen;
        entry(s)->chunk = s->chunk;
    }
    s->used = ++serial;
    if (trace != NULL) {
        KeyedArraySlot *key = &bank->map->slots[index];
        fprintf(trace, "request,%u,%llu,%llu,%d,%d,%d,%u,%.3f\n", SDL_GetTicks(), serial, s->bank_id, s->index,
                key->key1, key->key2, s->chunk->alen, elapsed);
        fflush(trace);
    }
    return 1;
}
int SoundCache_Bound(int slot) { return leases[slot].sound != NULL; }
int SoundCache_Prepared(int slot)
{
    return leases[slot].sound != NULL && !leases[slot].started;
}
void SoundCache_Release(int slot)
{
    if (slot < 0 || slot >= SOUND_SLOT_COUNT) return;
    if (leases[slot].sound != NULL) {
        if (trace != NULL) fprintf(trace, "release,%u,%llu,%d,%d\n", SDL_GetTicks(), leases[slot].sound->bank_id, slot, leases[slot].sound->index);
        --leases[slot].sound->refs;
        leases[slot].sound = NULL;
    }
    leases[slot].started = 0;
}
void SoundCache_Bind(int slot, Snd *bank, int index)
{
    CachedSound *s = find(bank, index);
    SoundCache_Release(slot);
    if (s != NULL) {
        ++s->refs; leases[slot].sound = s;
        if (trace != NULL) fprintf(trace, "prepare,%u,%llu,%d,%d\n", SDL_GetTicks(), s->bank_id, slot, index);
    }
}
void SoundCache_Started(int slot, int result)
{
    if (result < 0) {
        if (leases[slot].sound != NULL) g_sndSlots[slot].chunk = NULL;
        SoundCache_Release(slot);
    }
    else if (leases[slot].sound != NULL) {
        leases[slot].started = 1;
        if (trace != NULL && leases[slot].sound != NULL) fprintf(trace, "start,%u,%llu,%d,%d\n", SDL_GetTicks(), leases[slot].sound->bank_id, slot, result);
    }
}
void SoundCache_Tick(void)
{
    CachedSound *s, *old;
    size_t limit, pinned = 0;
    int i;
    if (sounds == NULL) return;
    limit = grow_budget();
    for (i = 0; i < g_sndSlotCount && i < SOUND_SLOT_COUNT; ++i) {
        s = leases[i].sound;
        if (s != NULL && leases[i].started) {
            int ch = g_sndSlots[i].channel;
            if (ch < 0 || !Mix_Playing(ch) || Mix_GetChunk(ch) != s->chunk) {
                SoundCache_Release(i);
                g_sndSlots[i].chunk = NULL;
            }
        }
    }
    while (pcm_bytes > limit) {
        old = NULL;
        for (s = sounds; s != NULL; s = s->next)
            if (s->chunk != NULL && s->refs == 0 && (old == NULL || s->used < old->used)) old = s;
        if (old == NULL) break; /* never cut off active/prepared sounds */
        destroy_pcm(old); ++evictions;
    }
    if (trace != NULL) fprintf(trace, "tick,%u\n", SDL_GetTicks());
    if (sounds != NULL && SDL_GetTicks() - last_report >= 1000) {
        for (s = sounds; s != NULL; s = s->next)
            if (s->chunk != NULL && s->refs) pinned += s->chunk->alen;
        Log_Printf("soundcache: source=%zu pcm=%zu pinned=%zu budget=%zu hits=%llu misses=%llu evictions=%llu max_decode_ms=%.3f\n",
            source_bytes, pcm_bytes, pinned, limit, hits, misses, evictions, max_decode_ms);
        last_report = SDL_GetTicks();
    }
}
void SoundCache_FreeBank(Snd *bank)
{
    CachedSound **link = &sounds, *s;
    int i;
    for (s = sounds; s != NULL; s = s->next) if (s->bank == bank) {
        if (trace != NULL) fprintf(trace, "free,%u,%llu\n", SDL_GetTicks(), s->bank_id);
        break;
    }
    while ((s = *link) != NULL) {
        if (s->bank != bank) { link = &s->next; continue; }
        /* Bank teardown may stop users, unlike ordinary eviction. */
        for (i = 0; i < SOUND_SLOT_COUNT; ++i) if (leases[i].sound == s) {
            SoundCache_Release(i);
            if (g_sndSlots != NULL && i < g_sndSlotCount) g_sndSlots[i].chunk = NULL;
        }
        destroy_pcm(s); source_bytes -= s->size;
        free(s->encoded); *link = s->next; free(s);
    }
    if (sounds == NULL) {
        if (trace != NULL) { fclose(trace); trace = NULL; }
        budget_initialized = 0;
    }
}
void SoundCache_Reset(void)
{
    while (sounds != NULL) SoundCache_FreeBank(sounds->bank);
    if (trace != NULL) { fclose(trace); trace = NULL; }
    serial = hits = misses = evictions = 0; max_decode_ms = 0;
}

/* Optional measured hot set, warmed only during bank loading. */
void SoundCache_WarmBank(Snd *bank)
{
    const char *p = config_prewarm;
    while (*p) {
        int group, number, consumed = 0;
        if (sscanf(p, "%d:%d%n", &group, &number, &consumed) != 2 || consumed <= 0) break;
        if (budget() != 0) SoundCache_Find(bank, group, number);
        SoundCache_Tick();
        p += consumed;
        if (*p != ',') break;
        ++p;
    }
}

/* Device/format reset drops PCM and leases but keeps bank-owned source bytes. */
void SoundCache_Invalidate(void)
{
    CachedSound *s;
    int i;
    if (trace != NULL) fprintf(trace, "invalidate,%u\n", SDL_GetTicks());
    for (i = 0; i < SOUND_SLOT_COUNT; ++i) {
        if (leases[i].sound != NULL && g_sndSlots != NULL && i < g_sndSlotCount)
            g_sndSlots[i].chunk = NULL;
        SoundCache_Release(i);
    }
    for (s = sounds; s != NULL; s = s->next) { destroy_pcm(s); s->failed = 0; s->retry_after = 0; }
}

/* Eager parsing skips undecodable duplicate keys. Preserve that lookup rule
 * when the first duplicate is only discovered to be bad at playback time. */
int SoundCache_Find(Snd *bank, int group, int number)
{
    int i = KeyedArray_Find(bank->map, group, number);
    if (i < 0) return -1;
    for (; i <= bank->map->last; ++i) {
        KeyedArraySlot *slot = &bank->map->slots[i];
        if (slot->used && slot->key1 == group && slot->key2 == number &&
            SoundCache_Resolve(bank, i)) return i;
    }
    return -1;
}
