#ifndef MUGEN_ZSS_ASSERT_H
#define MUGEN_ZSS_ASSERT_H

/* Port addition: runtime assertion extensions without changing the original
 * actor layout or the legacy CNS flag-name table. Metadata uses an index, not
 * a pointer encoded in an Expr integer. All functions accept a NULL actor. */
#define ZSS_ASSERT_KIND 2
#define ZSS_ASSERT_NO_CROUCH 0
#define ZSS_ASSERT_NO_JUMP 1
#define ZSS_ASSERT_NO_AIR_JUMP 2
#define ZSS_ASSERT_NO_BRAKE 3
#define ZSS_ASSERT_AUTO_GUARD 4
#define ZSS_ASSERT_COUNT 5
#define ZSS_ASSERT_PLAYERS_MAX 64 /* engine maximum: 4 players + 56 helpers */
int ZssAssert_Set(Player *player, int index);
int ZssAssert_Test(Player *player, int index);
void ZssAssert_ClearPlayer(Player *player);
/* Called after input consumption, before state execution. Paused actors keep
 * assertions until their next running preparation; hitpause still clears them
 * on a running tick, as in the supported MUGEN-1.x profile. */
void ZssAssert_BeginTick(Player *player, int running);

#ifdef MUGEN_ROLLBACK
void *ZssAssert_RollbackStorage(size_t *size);
#endif

#endif
