/* Run the real input consumer. Command/history processing and state entry are
 * explicit recording boundaries; the native fixture covers their full path. */
#include "mugen.h"
#include "zss_assert.h"
#include <assert.h>
#include "test_abi.h"
#include "../src/char.c"
#ifdef main
#undef main
#endif

static GameVars game;
static Player player, helper;
static CharData owner;
static int changes, last_state, command_reads, command_down;
void SDL_AtomicLock(SDL_SpinLock *lock) {assert(*lock==0);*lock=1;}
void SDL_AtomicUnlock(SDL_SpinLock *lock) {assert(*lock==1);*lock=0;}
void Input_Clear(InputBuf *buf) {memset(buf,0,sizeof(*buf));}
void Input_Resync(InputBuf *buf,const int *keys) {(void)buf;(void)keys;abort();}
unsigned int Input_GetHeld(const InputBuf *buf) {return buf->keys[KEY_CROUCH]!=0;}
int CmdState_AddInput(unsigned int key,CmdState *state,InputBuf *buf,unsigned int frame)
{
    (void)key;(void)state;(void)frame;command_reads++;command_down=buf->keys[KEY_CROUCH];return 0;
}
int CmdState_Update(const CmdSet *set,unsigned char *active,CmdState *state,unsigned int frame,unsigned int mask)
{(void)set;(void)active;(void)state;(void)frame;(void)mask;return 0;}
void Ai_UpdatePlayer(Player *p) {(void)p;}
int Player_ChangeState(int state,Player *p) {changes++;last_state=state;p->state_no=state;return 1;}
int Player_InGuardDist(Player *p,Player *other) {(void)p;(void)other;abort();}
void Player_Turn(Player *p) {(void)p;abort();}
void Player_ChangeAnimSelf(Player *p,int action) {(void)p;(void)action;abort();}
static void reset(void)
{
    ZssAssert_ClearPlayer(&player);ZssAssert_ClearPlayer(&helper);
    memset(&player,0,sizeof(player));memset(&helper,0,sizeof(helper));memset(&game,0,sizeof(game));
    player.id=17;player.index=1;player.data=&owner;player.cmds=&owner.cmds;
    player.ctrl=1;player.statetype=STATETYPE_STAND;player.vel.x=2.0;
    game.input_enabled=1;game.pause_kind=-1;game.game_type_m=1;
    game.round_state=2;game.player_tick[1]=1;game.players_first=1;game.players_last=1;
    player.input.keys[KEY_CROUCH]=1;g_game=&game;changes=command_reads=0;last_state=-1;
}
int main(void)
{
    InputBuf before;
    TEST_REPORT_ABI();
    reset();Player_UpdateInput(&player);
    assert(changes==1 && last_state==10 && player.vel.x==0 && command_down==1);
    reset();assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));before=player.input;
    Player_UpdateInput(&player);
    assert(!changes && player.state_no==0 && player.vel.x==2 && command_reads==1 && command_down==1);
    assert(!memcmp(&before,&player.input,sizeof(before)));
    ZssAssert_BeginTick(&player,0);Player_UpdateInput(&player);assert(!changes && command_down==1);
    ZssAssert_BeginTick(&player,1);Player_UpdateInput(&player);assert(changes==1 && last_state==10);
    reset();player.state_no=100;assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));
    Player_UpdateInput(&player);assert(!changes && player.vel.x==2);
    ZssAssert_ClearPlayer(&player);Player_UpdateInput(&player);assert(last_state==10 && player.vel.x==2);
    reset();player.statetype=STATETYPE_CROUCH;player.state_no=11;
    assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));Player_UpdateInput(&player);
    assert(!changes && player.state_no==11 && command_down==1);
    player.input.keys[KEY_CROUCH]=0;Player_UpdateInput(&player);assert(changes==1 && last_state==12);
    reset();player.ctrl=0;assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));
    Player_UpdateInput(&player);assert(!changes);
    reset();player.input.keys[KEY_JUMP]=1;assert(ZssAssert_Set(&player,ZSS_ASSERT_NO_CROUCH));
    Player_UpdateInput(&player);assert(last_state==40 && command_down==1);
    reset();helper.id=player.id;helper.helper=1;assert(ZssAssert_Set(&helper,ZSS_ASSERT_NO_CROUCH));
    Player_UpdateInput(&player);assert(last_state==10 && !ZssAssert_Test(&player,ZSS_ASSERT_NO_CROUCH));
    ZssAssert_ClearPlayer(&player);ZssAssert_ClearPlayer(&helper);
    puts("ok: real nocrouch input consumer, raw command directions, crouch hold/release, run expiry and isolation");
    return 0;
}
