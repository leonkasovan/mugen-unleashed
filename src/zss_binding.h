#ifndef MUGEN_ZSS_BINDING_H
#define MUGEN_ZSS_BINDING_H
#include "zss_program.h"
/* Port addition: positive states resolve first-wins; negative ZSS bodies
 * append in file order. Mixed negative CNS/ZSS composition is rejected.
 * Installation retains program ownership; caller still releases its
 * own reference with Zss_FreeProgram. Function modules resolve first-wins in
 * collection load order; function-only files may add no playable states.
 * A private non-state keyed entry owns their library. Original layouts remain
 * unchanged; ordinary game state lookups continue to use key2=0. */
int Zss_InstallProgram(ZssProgram *program, StateDefs *defs, const char **error);
int Zss_IsBoundState(const StateDef *state);
void Zss_PrepareStateCounters(Player *player,const StateDef *state,int force);
void Zss_RunGlobalState(Player *player);
int Zss_RunBoundBody(Player *p, void *binding, StateRunCtx *context);
void Zss_FreeBoundBody(void *binding);
/* Reads the current Stream position to EOF without taking Stream ownership. */
int Zss_LoadStateStream(Stream *stream, StateDefs *defs, CharData *owner, ZssDiagnostic *diagnostic);
/* Primary source: create a collection, or release all partial state on failure.
 * The caller retains Stream ownership on both paths. */
StateDefs *Zss_LoadStateStreamNew(Stream *stream, CharData *owner, ZssDiagnostic *diagnostic);
#endif
