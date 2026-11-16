#ifndef MUGEN_ZSS_BINDING_H
#define MUGEN_ZSS_BINDING_H
#include "zss_program.h"
/* Prototype integration: collisions are rejected rather than merging CNS/ZSS
 * states. Installation retains program ownership; caller still releases its
 * own reference with Zss_FreeProgram. Original structure layouts are unchanged. */
int Zss_InstallProgram(ZssProgram *program, StateDefs *defs, const char **error);
int Zss_IsBoundState(const StateDef *state);
int Zss_RunBoundBody(Player *p, void *binding, StateRunCtx *context);
void Zss_FreeBoundBody(void *binding);
/* Reads the current Stream position to EOF without taking Stream ownership. */
int Zss_LoadStateStream(Stream *stream, StateDefs *defs, CharData *owner, ZssDiagnostic *diagnostic);
#endif
