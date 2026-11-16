#ifndef MUGEN_ZSS_PROGRAM_H
#define MUGEN_ZSS_PROGRAM_H
#include "zss_controller.h"
typedef struct ZssProgram ZssProgram;
/* Prototype profile: states, enabled controllers, nested if/else, assignments,
 * while and anonymous for ranges. Explicit stN stream installation is experimental;
 * named for counters, locals, functions and attributes are not enabled. Compilation
 * owns all output; source can be released immediately. *out is unchanged on
 * failure. Program limits: 64 states, 1024 nodes, 32 nested blocks, 1 MiB.
 * Runtime guards: 2500 iterations per loop and 100000 control steps per body. */
int Zss_CompileProgram(const char *source, size_t size, CharData *owner,
                       ZssProgram **out, ZssDiagnostic *diagnostic);
/* Release one owned reference; installed state bindings retain their own. */
void Zss_FreeProgram(ZssProgram *program);
size_t Zss_ProgramStateCount(const ZssProgram *program);
const StateDef *Zss_ProgramState(const ZssProgram *program, size_t index, int *number);
/* Callback dispatch boundary: the engine adapter must call StateCtrl_Run, with
 * its StateRunCtx/AI/hitpause bookkeeping. This isolated executor does not apply
 * state entry properties or install itself in Player_RunState. Callback returns
 * original STATECTRL_* bits; it must not free or alter the program. */
typedef int (*ZssControllerRun)(Player *, StateCtrl *, void *context);
int Zss_RunBody(ZssProgram *program, size_t state, Player *player,
                ZssControllerRun run, void *context, int *result);
#endif
