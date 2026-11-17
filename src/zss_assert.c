/* Port addition: bounded actor-owned Ikemen assertion state. Unlike maps these
 * flags expire each running tick. Storage never allocates and is independent
 * of program/CharData ownership, custom states and MapReset. */
#include "mugen.h"
#include "zss_assert.h"

typedef struct ScriptAssertions {
    Player *player;
    int id;
    uint32_t flags;
} ScriptAssertions;
static ScriptAssertions assertions[ZSS_ASSERT_PLAYERS_MAX];
static SDL_SpinLock assertion_lock;

static ScriptAssertions *assertion_slot(Player *p, int create)
{
    ScriptAssertions *empty=NULL;
    size_t i;
    for (i=0;i<ZSS_ASSERT_PLAYERS_MAX;i++) {
        ScriptAssertions *slot=&assertions[i];
        if (slot->player==p) {
            if (slot->id==p->id) return slot;
            /* Helpers recycle addresses; a new generation inherits no flags. */
            memset(slot,0,sizeof(*slot));
        }
        if (empty==NULL && slot->player==NULL) empty=slot;
    }
    return create ? empty : NULL;
}
int ZssAssert_Set(Player *p, int index)
{
    ScriptAssertions *slot;
    int ok=0;
    if (p==NULL || index!=ZSS_ASSERT_NO_CROUCH) return 0;
    SDL_AtomicLock(&assertion_lock);
    slot=assertion_slot(p,1);
    if (slot!=NULL) {
        slot->player=p;slot->id=p->id;slot->flags|=UINT32_C(1);ok=1;
    }
    SDL_AtomicUnlock(&assertion_lock);return ok;
}
int ZssAssert_Test(Player *p, int index)
{
    ScriptAssertions *slot;
    int value=0;
    if (p==NULL || index!=ZSS_ASSERT_NO_CROUCH) return 0;
    SDL_AtomicLock(&assertion_lock);
    slot=assertion_slot(p,0);
    if (slot!=NULL) value=(slot->flags&UINT32_C(1))!=0;
    SDL_AtomicUnlock(&assertion_lock);return value;
}
void ZssAssert_ClearPlayer(Player *p)
{
    size_t i;
    if (p==NULL) return;
    SDL_AtomicLock(&assertion_lock);
    for (i=0;i<ZSS_ASSERT_PLAYERS_MAX;i++) if (assertions[i].player==p)
        memset(&assertions[i],0,sizeof(assertions[i]));
    SDL_AtomicUnlock(&assertion_lock);
}
void ZssAssert_BeginTick(Player *p, int running)
{
    if (running!=0) ZssAssert_ClearPlayer(p);
}
