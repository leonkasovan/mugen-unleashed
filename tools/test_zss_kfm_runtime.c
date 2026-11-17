/* Native qualification adapter, linked with --wrap=Round_Tick. Observes the
 * real engine and injects SDL keyboard events; never forces states or damage.
 * This file is not part of the production engine source glob. */
#include "mugen.h"
#include <math.h>

int __real_Round_Tick(ScreenLoop *screen);
static void key(SDL_Keycode code,int down)
{
    SDL_Event event={0};
    event.type=down ? SDL_KEYDOWN : SDL_KEYUP;event.key.state=down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym=code;event.key.keysym.scancode=SDL_GetScancodeFromKey(code);
    SDL_PushEvent(&event);
}
int __wrap_Round_Tick(ScreenLoop *screen)
{
    static FILE *trace;
    static int fight_ticks,held[5],started;
    static const SDL_Keycode keys[]={SDLK_RIGHT,SDLK_UP,SDLK_DOWN,SDLK_l,SDLK_COMMA};
    Player *p=NULL,*other=NULL;
    int i,result,want[5]={0};
    const char *mode=getenv("MUGEN_KFM_TEST_MODE");
    if(mode==NULL) return __real_Round_Tick(screen);
    if(!trace) {trace=fopen("kfm-runtime.csv","w");if(!trace) abort();
        fputs("frame,round,player,state,time,life,x,y,ctrl,hitpause,hitcount,anim\n",trace);}
    for(i=g_game->players_first;i<=g_game->players_last;i++) if(g_game->players[i]!=NULL && !g_game->players[i]->helper) {
        if(p==NULL) p=g_game->players[i];else if(other==NULL) other=g_game->players[i];
    }
    if(p!=NULL && other!=NULL) {
        if(!started) {g_game->exit_frame=(int)g_game->frames+(!strcmp(mode,"ai") ? 5500 : 2100);started=1;}
        if(g_game->round_state==2) {
            fight_ticks++;
            if(!strcmp(mode,"human")) {
                if(fight_ticks<75) want[0]=1;
                else if(fight_ticks<150) want[1]=1;
                else if(fight_ticks<210) want[2]=1;
                else if(fight_ticks<700) {
                    if(Player_ToLocalCoord(p,fabs(other->pos.x-p->pos.x))>40) want[0]=1;
                    else want[3]=(fight_ticks%45)<3;
                } else if(fight_ticks<1300) {
                    if(Player_ToLocalCoord(p,fabs(other->pos.x-p->pos.x))>40) want[0]=1;
                    else want[4]=(fight_ticks%50)<3;
                }
            }
            if(fight_ticks==600 || fight_ticks==1000) g_game->screenshot_requested=1;
        }
        for(i=0;i<5;i++) if(want[i]!=held[i]) {key(keys[i],want[i]);held[i]=want[i];}
    }
    result=__real_Round_Tick(screen);
    for(i=g_game->players_first;i<=g_game->players_last;i++) {
        Player *actor=g_game->players[i];
        if(actor==NULL || actor->helper) continue;
        fprintf(trace,"%u,%d,%d,%d,%d,%d,%.4f,%.4f,%d,%d,%d,%d\n",g_game->frames,g_game->round_state,
            actor->index,actor->state_no,actor->state_time,actor->life,actor->pos.x,actor->pos.y,actor->ctrl,
            actor->hitpause_time,actor->hitcount,actor->anim!=NULL ? actor->anim->anim->actions->slots[actor->anim->action].key2 : -1);
    }
    fflush(trace);return result;
}
