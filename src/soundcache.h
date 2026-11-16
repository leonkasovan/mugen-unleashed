/* Port addition: game-thread-owned lazy character sounds; no legacy layout changes. */
#ifndef MUGEN_SOUNDCACHE_H
#define MUGEN_SOUNDCACHE_H
/* Port config, copied from [Sound]; optional prewarm/trace strings may be NULL. */
void SoundCache_Configure(int lazy, int automatic, int initial_mb, int maximum_mb,
                          const char *prewarm, const char *trace_path);
int SoundCache_Enabled(void);
/* Add takes encoded-buffer ownership only on success. */
int SoundCache_Add(Snd *bank, int index, void *encoded, int size);
int SoundCache_Resolve(Snd *bank, int index);
int SoundCache_Find(Snd *bank, int group, int number);
void SoundCache_Bind(int slot, Snd *bank, int index);
void SoundCache_Started(int slot, int result);
void SoundCache_Release(int slot);
int SoundCache_Prepared(int slot);
int SoundCache_Bound(int slot);
void SoundCache_Tick(void);
void SoundCache_FreeBank(Snd *bank);
/* Full teardown versus device reset (which retains encoded sources). */
void SoundCache_Reset(void);
void SoundCache_Invalidate(void);
void SoundCache_WarmBank(Snd *bank);
#endif
